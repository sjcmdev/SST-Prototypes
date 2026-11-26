#pragma once
// S8: typed ScapsLab commands. One struct per user-visible action: `info` (op = legacy JSON "operation", palette name, menu path, shortcut),
// typed factories (Make/With), FromJson/ToJson (the JSON adapter used by Workflow::Dispatch and the Python/headless contract) and
// Execute(ILabStore&), which only decides (reject / queue / start) and asks the store to act. Nothing here touches a JobSystem.
// The command carries its request as Json (`args`): the job and `d.actions` keep reading the exact legacy request, so saved projects do not change.
#include "Commands/Store.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include <functional>
#include "Library/Session.hpp"
namespace SST::ScapsLab::Cmd {
using Out = Result<void>;
struct Info {
    const char *op, *name, *description, *menu = "", *shortcut = "";
    bool hidden = false;      // needs arguments: not offered in the command palette
    bool menuDefault = false; // runs with no arguments from a menu/palette
    bool legacy = false;      // explicit legacy JSON handler (no typed arguments)
};
struct LibraryOperation {
    static constexpr Info info{"library","Asset library","Inspect, edit, import or apply typed scientific assets","","",true};
    std::shared_ptr<const Library::Request> request;
    static LibraryOperation Make(Library::Request value) {return {std::make_shared<const Library::Request>(std::move(value))};}
    static LibraryOperation FromJson(const Json&) {throw std::invalid_argument("Convert library inputs in the Python binding");}
    Out Execute(ILabStore& s) const {
        if(!request)return StructuredError{ErrorCategory::Validation,Severity::Error,"Asset request required","Missing typed asset request","Supply a typed request","ScapsLab","invalid_asset"};
        try{s.LibraryTyped(request);return {};}
        catch(const Assets::Error& e){return StructuredError{ErrorCategory::Validation,Severity::Error,"Asset operation rejected",e.what(),"Review the asset operation","ScapsLab",Assets::CodeName(e.code)};}
    }
};
Out Reject(const char *code, std::string title, std::string details, std::string hint = "Wait or cancel");
Out Job(ILabStore &store, Json request); // busy gate + job on the active device
Json Merge(Json args, const char *op);   // args + "operation"
template<class D> struct Req {           // JSON-backed request: exact round trip of whatever the caller sent
    Json args = Json::object();
    static D With(Json a = Json::object()) {D d;d.args = a.is_object() ? std::move(a) : Json::object();d.args.erase("operation");return d;}
    static D FromJson(const Json &j) {return With(j);}
    Json ToJson() const {return Merge(args, D::info.op);}
    Out Execute(ILabStore &s) const {return Job(s, ToJson());}
};
#define SL_JOB(T, ...) struct T : Req<T> {static constexpr Info info{__VA_ARGS__};}
// Calculations (Run.cpp)
struct BoundsFile : Req<BoundsFile> {
    static constexpr Info info{"parameter_bounds","Parameter bounds","Inspect or reload parameter bounds","","",true};
    BoundsFileAction action=BoundsFileAction::Inspect;
    static BoundsFile Make(BoundsFileAction action){BoundsFile c;c.action=action;return c;}
    Out Execute(ILabStore& s) const {
        auto selected=action;
        if(args.contains("action")) {
            const auto value=args.at("action").get<std::string>();
            if(value!="inspect" && value!="reload" && value!="reset")return Reject("invalid_action","Invalid bounds action","Use inspect, reload or reset");
            selected=value=="reload"?BoundsFileAction::Reload:value=="reset"?BoundsFileAction::Reset:BoundsFileAction::Inspect;
        }
        s.BoundsFileTyped(selected);return {};
    }
};
struct Run : Req<Run> {
    static constexpr Info info{"run", "Calculate I-V", "Calculate the J-V curve of the active device", "Simulate/Calculate I-V", "", false, true};
    static Run Make() {return {};}
    static Run Preview(int stride) {return With({{"preview", true}, {"stride", stride}});}
};
struct RunBand : Req<RunBand> {
    static constexpr Info info{"band", "Calculate band diagram", "Band diagram at one bias", "", "", true};
    static RunBand Make(double voltage_V) {return With({{"voltage_V", voltage_V}});}
};
SL_JOB(RunCv, "cv", "Calculate C-V", "Capacitance-voltage sweep", "", "", true);
SL_JOB(RunCf, "cf", "Calculate C-f", "Capacitance-frequency sweep", "", "", true);
struct RunDlcp : Req<RunDlcp> {
    static constexpr Info info{"dlcp","Calculate DLCP","Large-signal drive-level capacitance profile","","",true};
    std::optional<RunAction> action;
    static RunDlcp Make(RunAction action){RunDlcp c;c.action=std::move(action);return c;}
    static RunDlcp FromJson(const Json& request){RunDlcp c;if(request.size()>1)c.action=ReadRunAction(ActionKind::Dlcp,request);return c;}
    Out Execute(ILabStore& s) const {if(s.Busy())return Reject("busy","Busy","A job is still active");s.RunDlcpTyped(action);return {};}
};
template<ActionKind Kind> struct RunTransient {
    static constexpr Info info{Kind==ActionKind::Dlts?"dlts":Kind==ActionKind::Laplace?"laplace_dlts":Kind==ActionKind::Capture?"capture_dlts":"optical_dlts",Kind==ActionKind::Dlts?"Classic DLTS":Kind==ActionKind::Laplace?"Laplace DLTS":Kind==ActionKind::Capture?"Capture / depth / DDLTS":"MCTS / ODLTS","Run a virtual transient experiment","","",true};
    RunAction action=RunPlan{}.actions[int(Kind)];
    static RunTransient Make(RunAction a){RunTransient c;c.action=std::move(a);return c;}
    static RunTransient FromJson(const Json& request){return Make(ReadRunAction(Kind,request));}
    Out Execute(ILabStore& s) const {if(s.Busy())return Reject("busy","Busy","A job is still active");auto plan=std::make_shared<RunPlan>();for(auto& a:plan->actions)a.include=false;plan->actions[int(Kind)]=action;plan->actions[int(Kind)].include=true;s.SingleShotTyped(std::move(plan));return {};}
};
using RunDlts=RunTransient<ActionKind::Dlts>;using RunLaplace=RunTransient<ActionKind::Laplace>;using RunCapture=RunTransient<ActionKind::Capture>;using RunOptical=RunTransient<ActionKind::Optical>;
SL_JOB(RunQe, "qe", "Calculate QE", "External quantum efficiency", "", "", true);
struct SingleShot : Req<SingleShot> {
    static constexpr Info info{"single_shot","Single shot","Execute and store included Run Actions","Simulate/Single shot","",false,true};
    std::shared_ptr<const RunPlan> plan;
    static SingleShot Make(std::shared_ptr<const RunPlan> p={}) {SingleShot c;c.plan=std::move(p);return c;}
    Out Execute(ILabStore& s) const {if(s.Busy())return Reject("busy","Busy","A job is still active");s.SingleShotTyped(plan?plan:args.contains("actions")?std::make_shared<const RunPlan>(ReadRunPlan(args.at("actions"))):nullptr);return {};}
};
struct SetRunPlan : Req<SetRunPlan> {
    static constexpr Info info{"run_settings","Run settings","Edit Run Actions","","",true};
    std::shared_ptr<const RunPlan> plan;
    std::string gesture;
    static SetRunPlan Make(std::shared_ptr<const RunPlan> p,std::string gesture={}) {SetRunPlan c;c.plan=std::move(p);c.gesture=std::move(gesture);return c;}
    Out Execute(ILabStore& s) const {s.SetRunPlanTyped(plan?plan:std::make_shared<const RunPlan>(ReadRunPlan(args.at("actions"))),gesture);return {};}
};
struct CaptureLive : Req<CaptureLive> {
    static constexpr Info info{"capture_live","Capture live","Store the current exact live result","","",true};
    ActionKind kind=ActionKind::Iv;
    static CaptureLive Make(ActionKind k) {CaptureLive c;c.kind=k;return c;}
    Out Execute(ILabStore& s) const {auto k=kind;if(args.contains("kind")){bool found=false;for(int i=0;i<kActionCount;++i)if(args.at("kind")==ActionId(ActionKind(i))){k=ActionKind(i);found=true;}if(!found)return Reject("invalid_kind","Unknown Action","Capture requires band, iv, cv, cf or qe");}s.CaptureLiveTyped(k);return {};}
};
struct ResumeRun : Req<ResumeRun> {
    static constexpr Info info{"resume_actions","Resume Actions","Continue after the completed Action","","",true};
    Out Execute(ILabStore& s) const {s.ResumeActions();return {};}
};
struct RecalculateLive : Req<RecalculateLive> {
    static constexpr Info info{"recalculate_live","Recalculate Live","Update one Live technique without storing","","",true};
    ActionKind kind=ActionKind::Iv;
    static RecalculateLive Make(ActionKind k) {RecalculateLive c;c.kind=k;return c;}
    Out Execute(ILabStore& s) const {
        if(s.Busy())return Reject("busy","Busy","A job is still active");
        auto k=kind;
        if(args.contains("kind")){bool found=false;for(int i=0;i<kActionCount;++i)if(args.at("kind")==ActionId(ActionKind(i))){k=ActionKind(i);found=true;}if(!found)return Reject("invalid_kind","Unknown Action","Expected band, iv, cv, cf or qe");}
        s.RecalculateLive(k);return {};
    }
};
struct SelectBias : Req<SelectBias> {
    static constexpr Info info{"select_bias","Select bias","Update the working point of spatial plots","","",true};
    int index=0;
    static SelectBias Make(int i) {SelectBias c;c.index=i;return c;}
    Out Execute(ILabStore& s) const {if(args.contains("index") && !args.at("index").is_number_integer())return Reject("invalid_index","Invalid bias","Bias index must be an integer");s.SelectBias(args.contains("index")?args.at("index").get<int>():index);return {};}
};
struct RecalculateStoredBias : Req<RecalculateStoredBias> {
    static constexpr Info info{"recalculate_stored_bias","Recalculate this bias","Load missing stored profiles without storing","","",true};
    Out Execute(ILabStore& s) const {if(s.Busy())return Reject("busy","Busy","A job is still active");s.RecalculateStoredBias();return {};}
};
struct RunBatch : Req<RunBatch> {
    static constexpr Info info{"run_batch","Run batch","Run the batch of the active device","","",false,true};
    std::shared_ptr<const BatchDraft> batch;std::shared_ptr<const RecorderDraft> recorder;std::shared_ptr<const RunPlan> plan;
    static RunBatch Make(std::shared_ptr<const BatchDraft> b,std::shared_ptr<const RecorderDraft> r,std::shared_ptr<const RunPlan> p){RunBatch c;c.batch=std::move(b);c.recorder=std::move(r);c.plan=std::move(p);return c;}
    Out Execute(ILabStore& s) const {if(!batch && !recorder && !plan)return Job(s,ToJson());if(s.Busy())return Reject("busy","Busy","A job is still active");s.RunBatchTyped(batch,recorder,plan);return {};}
};
SL_JOB(RetryBatchMember, "retry_batch", "Retry batch member", "Recalculate one batch member", "", "", true);
SL_JOB(RecordBatch, "record_batch", "Re-extract recorder columns", "Rebuild the recorder table of the finished batch");
SL_JOB(PreviewBatch, "preview_batch", "Preview batch", "Member list of a batch spec", "", "", true);
struct QuickSweep : Req<QuickSweep> {
    static constexpr Info info{"quick_sweep","Quick batch","Run one parameter sweep","","",true};
    std::shared_ptr<const BatchDraft> batch;
    std::shared_ptr<const RecorderDraft> recorder;
    std::shared_ptr<const ProjectDraft> snapshot;
    std::shared_ptr<const RunPlan> plan;
    static QuickSweep Make(std::shared_ptr<const BatchDraft> batch,std::shared_ptr<const RecorderDraft> recorder,std::shared_ptr<const ProjectDraft> snapshot,std::shared_ptr<const RunPlan> plan={}){QuickSweep c;c.plan=std::move(plan);c.batch=std::move(batch);c.recorder=std::move(recorder);c.snapshot=std::move(snapshot);return c;}
    Out Execute(ILabStore& s) const {if(!batch)return Job(s,ToJson());if(s.Busy())return Reject("busy","Busy","A job is still active");s.QuickSweepTyped(batch,recorder,snapshot,plan);return {};}
};
SL_JOB(BatchParameters, "batch_parameters", "List batch parameters", "Sweepable parameters of the active device");
SL_JOB(SelectBatchMember, "batch_select", "Select batch member", "Show one batch member", "", "", true);
struct SetBatchSettings : Req<SetBatchSettings> {
    static constexpr Info info{"batch_settings","Batch settings","Store the batch spec and recorder columns","","",true};
    std::shared_ptr<const BatchDraft> batch;std::shared_ptr<const RecorderDraft> recorder;
    static SetBatchSettings Make(std::shared_ptr<const BatchDraft> batch,std::shared_ptr<const RecorderDraft> recorder={}){SetBatchSettings c;c.batch=std::move(batch);c.recorder=std::move(recorder);return c;}
    Out Execute(ILabStore& s) const {if(batch || recorder){if(s.Busy())return Reject("busy","Busy","A job is still active");s.BatchTyped(batch,recorder);return {};}return Job(s,ToJson());}
};
SL_JOB(Inspect, "inspect", "Inspect device model", "Validate the active device and report its model");
struct RunAll : Req<RunAll> {
    static constexpr Info info{"run_all", "Calculate all compared devices", "One run per device checked for compare", "", "", false, true};
    Out Execute(ILabStore &s) const;
};
struct Cancel : Req<Cancel> {
    static constexpr Info info{"cancel", "Stop", "Cancel running jobs", "Simulate/Stop", "", false, true};
    static Cancel Device(const std::string &id) {return With({{"device_id", id}});}
    Out Execute(ILabStore &s) const;
};
struct SetAutoRecalculate : Req<SetAutoRecalculate> {
    static constexpr Info info{"auto_recalculate", "Auto-recalculate", "Recalculate when the document changes", "", "", true};
    static SetAutoRecalculate Make(bool enabled, std::optional<bool> band = {}) {auto c = With({{"enabled", enabled}});if(band)c.args["band_enabled"] = *band;return c;}
    Out Execute(ILabStore &s) const;
};
// Document and history (Edit.cpp)
struct EditDocument : Req<EditDocument> {
    std::shared_ptr<const ProjectDraft> draft;
    std::string gesture;
    static EditDocument Make(std::shared_ptr<const ProjectDraft> value,std::string gesture={}) {EditDocument c;c.draft=std::move(value);c.gesture=std::move(gesture);return c;}
    static constexpr Info info{"edit", "Edit device", "Replace the device document or apply a patch", "", "", true};
    static EditDocument Replace(Json document, std::string gesture = {}) {auto c = With({{"document", std::move(document)}});if(!gesture.empty())c.args["gesture_id"] = std::move(gesture);return c;}
    static EditDocument Patch(Json patch) {return With({{"patch", std::move(patch)}});}
    Out Execute(ILabStore &s) const;
};
SL_JOB(EditLayer, "layer", "Edit layer", "Add, remove or move a layer", "", "", true);
SL_JOB(LoadMaterial, "load_material", "Load layer material", "Read a SCAPS .material into a layer", "", "", true);
SL_JOB(SaveMaterial, "save_material", "Save layer material", "Write a layer as SCAPS .material", "", "", true);
SL_JOB(SetProfile, "set_profile", "Set profile", "Store a grading / defect profile", "", "", true);
SL_JOB(LoadProfile, "load_profile", "Load profile file", "Read a profile from a file", "", "", true);
SL_JOB(PreviewProfile, "preview_profile", "Preview profile", "Profile samples over the layer", "", "", true);
SL_JOB(ImportDef, "import_def", "Import SCAPS definition", "Replace the active device from a SCAPS .def", "", "", true);
struct Undo : Req<Undo> {
    static constexpr Info info{"undo", "Undo", "Undo the last edit", "Edit/Undo", "Ctrl+Z", false, true};
    Out Execute(ILabStore &s) const;
};
struct Redo : Req<Redo> {
    static constexpr Info info{"redo", "Redo", "Redo the last undone edit", "Edit/Redo", "Ctrl+Y", false, true};
    Out Execute(ILabStore &s) const;
};
struct ListHistory : Req<ListHistory> {
    static constexpr Info info{"history_list", "List history", "Run history of a device", "", "", true};
    Out Execute(ILabStore &s) const;
};
struct SelectRun : Req<SelectRun> {
    static constexpr Info info{"history_select", "Select run", "Show a stored run", "", "", true};
    static SelectRun Make(const std::string &device, const std::string &run) {return With({{"device_id", device}, {"run_id", run}});}
    Out Execute(ILabStore &s) const {s.HistoryOp(ToJson());return {};}
};
struct UpdateRun : Req<UpdateRun> {
    static constexpr Info info{"history_update", "Update run", "Pin, rename, hide or recolour a stored run", "", "", true};
    Out Execute(ILabStore &s) const {s.HistoryOp(ToJson());return {};}
};
struct ClearHistory : Req<ClearHistory> {
    static constexpr Info info{"history_clear", "Clear history", "Delete stored runs", "", "", true};
    Out Execute(ILabStore &s) const {s.HistoryOp(ToJson());return {};}
};
// Devices and project metadata (in place on the UI thread)
#define SL_DEVICE(T, ...) struct T : Req<T> {static constexpr Info info{__VA_ARGS__}; Out Execute(ILabStore &s) const;}
SL_DEVICE(AddDevice, "device_add", "New device", "Add a device (from a file when path is given)", "", "", false, true);
SL_DEVICE(DuplicateDevice, "device_duplicate", "Duplicate device", "Copy a device", "", "", true);
SL_DEVICE(RemoveDevice, "device_remove", "Delete device", "Remove a device", "", "", true);
SL_DEVICE(UpdateDevice, "device_update", "Update device", "Rename, recolour or toggle compare", "", "", true);
SL_DEVICE(MoveDevice, "device_move", "Move device", "Reorder devices", "", "", true);
SL_DEVICE(ListDevices, "device_list", "List devices", "Devices of the project", "", "", true);
SL_DEVICE(EditProject, "project_edit", "Edit project", "Rename the project or change its ui settings", "", "", true);
SL_DEVICE(SetActionSettings, "action_settings", "Action settings", "Store the run settings of a device", "", "", true);
struct SelectDevice : Req<SelectDevice> {
    static constexpr Info info{"device_select", "Select device", "Make a device the active one", "", "", true};
    static SelectDevice Make(const std::string &id) {return With({{"device_id", id}});}
    Out Execute(ILabStore &s) const;
};
// Project and files (Project.cpp). Save/load/export implementations stay where they are; these wrap them.
SL_JOB(NewProject, "project_new", "New project", "Replace the project with an empty one", "", "", false, true);
struct LoadProject : Req<LoadProject> {
    static constexpr Info info{"project_load", "Open project file", "Open a .scapslab project", "", "", true};
    static LoadProject Make(const std::string &path) {return With({{"path", path}});}
    Out Execute(ILabStore &s) const;
};
struct LoadDocument : Req<LoadDocument> {
    static constexpr Info info{"load", "Open device or project file", "Open a device YAML or a .scapslab project", "", "", true};
    Out Execute(ILabStore &s) const;
};
struct SaveProject : Req<SaveProject> {
    static constexpr Info info{"project_save", "Save project file", "Save the project (own save lane)", "", "", true};
    static SaveProject Make(const std::string &path, bool includeResults) {return With({{"path", path}, {"include_results", includeResults}});}
    Out Execute(ILabStore &s) const;
};
struct SaveDocument : Req<SaveDocument> {
    static constexpr Info info{"save", "Save device or project file", "Save the device YAML or a .scapslab project", "", "", true};
    Out Execute(ILabStore &s) const;
};
struct ExportDlcp : Req<ExportDlcp> {
    static constexpr Info info{"export_dlcp","Export DLCP CSV","Write DLCP and amplitude samples with units","","",true};
    std::shared_ptr<const AcResult> result;
    static ExportDlcp Make(std::shared_ptr<const AcResult> result,const std::string& path){auto c=With({{"path",path}});c.result=std::move(result);return c;}
    Out Execute(ILabStore& s) const {if(s.Busy())return Reject("busy","Busy","Wait before exporting");s.ExportDlcpTyped(result,args.at("path").get<std::string>());return {};}
};
SL_JOB(ExportAc, "export_ac", "Export C-V / C-f CSV", "Write an AC result as CSV", "", "", true);
SL_JOB(ExportGr, "export_gr", "Export generation-recombination", "Write G/R results", "", "", true);
SL_JOB(ExportResults, "export_results", "Export results", "Write runs as CSV/JSON", "", "", true);
SL_JOB(ExportBatch, "export_batch", "Export batch CSV", "Write the recorder table", "", "", true);
SL_JOB(ExportProfile, "export_profile", "Export profile", "Write a profile as text", "", "", true);
SL_JOB(ExportDef, "export_def", "Export SCAPS definition", "Write the device as SCAPS .def", "", "", true);
SL_JOB(SendToPlotLab, "send_to_plotlab", "Send batch to PlotLab", "Write a PlotLab workbook", "", "", true);
// Tutorials: JSON-driven by design (the tutorial player replays recorded requests): explicit legacy handlers.
#define SL_LEGACY(T, ...) struct T : Req<T> {static constexpr Info info{__VA_ARGS__}; Out Execute(ILabStore &s) const {s.TutorialRequest(ToJson());return {};}}
SL_LEGACY(TutorialStart, "tutorial_start", "Start tutorial", "Tutorial request (legacy JSON handler)", "", "", true, false, true);
SL_LEGACY(TutorialStop, "tutorial_stop", "Stop tutorial", "Stop the running tutorial", "Tutorials/Stop tutorial", "", false, true, true);
SL_LEGACY(TutorialState, "tutorial_state", "Tutorial state", "Tutorial request (legacy JSON handler)", "", "", true, false, true);
SL_LEGACY(TutorialList, "tutorial_list", "List tutorials", "Tutorial request (legacy JSON handler)", "", "", true, false, true);
#undef SL_JOB
#undef SL_DEVICE
#undef SL_LEGACY
struct Entry {const Info *info;};
const std::vector<Entry> &Table();                                      // every typed command, in registration order
Result<void> RegisterAll(CommandRegistry &registry, ILabStore &store);  // id "scapslab.<op>"; factories read a typed T ("args") or the legacy JSON ("request")
}
