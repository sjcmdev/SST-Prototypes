#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <scaps_core/illumination.hpp>
#include <fstream>
#include <set>
#include <thread>

namespace SST::ScapsLab {
namespace {
std::filesystem::path Directory() {
    const auto installed = std::filesystem::current_path() / "tutorials";
    return std::filesystem::exists(installed / "first-device.json") ? installed : std::filesystem::path(SCAPSLAB_TUTORIALS);
}
std::filesystem::path TutorialExportPath(const std::string &name) { return std::filesystem::temp_directory_path() / name; }
// Fills a document the way the optical-file widgets do (embedded table + remembered source name).
void EmbedTable(Json &object, const char *file, const char *table, const Json &points, const std::string &path) {
    object[table] = points; object[std::string("scapslab_") + file] = path; object[file] = "";
}
void PatchTutorialDocument(Json &document, const std::string &patch) {
    const auto profiles = ExamplesDirectory() / "profiles";
    auto &layer = document.at("layers")[0];
    if (patch == "defect") layer["defects"] = Json::array({{{"scapslab_id", "defect-1-1"}, {"label", "midgap"}, {"energy_reference", "relative_to_Ev"}, {"energy_offset_eV", 0.56}, {"Nt_m3", 1e20}, {"charge_type", "neutral"}, {"sigma_n_m2", 1e-19}, {"sigma_p_m2", 1e-19}, {"density_profile_m3", {{"law", "uniform"}, {"left", 1e20}, {"right", 1e20}}}}});
    else if (patch == "spectrum.spe") {
        auto &light = document["illumination"]; const auto path = (profiles / "spectrum.spe").string();
        light["mode"] = "spectrum"; light["scapslab_spectrum"] = "custom"; EmbedTable(light, "spectrum_file", "spectrum_nm_W_m2", Json(scaps_core::read_scaps_spectrum(path)), path);
    } else if (patch == "filter.ftr") {
        auto &light = document["illumination"]; const auto path = (profiles / "filter.ftr").string();
        EmbedTable(light, "front_filter_file", "front_filter_nm_fraction", Json(scaps_core::read_scaps_filter(path)), path);
    } else if (patch == "capture.opt") {
        auto &optical = layer["defects"][0]["optical"]; const auto path = (profiles / "capture.opt").string();
        optical = {{"enabled", true}}; EmbedTable(optical, "electron_file", "electron_table_nm_m2", Json(scaps_core::read_scaps_absorption(path)), path);
    } else if (patch == "eg-1.5") layer["material"]["Eg_eV"] = 1.5;
    else if (patch == "absorption.abs") {
        auto &absorption = layer["absorption"]; const auto path = (profiles / "absorption.abs").string();
        absorption = {{"model", "file"}}; EmbedTable(absorption, "file", "table_nm_m1", Json(scaps_core::read_scaps_absorption(path)), path);
    } else throw std::invalid_argument("Unknown tutorial patch");
}
}
Json TutorialDefinition(const std::string &id) {
    if (!std::set<std::string>{"first-device", "heterojunction", "bulk-defect", "interface-defect", "contacts", "light-iv-qe", "history-sweep", "load-grading-profile", "nt-defect-profile", "generation-from-file", "spectrum-absorption-files", "compare-runs-sweep", "export-profile", "filter-optical-files", "material-file", "table-undo-redo", "menus-tour"}.contains(id)) throw std::invalid_argument("Unknown tutorial_id: " + id);
    std::ifstream stream(Directory() / (id + ".json"));
    if (!stream) throw std::runtime_error("Cannot read tutorial: " + id);
    auto definition = Json::parse(stream);
    for (auto &step : definition.at("steps")) {
        auto &command = step.at("command");
        if(command.contains("profile_example")) {
            const auto name=command.at("profile_example").get<std::string>();
            if(!std::set<std::string>{"doping.grd","defects.grd","generation.gen"}.contains(name))throw std::invalid_argument("Unknown tutorial profile");
            command["path"]=(ExamplesDirectory()/"profiles"/name).string();command.erase("profile_example");
        }
        if(command.contains("path_target")) {command["path"]=TutorialExportPath(command.at("path_target").get<std::string>()).string();command.erase("path_target");}
        if(command.contains("export_target")) {
            const auto name=command.at("export_target").get<std::string>();
            if(name!="scapslab-tutorial-profile.csv" && name!="scapslab-tutorial-profile.grd" && name!="scapslab-tutorial.material")throw std::invalid_argument("Unknown tutorial export");
            const auto path=TutorialExportPath(name);std::error_code ec;std::filesystem::remove(path,ec);
            command["path"]=path.string();command.erase("export_target");
        }
        if (command.contains("example")) {
            const auto example = command.at("example").get<std::string>();
            if (example != "08-pn-illumination.yaml") throw std::invalid_argument("Unknown tutorial example");
            auto document = ReadProject((ExamplesDirectory() / example).string());
            for (const auto &patch : command.value("patches", Json::array())) PatchTutorialDocument(document, patch.get<std::string>());
            if (command.contains("document_set")) document.update(command.at("document_set"), true);
            command["document"] = std::move(document);
            command.erase("example");command.erase("patches");command.erase("document_set");
        }
    }
    return definition;
}
Json TutorialCatalog() {
    Json catalog = Json::array();
    for (const auto *id : {"first-device", "heterojunction", "bulk-defect", "interface-defect", "contacts", "light-iv-qe", "history-sweep", "load-grading-profile", "nt-defect-profile", "generation-from-file", "spectrum-absorption-files", "compare-runs-sweep", "export-profile", "filter-optical-files", "material-file", "table-undo-redo", "menus-tour"}) {
        const auto definition = TutorialDefinition(id);
        catalog.push_back({{"tutorial_id", id}, {"title", definition.at("title")}, {"step_count", definition.at("steps").size()}, {"description", definition.value("description", std::string{})}, {"minutes", definition.value("minutes", 0)}});
    }
    return catalog;
}
Tutorial::Predicates Workflow::TutorialPredicates() {
    return {
        {"profile.loaded", [this] {const auto &layer=Current().document.at("layers")[0];return Json(layer.contains("grading") && layer.at("grading").contains("Na_m3") && layer.at("grading").at("Na_m3").value("law",std::string{})=="table");}},
        {"defect.law", [this] {const auto &defects=Current().document.at("layers")[0].value("defects",Json::array());return defects.empty()?Json(""):Json(defects[0].value("density_profile_m3",Json::object()).value("law",std::string{}));}},
        {"defect.profile_file", [this] {const auto &defects=Current().document.at("layers")[0].value("defects",Json::array());return Json(!defects.empty() && defects[0].value("density_profile_m3",Json::object()).value("law",std::string{})=="table");}},
        {"illumination.generation_file", [this] {return Json(Current().document.value("illumination",Json::object()).value("mode",std::string{})=="generation_file" && !Current().document.at("illumination").value("generation_m_m3s",Json::array()).empty());}},
        {"illumination.spectrum_points", [this] {return Json(Current().document.value("illumination",Json::object()).value("spectrum_nm_W_m2",Json::array()).size());}},
        {"absorption.file_points", [this] {return Json(Current().document.at("layers")[0].value("absorption",Json::object()).value("table_nm_m1",Json::array()).size());}},
        {"grading.law", [this] {return Json(Current().document.at("layers")[0].value("grading",Json::object()).value("Na_m3",Json::object()).value("law",std::string{}));}},
        {"device.temperature_K", [this] { return Current().document.at("temperature_K"); }},
        {"history.count", [this] {return Json(Current().history.size());}},
        {"export.file_count", [] {int n=0;for(const auto *name:{"scapslab-tutorial-profile.csv","scapslab-tutorial-profile.grd"})if(std::filesystem::exists(TutorialExportPath(name)))++n;return Json(n);}},
        {"illumination.filter_points", [this] {return Json(Current().document.value("illumination",Json::object()).value("front_filter_nm_fraction",Json::array()).size());}},
        {"defect.optical_points", [this] {const auto &defects=Current().document.at("layers")[0].value("defects",Json::array());return Json(defects.empty()?0:defects[0].value("optical",Json::object()).value("electron_table_nm_m2",Json::array()).size());}},
        {"layer.Eg_eV", [this] {return Current().document.at("layers")[0].at("material").at("Eg_eV");}},
        {"material.file_exists", [] {return Json(std::filesystem::exists(TutorialExportPath("scapslab-tutorial.material")));}},
        {"menu.File", [this] {return Json(menuOpened.contains("File"));}},{"menu.Edit", [this] {return Json(menuOpened.contains("Edit"));}},
        {"menu.View", [this] {return Json(menuOpened.contains("View"));}},{"menu.Simulate", [this] {return Json(menuOpened.contains("Simulate"));}},
        {"menu.Tools", [this] {return Json(menuOpened.contains("Tools"));}},{"menu.Tutorials", [this] {return Json(menuOpened.contains("Tutorials"));}},
        {"layer2.Eg_eV", [this] {const auto &l=Current().document.at("layers");return l.size()>1?l[1].at("material").at("Eg_eV"):Json(0);}},
        {"layer1.Na_m3", [this] {return Current().document.at("layers")[0].at("doping").at("Na_m3");}},
        {"defect.count", [this] {return Json(Current().document.at("layers")[0].value("defects",Json::array()).size());}},
        {"defect.Nt_m3", [this] {const auto &d=Current().document.at("layers")[0].value("defects",Json::array());return d.empty()?Json(0):d[0].at("Nt_m3");}},
        {"interface_defect.count", [this] {return Json(Current().document.value("interface_defects",Json::array()).size());}},
        {"interface_defect.Nt_m2", [this] {const auto &d=Current().document.value("interface_defects",Json::array());return d.empty()?Json(0):d[0].at("Nt_m2");}},
        {"contact.left_phi_eV", [this] {return Current().document.at("contacts").at("left").at("phi_m_eV");}},
        {"contact.left_flat_band", [this] {return Json(Current().document.at("contacts").at("left").value("flat_band",false));}},
        {"contact.right_Sn_m_s", [this] {return Current().document.at("contacts").at("right").at("Sn_m_s");}},
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
        menuOpened.clear();
        tutorial.Start(std::move(definition), mode, TutorialPredicates(), resume);
        tutorialAuto=autoRecalculate; tutorialBand=autoRecalculateBand;
        autoRecalculate=false; autoRecalculateBand=false;
    } else if (operation == "tutorial_stop") {
        if(tutorial.Active()) {tutorial.Stop(); autoRecalculate=tutorialAuto;autoRecalculateBand=tutorialBand;}
    } else if (operation == "tutorial_state" && request.contains("menu")) {
        menuOpened.insert(request.at("menu").get<std::string>()); // lets verify/demo mimic opening a menu
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
    while (workflow.tutorial.Automatic() || workflow.Pending()) {
        workflow.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return workflow.tutorial.Report();
}
}
