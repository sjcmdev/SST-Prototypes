#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <fstream>
#include <thread>

namespace SST::ScapsLab {
namespace {
std::filesystem::path Directory() {
    const auto installed = std::filesystem::current_path() / "tutorials";
    return std::filesystem::exists(installed / "first-device.json") ? installed : std::filesystem::path(SCAPSLAB_TUTORIALS);
}
}
Json TutorialDefinition(const std::string &id) {
    if (id != "first-device" && id != "light-iv-qe" && id != "history-sweep" && id != "load-grading-profile") throw std::invalid_argument("Unknown tutorial_id: " + id);
    std::ifstream stream(Directory() / (id + ".json"));
    if (!stream) throw std::runtime_error("Cannot read tutorial: " + id);
    auto definition = Json::parse(stream);
    for (auto &step : definition.at("steps")) {
        auto &command = step.at("command");
        if(command.contains("profile_example")) {
            if(command.at("profile_example")!="doping.grd")throw std::invalid_argument("Unknown tutorial profile");
            command["path"]=(ExamplesDirectory()/"profiles/doping.grd").string();command.erase("profile_example");
        }
        if (command.contains("example")) {
            const auto example = command.at("example").get<std::string>();
            if (example != "08-pn-illumination.yaml") throw std::invalid_argument("Unknown tutorial example");
            command["document"] = ReadProject((ExamplesDirectory() / example).string());
            command.erase("example");
        }
    }
    return definition;
}
Json TutorialCatalog() {
    Json catalog = Json::array();
    for (const auto *id : {"first-device", "light-iv-qe", "history-sweep", "load-grading-profile"}) {
        const auto definition = TutorialDefinition(id);
        catalog.push_back({{"tutorial_id", id}, {"title", definition.at("title")}, {"step_count", definition.at("steps").size()}});
    }
    return catalog;
}
Tutorial::Predicates Workflow::TutorialPredicates() {
    return {
        {"profile.loaded", [this] {const auto &layer=Current().document.at("layers")[0];return Json(layer.contains("grading") && layer.at("grading").contains("Na_m3") && layer.at("grading").at("Na_m3").value("law",std::string{})=="table");}},
        {"device.name", [this] { return Json(Current().name); }},
        {"device.layer_count", [this] { return Json(Current().document.at("layers").size()); }},
        {"device.dark", [this] { return Json(!Current().document.value("illumination", Json::object()).value("enabled", false)); }},
        {"device.light", [this] { return Json(Current().document.value("illumination", Json::object()).value("enabled", false)); }},
        {"result.iv.valid", [this] { return Json(Current().result.value("valid", false)); }},
        {"result.iv.Voc_V", [this] { return Current().result.value("photovoltaic", Json::object()).value("Voc_V", Json(nullptr)); }},
        {"result.iv.Jsc_A_m2", [this] { return Current().result.value("photovoltaic", Json::object()).value("Jsc_A_m2", Json(nullptr)); }},
        {"result.iv.FF_fraction", [this] { return Current().result.value("photovoltaic", Json::object()).value("FF_fraction", Json(nullptr)); }},
        {"result.qe.valid", [this] { return Json(Current().acResults.value("qe", Json::object()).value("valid", false)); }},
        {"history.pinned_count", [this] { std::size_t count=0; for (const auto &run : Current().history) if (run.at("pinned")==true) ++count; return Json(count); }},
        {"sweep.valid_count", [this] { std::size_t count=0; for (const auto &row : Current().batchResult.value("rows",Json::array())) if (row.value("status",std::string{})=="Converged") ++count; return Json(count); }}
    };
}
void Workflow::TutorialRequest(const Json &request) {
    const std::string operation = request.at("operation");
    if (operation == "tutorial_start") {
        if (Busy()) throw std::invalid_argument("Wait for the active operation before starting a tutorial");
        const auto id = request.at("tutorial_id").get<std::string>();
        const auto mode = request.value("mode", std::string("learn"));
        const auto resume = request.value("resume", false) && mode == "learn" ? tutorialProgress.value(id, Json::object()).value("step_index", std::size_t(0)) : 0;
        auto definition = TutorialDefinition(id);
        tutorial.Start(std::move(definition), mode, TutorialPredicates(), resume);
        tutorialAuto=autoRecalculate; tutorialBand=autoRecalculateBand;
        autoRecalculate=false; autoRecalculateBand=false;
    } else if (operation == "tutorial_stop") {
        if(tutorial.Active()) {tutorial.Stop(); autoRecalculate=tutorialAuto;autoRecalculateBand=tutorialBand;}
    } else if (operation != "tutorial_state" && operation != "tutorial_list") throw std::invalid_argument("Unknown tutorial operation");
    response={{"ok",true},{"error",nullptr},{"tutorial",tutorial.Report()},{"progress",tutorialProgress}};
    if (operation=="tutorial_list") response["tutorials"]=TutorialCatalog();
}
void Workflow::UpdateTutorial() {
    if (!tutorial.Active()) return;
    const auto before=tutorial.Report();
    tutorial.Tick(TutorialPredicates(), [this](Json command) {
        if (command.value("run_id", std::string{})=="$selected_run") command["run_id"]=Current().selectedRun;
        return Dispatch(command);
    }, Busy(), response);
    const auto report=tutorial.Report();
    if (before!=report) {
        if (report.at("mode")!="verify") {
            auto progress=report;
            if (report.at("status")=="passed") progress["step_index"]=0;
            tutorialProgress[report.at("tutorial_id").get<std::string>()]=progress;
            if (!tutorialProgressPath.empty()) {
                std::string error;
                if (!TextFileIO::Save(Path{tutorialProgressPath}, tutorialProgress.dump(2), error)) status="Tutorial progress: "+error;
            }
        }
        if (!tutorial.Active()) {autoRecalculate=tutorialAuto;autoRecalculateBand=tutorialBand;}
        if (!Busy()) response={{"ok",report.at("status")!="failed"},{"tutorial",report},{"error",report.at("status")=="failed"?Json{{"code","tutorial.failed"},{"message","See tutorial step report"}}:Json(nullptr)}};
    }
}
Json VerifyTutorial(const std::string &id) {
    Workflow workflow(CreateRef<JobSystem>(nullptr,1));
    workflow.Current().document=ReadProject(SCAPSLAB_PROJECT);
    const auto started=workflow.Dispatch({{"operation","tutorial_start"},{"tutorial_id",id},{"mode","verify"}});
    if (!started.value("ok",false)) return {{"schema_version",1},{"tutorial_id",id},{"status","failed"},{"ok",false},{"error",started.at("error")},{"steps",Json::array()}};
    while (workflow.tutorial.Automatic() || workflow.Busy()) {
        workflow.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return workflow.tutorial.Report();
}
}
