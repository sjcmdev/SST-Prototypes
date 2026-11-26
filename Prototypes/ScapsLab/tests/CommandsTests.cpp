// S8: typed commands (Commands/) with a mock store (no JobSystem), the JSON adapter table, palette names.
#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Commands/Commands.hpp"
#include <set>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
struct Mock final : ILabStore {
    std::vector<std::string> calls;
    Json last;
    bool busy = false, active = false, replacing = false, queuedEdit = false, cancelAny = true, stepOk = true;
    std::set<std::string> running{"d1"}, known{"d1", "d2"};
    std::vector<std::string> ops;
    bool Busy() const override {return busy;}
    bool AnyActive() const override {return active;}
    bool ReplacingProject() const override {return replacing;}
    bool DeviceRunning(const std::string &d) const override {return running.contains(d);}
    void RequireDevice(const std::string &d) const override {if(!known.contains(d)) throw std::invalid_argument("Unknown device_id: " + d);}
    std::vector<std::string> ActiveOps() const override {return ops;}
    bool EditQueued() const override {return queuedEdit;}
    void StartJob(Json r) override {calls.push_back("StartJob");last = std::move(r);}
    void StartSave(const Json &r) override {calls.push_back("StartSave");last = r;}
    bool CancelAll() override {calls.push_back("CancelAll");return cancelAny;}
    void CancelDevice(const std::string &d) override {calls.push_back("CancelDevice:" + d);}
    void DeferProjectLoad(const Json &r) override {calls.push_back("DeferProjectLoad");last = r;}
    void QueueEdit(const Json &r) override {calls.push_back("QueueEdit");last = r;}
    void PublishSnapshot() override {calls.push_back("PublishSnapshot");}
    void HistoryOp(const Json &r) override {calls.push_back("HistoryOp");last = r;}
    void DeviceOp(const Json &r) override {calls.push_back("DeviceOp");last = r;}
    void TutorialRequest(const Json &r) override {calls.push_back("TutorialRequest");last = r;}
    void SelectDevice(const std::string &d) override {calls.push_back("SelectDevice:" + d);}
    void SetAutoRecalculate(bool a, std::optional<bool> b) override {calls.push_back(std::string("Auto:") + (a ? "1" : "0") + (b ? (*b ? ":1" : ":0") : ":-"));}
    Result<void> Step(bool undo) override {calls.push_back(undo ? "Undo" : "Redo");if(!stepOk) return Cmd::Reject("nothing", "Nothing", "Nothing to undo");return {};}
    void RunAll() override {calls.push_back("RunAll");}
};
std::string Details(const Cmd::Out &o) {return o ? std::string{} : o.Error().technicalDetails;}
std::string Code(const Cmd::Out &o) {return o ? std::string{} : o.Error().code;}

// Every command that is "busy gate + StartJob(request)".
template<class T> void ExpectJob(Json args = Json::object()) {
    Mock idle;
    const auto cmd = T::With(args);
    ASSERT_TRUE(cmd.Execute(idle)) << T::info.op;
    ASSERT_EQ(idle.calls, std::vector<std::string>{"StartJob"}) << T::info.op;
    EXPECT_EQ(idle.last.at("operation"), T::info.op);
    for(const auto &[k, v] : args.items()) EXPECT_EQ(idle.last.at(k), v) << T::info.op << " keeps " << k;
    Mock busy;busy.busy = true;
    const auto out = cmd.Execute(busy);
    EXPECT_FALSE(out) << T::info.op;
    EXPECT_EQ(Code(out), "busy");
    EXPECT_EQ(Details(out), "A job is still active");
    EXPECT_TRUE(busy.calls.empty()) << T::info.op << " must not start while busy";
}
}

TEST(Commands, JobCommandsStartWhenIdleAndRejectWhenBusy) {
    ExpectJob<Cmd::Run>();
    ExpectJob<Cmd::RunBand>({{"voltage_V", 0.3}});
    ExpectJob<Cmd::RunCv>({{"start_V", -0.5}});
    ExpectJob<Cmd::RunCf>({{"frequency_Hz", 1e4}});
    ExpectJob<Cmd::RunQe>({{"points", 61}});
    ExpectJob<Cmd::RunBatch>();
    ExpectJob<Cmd::RetryBatchMember>({{"member_id", "m1"}});
    ExpectJob<Cmd::RecordBatch>();
    ExpectJob<Cmd::PreviewBatch>({{"spec", Json::object()}});
    ExpectJob<Cmd::QuickSweep>({{"points", 5}});
    ExpectJob<Cmd::BatchParameters>();
    ExpectJob<Cmd::SelectBatchMember>({{"index", 2}});
    ExpectJob<Cmd::SetBatchSettings>({{"spec", Json::object()}});
    ExpectJob<Cmd::Inspect>();
    ExpectJob<Cmd::EditLayer>({{"action", "add"}});
    ExpectJob<Cmd::LoadMaterial>({{"path", "a.material"}});
    ExpectJob<Cmd::SaveMaterial>({{"path", "a.material"}});
    ExpectJob<Cmd::SetProfile>({{"profile", Json::object()}});
    ExpectJob<Cmd::LoadProfile>({{"path", "a.grd"}});
    ExpectJob<Cmd::PreviewProfile>();
    ExpectJob<Cmd::ImportDef>({{"path", "a.def"}});
    ExpectJob<Cmd::NewProject>();
    ExpectJob<Cmd::ExportAc>({{"kind", "cv"}});
    ExpectJob<Cmd::ExportGr>({{"path", "gr.csv"}});
    ExpectJob<Cmd::ExportResults>({{"path", "r.csv"}});
    ExpectJob<Cmd::ExportBatch>({{"path", "b.csv"}});
    ExpectJob<Cmd::ExportProfile>({{"path", "p.csv"}});
    ExpectJob<Cmd::ExportDef>({{"path", "a.def"}});
    ExpectJob<Cmd::SendToPlotLab>({{"path", "a.plotlab"}});
}
TEST(Commands, RunPreviewKeepsStride) {
    Mock m;ASSERT_TRUE(Cmd::Run::Preview(4).Execute(m));
    EXPECT_EQ(m.last, (Json{{"operation", "run"}, {"preview", true}, {"stride", 4}}));
}
TEST(Commands, EditNormalizesToTypedWhileIdleOrBusyAndRefusesProjectReplacement) {
    const auto edit = Cmd::EditDocument::Patch({{"temperature_K", 301}});
    Mock idle;ASSERT_TRUE(edit.Execute(idle));EXPECT_EQ(idle.calls, std::vector<std::string>{"QueueEdit"});
    Mock busy;busy.busy = true;ASSERT_TRUE(edit.Execute(busy));EXPECT_EQ(busy.calls, std::vector<std::string>{"QueueEdit"});
    EXPECT_EQ(busy.last.at("patch").at("temperature_K"), 301);
    Mock replacing;replacing.busy = replacing.replacing = true;
    const auto out = edit.Execute(replacing);
    EXPECT_EQ(Details(out), "Project structure is changing; wait before editing");EXPECT_EQ(Code(out), "busy");EXPECT_TRUE(replacing.calls.empty());
    EXPECT_EQ(Cmd::EditDocument::Replace(Json::object(), "g1").ToJson().at("gesture_id"), "g1");
}
TEST(Commands, CancelWholeProjectOrOneDevice) {
    Mock m;m.cancelAny = false;
    auto out = Cmd::Cancel::With().Execute(m);EXPECT_EQ(Code(out), "not_running");EXPECT_EQ(Details(out), "No running job");
    Mock ok;ASSERT_TRUE(Cmd::Cancel::With().Execute(ok));EXPECT_EQ(ok.calls, std::vector<std::string>{"CancelAll"});
    Mock one;ASSERT_TRUE(Cmd::Cancel::Device("d1").Execute(one));EXPECT_EQ(one.calls, std::vector<std::string>{"CancelDevice:d1"});
    EXPECT_EQ(Code(Cmd::Cancel::Device("d2").Execute(one)), "not_running");
    EXPECT_THROW((void)Cmd::Cancel::Device("nope").Execute(one), std::invalid_argument);
}
TEST(Commands, SaveGoesToTheSaveLaneEvenWhenBusy) {
    Mock m;m.busy = true;
    ASSERT_TRUE(Cmd::SaveProject::Make("a.scapslab", true).Execute(m));EXPECT_EQ(m.calls, std::vector<std::string>{"StartSave"});
    EXPECT_EQ(m.last, (Json{{"operation", "project_save"}, {"path", "a.scapslab"}, {"include_results", true}}));
    EXPECT_THROW((void)Cmd::SaveProject::With().Execute(m), std::exception); // path is required
    Mock doc;ASSERT_TRUE(Cmd::SaveDocument::With({{"path", "x.scapslab"}}).Execute(doc));EXPECT_EQ(doc.calls, std::vector<std::string>{"StartSave"});
    Mock yaml;ASSERT_TRUE(Cmd::SaveDocument::With({{"path", "x.yaml"}}).Execute(yaml));EXPECT_EQ(yaml.calls, std::vector<std::string>{"StartJob"}); // device YAML is a job
    Mock yamlBusy;yamlBusy.busy = true;EXPECT_EQ(Code(Cmd::SaveDocument::With({{"path", "x.yaml"}}).Execute(yamlBusy)), "busy");
}
TEST(Commands, LoadWhileCalculatingCancelsAndDefers) {
    Mock idle;ASSERT_TRUE(Cmd::LoadProject::Make("p.scapslab").Execute(idle));EXPECT_EQ(idle.calls, std::vector<std::string>{"StartJob"});
    Mock calc;calc.busy = calc.active = true;calc.ops = {"run", "run_batch"};
    ASSERT_TRUE(Cmd::LoadProject::Make("p.scapslab").Execute(calc));EXPECT_EQ(calc.calls, std::vector<std::string>{"DeferProjectLoad"});
    Mock other;other.busy = other.active = true;other.ops = {"run", "export_results"};
    EXPECT_EQ(Code(Cmd::LoadProject::Make("p.scapslab").Execute(other)), "busy");EXPECT_TRUE(other.calls.empty());
    Mock doc;doc.busy = doc.active = true;doc.ops = {"cv"};
    ASSERT_TRUE(Cmd::LoadDocument::With({{"path", "d.yaml"}}).Execute(doc));EXPECT_EQ(doc.calls, std::vector<std::string>{"DeferProjectLoad"});
}
TEST(Commands, UndoRedoAreSynchronousAndOnlyBlockedByProjectReplacement) {
    Mock m;ASSERT_TRUE(Cmd::Undo{}.Execute(m));ASSERT_TRUE(Cmd::Redo{}.Execute(m));
    EXPECT_EQ(m.calls, (std::vector<std::string>{"Undo", "Redo"}));
    m.busy = m.active = true;m.ops = {"run", "run_batch"};m.calls.clear(); // calculations never block undo: the store cancels the active device's job
    ASSERT_TRUE(Cmd::Undo{}.Execute(m));ASSERT_TRUE(Cmd::Redo{}.Execute(m));EXPECT_EQ(m.calls, (std::vector<std::string>{"Undo", "Redo"}));
    for(const char *op : {"project_load", "load", "project_new"}) {
        Mock loading;loading.busy = loading.active = true;loading.ops = {op};
        EXPECT_EQ(Code(Cmd::Undo{}.Execute(loading)), "busy");EXPECT_EQ(Code(Cmd::Redo{}.Execute(loading)), "busy");EXPECT_TRUE(loading.calls.empty());
    }
    Mock none;none.stepOk = false;EXPECT_EQ(Code(Cmd::Undo{}.Execute(none)), "nothing");
}
TEST(Commands, SelectDeviceChecks) {
    Mock m;ASSERT_TRUE(Cmd::SelectDevice::Make("d2").Execute(m));EXPECT_EQ(m.calls, std::vector<std::string>{"SelectDevice:d2"});
    Mock calc;calc.ops = {"run", "cf"};ASSERT_TRUE(Cmd::SelectDevice::Make("d2").Execute(calc)); // calculations never block a switch
    Mock project;project.ops = {"project_load"};EXPECT_THROW((void)Cmd::SelectDevice::Make("d2").Execute(project), std::invalid_argument);
    Mock unknown;EXPECT_THROW((void)Cmd::SelectDevice::Make("zz").Execute(unknown), std::invalid_argument);
    Mock queued;queued.queuedEdit = true;EXPECT_THROW((void)Cmd::SelectDevice::Make("d2").Execute(queued), std::invalid_argument);
    EXPECT_TRUE(project.calls.empty() && unknown.calls.empty() && queued.calls.empty());
}
TEST(Commands, DeviceCommandsEditInPlaceAndRespectBusy) {
    const auto inPlace = []<class T>(T cmd) {
        Mock m;ASSERT_TRUE(cmd.Execute(m)) << T::info.op;EXPECT_EQ(m.calls, std::vector<std::string>{"DeviceOp"}) << T::info.op;EXPECT_EQ(m.last.at("operation"), T::info.op);
        Mock busy;busy.busy = true;EXPECT_EQ(Code(cmd.Execute(busy)), "busy");EXPECT_TRUE(busy.calls.empty());
    };
    inPlace(Cmd::AddDevice::With({{"name", "x"}}));
    inPlace(Cmd::DuplicateDevice::With({{"device_id", "d1"}}));
    inPlace(Cmd::RemoveDevice::With({{"device_id", "d1"}}));
    inPlace(Cmd::UpdateDevice::With({{"device_id", "d1"}, {"name", "n"}}));
    inPlace(Cmd::MoveDevice::With({{"device_id", "d1"}, {"index", 0}}));
    inPlace(Cmd::ListDevices::With());
    inPlace(Cmd::EditProject::With({{"name", "p"}}));
    inPlace(Cmd::SetActionSettings::With({{"actions", Json::object()}}));
    Mock file;ASSERT_TRUE(Cmd::AddDevice::With({{"path", "a.yaml"}}).Execute(file)); // from a file: only the read is a job
    EXPECT_EQ(file.calls, std::vector<std::string>{"StartJob"});EXPECT_EQ(file.last.at("operation"), "device_read");EXPECT_EQ(file.last.at("path"), "a.yaml");
}
TEST(Commands, HistoryCommandsRunWithoutTheBusyGate) {
    Mock m;m.busy = true;
    ASSERT_TRUE(Cmd::SelectRun::Make("d1", "r1").Execute(m));ASSERT_TRUE(Cmd::UpdateRun::With({{"run_id", "r1"}, {"pinned", true}}).Execute(m));ASSERT_TRUE(Cmd::ClearHistory::With({{"scope", "all"}}).Execute(m));
    EXPECT_EQ(m.calls, (std::vector<std::string>{"HistoryOp", "HistoryOp", "HistoryOp"}));
    EXPECT_EQ(m.last.at("operation"), "history_clear");
    Mock list;ASSERT_TRUE(Cmd::ListHistory::With().Execute(list));ASSERT_TRUE(Cmd::ListHistory::With({{"device_id", "d1"}}).Execute(list));
    EXPECT_EQ(list.calls, (std::vector<std::string>{"PublishSnapshot", "PublishSnapshot"}));
    EXPECT_THROW((void)Cmd::ListHistory::With({{"device_id", "zz"}}).Execute(list), std::invalid_argument);
}
TEST(Commands, AutoRecalculateAndRunAndTutorial) {
    Mock m;ASSERT_TRUE(Cmd::SetAutoRecalculate::Make(true).Execute(m));ASSERT_TRUE(Cmd::SetAutoRecalculate::Make(false, true).Execute(m));
    EXPECT_EQ(m.calls, (std::vector<std::string>{"Auto:1:-", "Auto:0:1"}));
    Mock run;ASSERT_TRUE(Cmd::RunAll::With().Execute(run));EXPECT_EQ(run.calls, std::vector<std::string>{"RunAll"});
    Mock busy;busy.busy = true;EXPECT_EQ(Code(Cmd::RunAll::With().Execute(busy)), "busy");
    Mock t;t.busy = true;ASSERT_TRUE(Cmd::TutorialStart::With({{"tutorial_id", "x"}}).Execute(t));EXPECT_EQ(t.calls, std::vector<std::string>{"TutorialRequest"});EXPECT_EQ(t.last.at("operation"), "tutorial_start");
}
TEST(Commands, JsonRoundTripKeepsTheRequestExactly) { // the request is what lands in d.actions and in saved projects
    const Json request = {{"operation", "qe"}, {"start_nm", 300.}, {"stop_nm", 1500.}, {"points", 61}, {"probe_mode", "constant_flux"}};
    EXPECT_EQ(Cmd::RunQe::FromJson(request).ToJson(), request);
    const Json preview = {{"operation", "run"}, {"preview", true}, {"stride", 4.0}};
    EXPECT_EQ(Cmd::Run::FromJson(preview).ToJson().dump(), preview.dump());
}

// Public Dispatch contract: legacy operations plus the S2 typed Actions adapters.
static const char *const kPublicOps[] = {"library","load_material","save_material","set_profile","load_profile","preview_profile","export_profile","tutorial_start","tutorial_stop","tutorial_state","tutorial_list","load","save","edit","layer","run","cv","cf","qe","dlcp","dlts","laplace_dlts","capture_dlts","optical_dlts","export_dlcp","export_ac","export_gr","export_results","action_settings","inspect","cancel","undo","redo","auto_recalculate","band",
    "history_list","history_update","history_select","history_clear","quick_sweep","project_new","project_load","project_save","project_edit","device_list","device_add","device_duplicate","device_remove","device_select","device_update","device_move","run_all","batch_parameters","batch_select","batch_settings","preview_batch","run_batch","retry_batch","record_batch","export_batch","send_to_plotlab","import_def","export_def","single_shot","run_settings","capture_live","resume_actions","recalculate_live","live_editing","select_bias","recalculate_stored_bias","parameter_bounds"};

TEST(CommandAdapter, EveryPublicOperationMapsToATypedCommandOrAnExplicitLegacyHandler) {
    std::map<std::string, const Cmd::Info *> byOp;
    for(const auto &e : Cmd::Table()) EXPECT_TRUE(byOp.emplace(e.info->op, e.info).second) << "duplicate op " << e.info->op;
    for(const auto *op : kPublicOps) {
        const auto it = byOp.find(op);
        ASSERT_NE(it, byOp.end()) << "legacy op without a command: " << op;
        EXPECT_EQ(it->second->legacy, std::string(op).starts_with("tutorial_")) << op; // only the tutorial player stays a JSON handler
    }
    EXPECT_EQ(byOp.size(), std::size(kPublicOps)) << "a typed command without a declared public op name";
    Workflow w(CreateRef<JobSystem>(nullptr, 1));
    for(const auto *op : kPublicOps) EXPECT_TRUE(w.commands->HasCommand(CommandID{std::string("scapslab.") + op})) << op;
}
TEST(CommandAdapter, DispatchReachesTheTypedCommandsAndKeepsTheReplyShape) {
    Workflow w(CreateRef<JobSystem>(nullptr, 1));
    const auto unknown = w.Dispatch({{"operation", "no_such_op"}});
    EXPECT_FALSE(unknown.at("ok").get<bool>());EXPECT_EQ(unknown.at("error").at("code"), "command_rejected");
    const auto missing = w.Dispatch(Json::object());
    EXPECT_EQ(missing.at("error").at("code"), "invalid_request");
    const auto notRunning = w.Dispatch({{"operation", "cancel"}});
    EXPECT_EQ(notRunning.at("error").at("code"), "command_rejected");EXPECT_EQ(notRunning.at("error").at("message"), "No running job");
    const auto typed = w.Exec(Cmd::Cancel::With()); // the typed API gives the same reply
    EXPECT_EQ(typed, notRunning);
    EXPECT_EQ(w.Dispatch({{"operation", "device_select"}, {"device_id", "zz"}}).at("error").at("message"), "Unknown device_id: zz");
    EXPECT_EQ(w.Exec(Cmd::SelectDevice::Make("zz")).at("error").at("message"), "Unknown device_id: zz");
    EXPECT_TRUE(w.Exec(Cmd::EditProject::With({{"name", "Typed"}})).at("ok").get<bool>());EXPECT_EQ(w.project.name, "Typed");
}
TEST(CommandAdapter, TypedRunProducesTheSameResultAsJson) {
    Workflow a(CreateRef<JobSystem>(nullptr, 1)), b(CreateRef<JobSystem>(nullptr, 1));
    for(auto *w : {&a, &b}) {w->lightResponse = true;w->Current().SetDocument( ReadProject(SCAPSLAB_PROJECT));}
    ASSERT_TRUE(a.Exec(Cmd::Run::Make()).at("ok").get<bool>());ASSERT_TRUE(b.Dispatch({{"operation", "run"}}).at("ok").get<bool>());
    for(auto *w : {&a, &b}) {for(int i = 0; i < 20000 && w->Pending(); ++i) {w->OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_FALSE(w->Pending());}
    ASSERT_FALSE(a.Current().result.empty());
    EXPECT_EQ(a.Current().result.json().size(), b.Current().result.json().size());EXPECT_EQ(a.Current().result.value("valid", false), b.Current().result.value("valid", false));EXPECT_EQ(a.Current().history.size(), b.Current().history.size());
    EXPECT_EQ(a.Current().actions, b.Current().actions);
}
TEST(CommandPalette, NamesAreUniqueAndMenuPathsAreSane) {
    std::set<std::string> names, ids;
    for(const auto &e : Cmd::Table()) {
        EXPECT_TRUE(names.insert(e.info->name).second) << "duplicate palette name: " << e.info->name;
        EXPECT_TRUE(ids.insert(e.info->op).second);
        EXPECT_NE(std::string(e.info->name), "") << e.info->op;
        if(*e.info->menu) EXPECT_NE(std::string(e.info->menu).find('/'), std::string::npos) << e.info->op;
        if(*e.info->shortcut) EXPECT_TRUE(*e.info->menu) << "shortcut without menu: " << e.info->op;
    }
    Workflow w(CreateRef<JobSystem>(nullptr, 1)); // what the registry actually holds (also catches a clash with commands registered elsewhere)
    std::set<std::string> registered;
    for(const auto &meta : w.commands->ListCommands()) EXPECT_TRUE(registered.insert(meta.name).second) << "duplicate registered name: " << meta.name;
    const auto undo = w.commands->GetMeta(CommandID{"scapslab.undo"});
    ASSERT_TRUE(undo);EXPECT_EQ(undo.Value().menuPath, "Edit/Undo");EXPECT_EQ(undo.Value().shortcut, "Ctrl+Z");
    const auto run = w.commands->GetMeta(CommandID{"scapslab.run"});
    ASSERT_TRUE(run);EXPECT_FALSE(HasFlag(run.Value().flags, CommandFlags::HiddenFromPalette));
    const auto band = w.commands->GetMeta(CommandID{"scapslab.band"});
    ASSERT_TRUE(band);EXPECT_TRUE(HasFlag(band.Value().flags, CommandFlags::HiddenFromPalette)); // needs a voltage
}
