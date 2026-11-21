#pragma once
#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notifier.hpp"
#include "Events.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <nlohmann/json.hpp>
#include "Core/Undo/UndoStack.hpp"
#include "Core/Presentation/Tutorial.hpp"
#include "Domain/Results.hpp"
#include "Core/Presentation/TutorialOverlay.hpp"
#include <chrono>
#include <atomic>
#include <array>
#include <deque>
#include <set>
#include <map>
#include <scaps_core/device_project.hpp>
namespace SST::ScapsLab {
using Json = nlohmann::json;
Json ReadProject(const std::string &path);
Json NewDeviceDocument(); // template device with SCAPS-like flat-band contacts (engine default and example decks untouched)
Json NormalizeDocument(Json document);
scaps_core::Profile ProfileFromJson(const Json &spec, bool logarithmic=false);
Json ProfileFromFile(const std::string &path, bool logarithmic=false);
Json ProfileSamples(const Json &spec, double thickness_m, const Json &composition=Json::object());
Json ResolveSharedProfile(Json spec,const Json &document,const std::string &layerId,const std::string &parameter);
void ExportProfile(const Json &samples, const std::string &path);
scaps_core::DeviceProject Model(const Json &document);
void WriteProject(const Json &document, const std::string &path);
Json AM15Spectrum();
Json SelfContained(Json document);
Json ResultRuns(const Json &workspace);
Json ExportResults(const Json &workspace, const Json &request);
Json NewDefect(bool sheet = false);
const Json &PointAtBias(const Json &sweep,const Json &band,int selected);
void ChangeSheetDistribution(Json &defect,const std::string &shape,const std::string &convention);
Json BatchDefaults();
Json RecorderDefaults();
Json BatchParameters(const Json &document);
std::string ParameterLabel(const std::string &path,const std::string &unit,const Json &value,const Json &before=nullptr);
std::string RunLabel(const Json &run);
std::string DiffLabel(const Json &previous,const Json &document);
Json BatchPreview(const Json &document,const Json &spec);
Json QuickSweep(const Json &document,const Json &request);
void TrimHistory(Json &history,int retention);
void StoreRun(Json &history,const std::string &kind,const Json &result,const Json &document,const Json &request,int retention,const std::string &group="");
void CompactHistory(Json &workspace);
std::size_t PrivateBytes();
std::size_t JsonValues(const Json &v);
class Operation final : public IJob {
public:
    Json document, request, output, acResults, workspace, batchResult;
    std::vector<std::array<Ref<const Json>,5>> shared; // save jobs: history,result,band,ac,batch per device (immutable, shared with the UI)
    Json batchSpec=BatchDefaults(), recorderColumns=RecorderDefaults();
    Operation(Json d, Json r) : document(std::move(d)), request(std::move(r)) {}
    std::string GetName() const override { return "ScapsLab " + request.at("operation").get<std::string>(); }
    std::string GetType() const override { return "scapslab.operation"; }
    void Execute(JobContext &context) override;
};
bool BatchOperation(Operation &operation,JobContext &context);
// Copy-on-write JSON: a save job shares the pointer (O(1) snapshot); writers use mut()/assignment, which never touch a shared value.
class Cow {
    Ref<Json> p;
public:
    Cow(Json v=Json::object()):p(CreateRef<Json>(std::move(v))) {}
    Cow &operator=(Json v) {p=CreateRef<Json>(std::move(v));return *this;}
    operator const Json &() const {return *p;}
    const Json &operator*() const {return *p;}
    const Json *operator->() const {return p.get();}
    Json &mut() {if(p.use_count()>1)p=CreateRef<Json>(*p);return *p;}
    Ref<const Json> share() const {return p;}
    void adopt(Ref<const Json> v) {p=std::const_pointer_cast<Json>(std::move(v));} // mut() copies while shared, so this stays immutable
    template<class K> bool contains(K &&k) const {return p->contains(std::forward<K>(k));}
    template<class K> const Json &at(K &&k) const {return p->at(std::forward<K>(k));}
    template<class K> const Json &operator[](K &&k) const {return (*p)[std::forward<K>(k)];}
    bool empty() const {return p->empty();}
    std::size_t size() const {return p->size();}
    auto begin() const {return p->begin();} auto end() const {return p->end();}
    auto rbegin() const {return p->rbegin();} auto rend() const {return p->rend();}
    const Json &back() const {return p->back();} const Json &front() const {return p->front();}
    template<class... A> auto value(A &&...a) const {return p->value(std::forward<A>(a)...);}
    std::string dump(int i=-1) const {return p->dump(i);}
    bool operator==(const Json &o) const {return *p==o;}
    bool operator==(const Cow &o) const {return *p==*o.p;}
};
struct DeviceState {
    std::string id, name, description, status = "no results";
    Json colour = Json::array({0.25, 0.65, 0.95, 1.0});
    Json document;
    Cow bandResult{Json::object()}, result{Json::object()}, acResults{Json::object()};
    // Typed views of the Json above (S2): same data, built once per result object; the Json stays the saved form.
    Memo<IvResult,IvFromJson> ivTyped, bandTyped;
    mutable std::map<std::string,Memo<AcResult,AcFromJson>> acTyped; // per kind cv/cf/qe; reset() after in-place acResults edits
    Ref<const IvResult> Iv() const {return ivTyped.get(result.share());}
    Ref<const IvResult> Band() const {return bandTyped.get(bandResult.share());}
    Ref<const AcResult> Ac(const std::string &kind) const {static const Json none=Json::object();auto &m=acTyped[kind];if(!acResults->contains(kind)){return m.get(CreateRef<const Json>(none));}auto &j=acResults->at(kind);return m.get(Ref<const Json>(acResults.share(),&j));} // aliasing ref: tied to the acResults object
    Json actions=Json::object();
    Cow batchResult{Json::object()};Json batchSpec=BatchDefaults(), recorderColumns=RecorderDefaults();
    int selectedMember=0;
    std::uint64_t revision=0; // bumped with every DocumentEdited event
    Cow history{Json::array()};
    std::string selectedRun;
    UndoStack undo;
    bool dirty = false, metadataDirty = false, needsBand = false, needsRecalculate = false;
    double biasVoltage_V = 0;
    int selectedLayer = 0, selectedInterface = -1, selectedBias = 0;
    std::chrono::steady_clock::time_point changedAt;
};
void LimitProfiles(DeviceState &d,int keep=3);
struct Project {
    int schema_version = 3;
    std::string name = "Untitled project", active_device;
    std::vector<Ref<DeviceState>> devices;
    std::set<std::string> compare_set;
    Json ui = Json::object();
};
std::filesystem::path ExamplesDirectory();
std::string DocumentHash(const Json &document);
Json PackResults(const Json &value);
Json UnpackResults(const Json &value);
void ValidateWorkspace(const Json &workspace); // throws std::invalid_argument; touches no live state
void ValidateActions(const Json &actions);
int ValidateUi(const Json &ui); // returns history_retention
Json NormalizeWorkspace(Json workspace); // validate + normalize device documents; pure, no Workflow/JobSystem
Json SingleDeviceWorkspace(Json document,std::string projectName);
std::string WorkspaceText(const Json &workspace);
class Workflow final : public Layer {
public:
    explicit Workflow(Ref<JobSystem> jobs, Ref<CommandRegistry> registry = {}, Ref<EventBus> bus = {}, Ref<Notifier> notifier = {}); // no bus/notifier (headless, tests): own ones; events are published synchronously on the calling thread, no ProcessQueue needed
    ~Workflow() override;
    Workflow(const Workflow &) = delete;
    Workflow &operator=(const Workflow &) = delete;
    Project project;
    DeviceState &Current();
    DeviceState &Device(const std::string &id);
    Json Workspace(bool withResults=true) const;
    void SetWorkspace(const Json &value,bool validated=false); // validated: value came from NormalizeWorkspace
    void DeviceOp(const Json &request); // device_*/project_edit/action_settings: in place on the UI thread (no job, no workspace copy)
    static inline std::atomic_size_t constructed{0}; // test counter: Workflow instances ever built
    Json Snapshot() const;
    std::string MemoryReport() const; // SCAPSLAB_PERF: private bytes + history/result sizes
    Json Runs() const;
    void RefreshDirty();
    void StartOperation(const Json &request, const std::string &deviceId);
    void HistoryOp(const Json &request); // history_select/update/clear: in place on the UI thread (no workspace copy, not undoable)
    Json response;
    std::string status = "Ready", path, pendingDevice;
    Ref<JobSystem> jobs;
    Ref<CommandRegistry> commands;
    Ref<EventBus> bus;
    Ref<Notifier> notifier;
    template<class E> void Emit(E e) {bus->Publish(e);}
    JobId active = 0;
    Ref<Operation> pending;
    std::vector<std::pair<std::string,std::string>> pinnedLayers; // open extra "Layer: <name>" windows: {device id, layer id}
    bool autoRecalculateBand = false, autoRecalculate = false, interacting = false, focusLayers = false, resetLayout = false, pendingSheet = false;
    int focusView = -1, focusPanel = -1, pendingDefect = -1;
    bool dirty = false, projectDirty = false, preferProjectUndo = false, includeResults = true, lightResponse = false;
    UndoStack projectUndo;
    std::deque<Json> edits;
    Json queuedProject;
    std::deque<std::string> runQueue;
    Json batchResponse;
    std::size_t batchTotal = 0, batchCompleted = 0;
    Json recent = Json::array();
    Tutorial tutorial;
    TutorialOverlay tutorialOverlay;
    Json tutorialProgress = Json::object();
    std::string tutorialProgressPath;
    bool tutorialAuto = false, tutorialBand = false;
    Tutorial::Predicates TutorialPredicates();
    std::set<std::string> menuOpened;
    void MenuHook(const std::string &root, bool open) {if(open && tutorial.Active())menuOpened.insert(root);tutorialOverlay.Item("scapslab.Menu",root);}
    void TutorialRequest(const Json &request);
    void UpdateTutorial();
    std::string openRequest, fileAction;
    std::string recoveryPath;
    std::chrono::steady_clock::time_point autosavedAt=std::chrono::steady_clock::now();
    void Changed();
    // Save/autosave run as their own Core job beside `active`; one at a time, the next request is coalesced.
    JobId saveJob = 0;
    Ref<Operation> savePending;
    Json queuedSave;
    bool savingAuto = false;
    unsigned epoch = 0, saveEpoch = 0; // bumped when a project is loaded/new
    bool Saving() const { return saveJob != 0 || !queuedSave.empty(); }
    void StartSave(const Json &request);
    void UpdateSave();
    Json Dispatch(const Json &request);
    void OnUpdate(float) override;
    void UpdateOperation();
    bool Pending() const { return Busy() || Saving(); } // calculations or saves
    bool Busy() const { return active != 0 || !queuedProject.empty() || !edits.empty() || !runQueue.empty(); }
};
void RegisterPython();
Json TutorialDefinition(const std::string &id);
Json TutorialCatalog();
Json VerifyTutorial(const std::string &id);
}
