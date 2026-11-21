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
std::string StoreRun(Json &history,const std::string &kind,const Json &result,const Json &document,const Json &request,int retention,const std::string &group=""); // returns run_id; the typed result is attached by the caller (DeviceState::runs)
void CompactHistory(Json &workspace);
std::size_t PrivateBytes();
std::size_t JsonValues(const Json &v);
// Typed results of one device: the stored form (S4). `runs` maps history run_id -> typed result (history Json holds only run metadata).
using RunMap = std::map<std::string,Ref<const ResultBase>>;
struct DeviceResults {
    Ref<const IvResult> result, band;
    std::map<std::string,Ref<const AcResult>> ac;
    Ref<const BatchResult> batch;
    Ref<const RunMap> runs=CreateRef<const RunMap>();
};
std::vector<DeviceResults> ExtractResults(Json &workspace);              // moves every result out of a workspace Json into typed form (leaves `{}`; run entries lose "result")
void AttachResults(Json &device,const Json &history,const DeviceResults &r,bool cached=false); // inverse, for save/export: typed -> Json into one device object (cached: via the results' Json views, for headless/Python where Json is the API)
Json HistoryJson(const Json &history,const RunMap &runs,bool cached=false);                 // history with each run's "result" restored
struct SaveRefs {Ref<const Json> history; DeviceResults results;}; // one device of a save job (immutable, shared with the UI)
class Operation final : public IJob {
public:
    Json document, request, output, acResults, workspace, batchResult;
    Ref<const ResultBase> typedOut;       // typed form of `output` (run/band/cv/cf/qe/batch ops), built on the job thread
    std::vector<DeviceResults> loaded;    // project_load/new: typed results taken out of `workspace`
    std::vector<SaveRefs> shared;         // save jobs
    Json batchSpec=BatchDefaults(), recorderColumns=RecorderDefaults();
    bool slim=false, prewarm=false, outputValid=false; // prewarm: build panel Json views on the worker (UI app only)
      // slim: drop run output Json once typed (UI app); outputValid: output["valid"] read before that
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
// Read-only Json-like handle to a typed result (null = empty `{}`): Json reads go through the result's cached view (ResultBase::View), so panels and tests
// written against Json keep working; the stored form is the typed Ref.
template<class T> class Held {
    Ref<const T> v;
    static const Json &None() {static const Json none=Json::object();return none;}
public:
    Held()=default;
    Ref<const T> typed() const {return v;}
    Ref<const T> share() const {return v;}
    void adopt(Ref<const T> r) {v=std::move(r);}
    void clear() {v.reset();}
    const Json &json() const {return v?v->View():None();}
    operator const Json &() const {return json();}
    const Json &operator*() const {return json();}
    const Json *operator->() const {return &json();}
    template<class K> bool contains(K &&k) const {return json().contains(std::forward<K>(k));}
    template<class K> const Json &at(K &&k) const {return json().at(std::forward<K>(k));}
    template<class K> const Json &operator[](K &&k) const {return json()[std::forward<K>(k)];}
    bool empty() const {return !v;}
    std::size_t size() const {return json().size();}
    template<class... A> auto value(A &&...a) const {return json().value(std::forward<A>(a)...);}
    std::string dump(int i=-1) const {return json().dump(i);}
    bool operator==(const Held &o) const {return v==o.v || (v && o.v && json()==o.json()) || (!v && !o.v);}
    bool operator==(const Json &o) const {return json()==o;}
};
class AcSet { // cv/cf/qe results by kind
    std::map<std::string,Ref<const AcResult>> m;
public:
    bool contains(const std::string &k) const {return m.contains(k);}
    const Json &at(const std::string &k) const {return m.at(k)->View();}
    const Json &operator[](const std::string &k) const {static const Json none=Json::object();auto it=m.find(k);return it==m.end()?none:it->second->View();}
    Json value(const std::string &k,const Json &fallback) const {auto it=m.find(k);return it==m.end()?fallback:it->second->View();}
    Ref<const AcResult> get(const std::string &k) const {auto it=m.find(k);return it==m.end()?Ref<const AcResult>{}:it->second;}
    void set(const std::string &k,Ref<const AcResult> r) {if(r)m[k]=std::move(r);else m.erase(k);}
    void erase(const std::string &k) {m.erase(k);}
    void clear() {m.clear();}
    bool empty() const {return m.empty();}
    std::size_t size() const {return m.size();}
    const std::map<std::string,Ref<const AcResult>> &all() const {return m;}
    Json json(bool cached=false) const {Json o=Json::object();for(const auto &[k,r]:m)o[k]=cached?r->View():r->Dump();return o;} // Json copy of all kinds (cached: through the Json views)
    bool operator==(const AcSet &o) const {if(m.size()!=o.m.size())return false;for(const auto &[k,r]:m){auto it=o.m.find(k);if(it==o.m.end()||(r!=it->second&&r->View()!=it->second->View()))return false;}return true;}
};
struct DeviceState {
    std::string id, name, description, status = "no results";
    Json colour = Json::array({0.25, 0.65, 0.95, 1.0});
    Json document;
    Held<IvResult> bandResult, result;
    AcSet acResults;
    Held<BatchResult> batchResult;
    Ref<const RunMap> runs=CreateRef<const RunMap>(); // typed result per history run_id
    Ref<const IvResult> Iv() const {static const auto none=CreateRef<const IvResult>();return result.typed()?result.typed():none;}
    Ref<const IvResult> Band() const {static const auto none=CreateRef<const IvResult>();return bandResult.typed()?bandResult.typed():none;}
    Ref<const AcResult> Ac(const std::string &kind) const {static const auto none=CreateRef<const AcResult>();auto r=acResults.get(kind);return r?r:none;}
    Ref<const ResultBase> RunResult(const std::string &runId) const {auto it=runs->find(runId);return it==runs->end()?Ref<const ResultBase>{}:it->second;}
    const Json &RunJson(const std::string &runId) const {static const Json none=Json::object();auto r=RunResult(runId);return r?r->View():none;}
    DeviceResults Results() const;                    // typed results as one value (snapshots, save jobs)
    void SetResults(const DeviceResults &r);
    void SyncRuns();                                  // drop typed results of runs that left the history
    Json actions=Json::object();
    Json batchSpec=BatchDefaults(), recorderColumns=RecorderDefaults();
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
    void SetWorkspace(const Json &value,bool validated=false,const std::vector<DeviceResults> *typed=nullptr); // validated: value came from NormalizeWorkspace; typed: its results already taken out by ExtractResults
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
    void WatchPlots(); // UI thread: start bumping plotGen on plot-affecting events
    std::uint64_t plotGen=0; // S3: bumped by RunCompleted/HistoryChanged/SelectionChanged/DocumentEdited/DeviceListChanged/ProjectLoaded; panel plot caches compare it (no per-frame hashing/conversion)
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
    std::vector<SubscriptionHandle> plotSubs; // keep last: unsubscribes before the other members go
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
    double viewIdleSeconds=0;                             // >0 (UI app): free Json views of results idle that long
    std::chrono::steady_clock::time_point viewTrimAt{};
    void TrimViews();
    bool Pending() const { return Busy() || Saving(); } // calculations or saves
    bool Busy() const { return active != 0 || !queuedProject.empty() || !edits.empty() || !runQueue.empty(); }
};
void RegisterPython();
Json TutorialDefinition(const std::string &id);
Json TutorialCatalog();
Json VerifyTutorial(const std::string &id);
}
