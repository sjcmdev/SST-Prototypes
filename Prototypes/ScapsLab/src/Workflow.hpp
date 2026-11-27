#pragma once
#include "Core/Layer.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notifier.hpp"
#include "Events.hpp"
#include "Commands/Store.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <nlohmann/json.hpp>
#include "Core/Undo/UndoStack.hpp"
#include "Core/Presentation/Tutorial.hpp"
#include "Domain/Results.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Domain/ProjectDraft.hpp"
#include <scaps_core/solver_types.hpp>
#include "Domain/BatchDraft.hpp"
#include "PlotSettings.hpp"
#include "Core/Presentation/TutorialOverlay.hpp"
#include "Library/Session.hpp"
#include <algorithm>
#include <chrono>
#include <mutex>
#include <atomic>
#include <array>
#include <deque>
#include <exception>
#include <set>
#include <functional>
#include <map>
#include <scaps_core/device_project.hpp>
namespace SST {class IOLayer;}
namespace SST::ScapsLab {
using Json = nlohmann::json;
Json ReadProject(const std::string &path,std::vector<std::string>* importWarnings=nullptr);
Json NewDeviceDocument(); // template device with SCAPS-like flat-band contacts (engine default and example decks untouched)
Json NormalizeDocument(Json document);
scaps_core::Profile ProfileFromJson(const Json &spec, bool logarithmic=false);
Json ProfileFromFile(const std::string &path, bool logarithmic=false);
struct ProfileSamplesData {std::vector<double> x_m,value_SI;std::string source_file;std::size_t source_point_count=0;};
struct ProfileFileData {scaps_core::Profile profile;std::vector<std::string> warnings;};
ProfileFileData ReadProfileFile(const std::string&,bool logarithmic=false);
ProfileSamplesData ProfileSamples(const scaps_core::Profile&,double thickness_m,const scaps_core::Profile* composition=nullptr);
scaps_core::Profile ResolveSharedProfile(const scaps_core::Profile&,const ProjectDraft&,const std::string& layerId,const std::string& parameter);
void ExportProfile(const ProfileSamplesData&,const std::string& path,const std::string& parameter="value_SI");
Json ProfileSamples(const Json &spec, double thickness_m, const Json &composition=Json::object());
Json ResolveSharedProfile(Json spec,const Json &document,const std::string &layerId,const std::string &parameter);
void ExportProfile(const Json &samples, const std::string &path);
scaps_core::DeviceProject Model(const Json &document);
void WriteProject(const Json &document, const std::string &path);
Json AM15Spectrum();
const scaps_core::OpticalTable& AM15SpectrumValues();
Json SelfContained(Json document);
struct ResultRun {
    std::string runId,deviceId,deviceName,kind;
    Ref<const ResultBase> result;
};
std::vector<std::filesystem::path> ExportResults(const std::vector<ResultRun>& selected,const std::filesystem::path& path,const std::string& format);
Json NewDefect(bool sheet = false);
const Json &PointAtBias(const Json &sweep,const Json &band,int selected);
void ChangeSheetDistribution(Json &defect,const std::string &shape,const std::string &convention);
Json BatchDefaults();
Json RecorderDefaults();
Json BatchParameters(const Json &document);
Json BatchParameters(const ProjectDraft&);
Json BatchPreview(const ProjectDraft&,const Json&);
Json QuickSweep(const ProjectDraft&,const Json&);
std::string ParameterLabel(const std::string &path,const std::string &unit,const Json &value,const Json &before=nullptr);
std::string RunLabel(const Json &run);
std::string DiffLabel(const Json &previous,const Json &document);
Json BatchPreview(const Json &document,const Json &spec);
Json QuickSweep(const Json &document,const Json &request);
void TrimHistory(Json &history,int retention);
std::string StoreRun(Json &history,const std::string &kind,const Json &result,const Json &document,const Json &request,int retention,const std::string &group="",const Json *parameters=nullptr,const std::string *modelHash=nullptr); // returns run_id; the typed result is attached by the caller (DeviceState::runs)
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
void AttachResults(Json &device,const Json &history,const DeviceResults &r,bool cached=false,bool compact=false); // file v4 pools aliases; Python retains the expanded response
Json HistoryJson(const Json &history,const RunMap &runs,bool cached=false);                 // history with each run's "result" restored
struct SaveRefs {Ref<const Json> history; DeviceResults results;}; // one device of a save job (immutable, shared with the UI)
// Live hand-off from a running job to the UI (progressive I-V, photovoltaic estimate, finished batch members). The worker appends under the lock, the UI thread
// copies a snapshot at most every ~100 ms. Nothing here is a result: it is never stored in history, saved, exported or read by an exactness-claiming readout.
struct PvEstimate {bool valid=false;double Voc_V=NAN,Jsc_A_m2=NAN,FF=NAN,eta=NAN,Pmax_Wm2=NAN,Vmpp_V=NAN,Jmpp_A_m2=NAN;};
PvEstimate EstimatePv(const std::vector<double> &V,const std::vector<double> &J,double incidentPower_W_m2); // from the solved sweep (plotted voltage, native terminal J), no extra solves
struct LiveMember {int index=0;Column v,j;};
struct LiveFeed {
    std::mutex lock;
    std::uint64_t seq=0;
    std::vector<double> v,j;           // I-V points solved so far (plotted voltage [V], terminal J [A/m^2], NaN when not converged)
    PvEstimate pv;
    std::vector<Ref<const DlcpProfilePoint>> dlcp;
    std::vector<Ref<const DltsTrace>> dlts;
    std::vector<LiveMember> members;   // batch members finished so far
    std::size_t memberTotal=0;
    Ref<const BatchDraft> batch;
    void Dlts(Ref<const DltsTrace> p){std::lock_guard g(lock);dlts.push_back(std::move(p));++seq;}
    void Dlcp(Ref<const DlcpProfilePoint> p) {std::lock_guard g(lock);dlcp.push_back(std::move(p));++seq;}
    void Point(double V,double J) {std::lock_guard g(lock);v.push_back(V);j.push_back(J);++seq;}
    void Estimate(const PvEstimate &e) {std::lock_guard g(lock);pv=e;++seq;}
    void AddMember(LiveMember m) {std::lock_guard g(lock);members.push_back(std::move(m));++seq;}
};
class Operation final : public IJob {
public:
    Ref<const RunPlan> runPlan;
    std::optional<RunAction> action;
    std::vector<std::pair<ActionKind,Ref<const ResultBase>>> completedActions;
    std::stop_source cancellation;
    std::atomic_bool paused=false;
    std::size_t nextAction=0;
    std::size_t nextMember=0;
    Ref<BatchResult> actionBatch;
    Ref<const BatchPlan> preparedBatch;
    bool deferMetadata=false,retainProfiles=true,refinePv=true,parallelMember=false,retainBiasProfiles=false;
    Ref<const scaps_core::DeviceState> warmInput, warmOutput;
    Ref<const std::vector<double>> liveMeshInput,liveMeshOutput;
    std::optional<scaps_core::BatchSpec> measurement;
    bool automatic=false;
    std::uint64_t revision=0;
    int detailBias=0;
    std::string detailRun;
    std::string replaceRun;
    Json document, request, output, workspace, batchResult;
    std::vector<ResultRun> exportRuns;
    std::exception_ptr exportError;
    Ref<const AcResult> acInput;
    Ref<const IvResult> ivInput;
    Ref<const ResultBase> typedOut;       // typed form of `output` (run/band/cv/cf/qe/batch ops), built on the job thread
    Ref<const AcResult> dlcpInput;
    Ref<const BatchResult> batchInput;
    Ref<const ProjectDraft> draft;
    std::vector<DeviceResults> loaded;
    std::vector<Ref<const ProjectDraft>> loadedDrafts;    // project_load/new: typed results taken out of `workspace`
    std::vector<SaveRefs> shared;         // save jobs
    struct Member {Json result,model,parameters;std::string hash;Ref<const BatchResult> typed;double t=0;}; // one batch member as a history run, prepared on the worker (apply only stores it)
    std::vector<Member> members;
    Ref<LiveFeed> feed;                   // set by Workflow::StartOperation for exact runs and batches
    std::string batchGroup;
    Ref<const BatchDraft> batch=CreateRef<const BatchDraft>();
    Ref<const RecorderDraft> recorder=CreateRef<const RecorderDraft>();
    std::shared_ptr<const Library::State> assetState;
    Library::Browser assetBrowser;
    std::shared_ptr<const Library::DraftMap> assetDrafts;
    bool superseded=false;                // set on the UI thread when undo/redo changed the document (or removed the device) under this job: its result is dropped on arrival
    bool outputValid=false;
    bool jsonResponse=false; // JSON adapter preserves full-curve profile output; GUI requests lightweight typed samples
    Operation(Ref<const ProjectDraft> d, Json r) : request(std::move(r)), draft(std::move(d)) {}
    std::string GetName() const override { return "ScapsLab " + request.at("operation").get<std::string>(); }
    std::string GetType() const override { return "scapslab.operation"; }
    void Execute(JobContext &context) override;
};
bool BatchOperation(Operation &operation,JobContext &context);
void SolveDlts(Operation&,JobContext&,const scaps_core::Mesh1D&);
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
inline constexpr std::size_t kUndoMaxDepth=500, kUndoMaxBytes=8u<<20; // per undo stack: oldest steps drop first (a step is a small delta, so this is hundreds of edits)
struct DeviceState {
    DeviceState() {undo.SetLimits(kUndoMaxDepth,kUndoMaxBytes);}
    std::string id, name, description, status = "no results";
    Json colour = Json::array({0.25, 0.65, 0.95, 1.0});
    Json fileExtensions=Json::object();
    Ref<const ProjectDraft> draft;
    mutable Ref<const Json> fileView;
    const Json& Document() const {if(!fileView)fileView=CreateRef<const Json>(draft?DraftDocument(*draft):Json{});return *fileView;}
    void SetDraft(Ref<const ProjectDraft> value) {draft=std::move(value);fileView.reset();}
    void SetDocument(const Json& value) {SetDraft(value.empty()?Ref<const ProjectDraft>{}:DraftFromDocument(value));}
    Held<IvResult> bandResult, result;
    AcSet acResults;
    std::map<std::string,std::uint64_t> acRevisions;
    Held<BatchResult> batchResult;
    Ref<const RunMap> runs=CreateRef<const RunMap>(); // typed result per history run_id
    Ref<const IvResult> Iv() const {static const auto none=CreateRef<const IvResult>();return result.typed()?result.typed():none;}
    Ref<const IvResult> Band() const {static const auto none=CreateRef<const IvResult>();return bandResult.typed()?bandResult.typed():none;}
    Ref<const IvResult> ProfileSweep() const {auto stored=std::dynamic_pointer_cast<const IvResult>(RunResult(selectedRun));return stored?stored:Iv();}
    Ref<const IvResult> ProfileBand() const {static const auto none=CreateRef<const IvResult>();return selectedRun.empty()?Band():detailRun==selectedRun && storedBand?storedBand:none;}
    Ref<const AcResult> Ac(const std::string &kind) const {static const auto none=CreateRef<const AcResult>();auto r=acResults.get(kind);return r?r:none;}
    Ref<const ResultBase> RunResult(const std::string &runId) const {auto it=runs->find(runId);return it==runs->end()?Ref<const ResultBase>{}:it->second;}
    const Json &RunJson(const std::string &runId) const {static const Json none=Json::object();auto r=RunResult(runId);return r?r->View():none;}
    DeviceResults Results() const;                    // typed results as one value (snapshots, save jobs)
    void SetResults(const DeviceResults &r);
    void SyncRuns();                                  // drop typed results of runs that left the history
    Json actions=Json::object();
    Ref<const RunPlan> runPlan=CreateRef<const RunPlan>();
    bool runPlanEdited=false;
    Ref<const BatchDraft> batch=CreateRef<const BatchDraft>();
    Ref<const RecorderDraft> recorder=CreateRef<const RecorderDraft>();
    mutable Ref<const Json> batchView,recorderView;
    const Json& BatchSpec() const {if(!batchView)batchView=CreateRef<const Json>(BatchDraftJson(*batch));return *batchView;}
    const Json& RecorderSpec() const {if(!recorderView)recorderView=CreateRef<const Json>(RecorderDraftJson(*recorder));return *recorderView;}
    void SetBatch(Ref<const BatchDraft> value){batch=std::move(value);batchView.reset();}
    void SetRecorder(Ref<const RecorderDraft> value){recorder=std::move(value);recorderView.reset();}
    void SetBatchSpec(const Json& value){SetBatch(ReadBatchDraft(value));}
    void SetRecorderSpec(const Json& value){SetRecorder(ReadRecorderDraft(value));}
    int selectedMember=0;
    int selectedAttempt=0; // zero selects the latest attempt
    std::uint64_t revision=0; // bumped with every DocumentEdited event
    Cow history{Json::array()};
    std::string selectedRun;
    bool pinStoredView=false,needsBiasDetails=false;
    std::string detailRun;
    Ref<const IvResult> storedBand;
    UndoStack undo;
    bool dirty = false, metadataDirty = false, needsBand = false, needsRecalculate = false;
    double biasVoltage_V = 0;
    int selectedLayer = 0, selectedInterface = -1, selectedBias = 0;
    std::chrono::steady_clock::time_point changedAt;
    Ref<const IvResult> changeIv,changeBand; // last explicit reference, retained across automatic solves; never saved
    int changeBias=0;
    Ref<const scaps_core::DeviceState> warmIv,warmBand;
    bool liveGesture=false,previewRequested=false;
    int liveStride=1;
    Ref<const std::vector<double>> liveMesh;
    std::vector<double> liveBiases;
    std::map<std::string,Ref<const AcResult>> changeAc;
    void RememberChange() {if(!changeIv && !changeBand)changeBias=selectedBias;if(!changeIv)changeIv=result.typed();if(!changeBand)changeBand=bandResult.empty()?result.typed():bandResult.typed();for(const auto& [kind,r]:acResults.all())changeAc.try_emplace(kind,r);}
    const IvPoint* ChangePoint() const {const auto& r=changeIv?changeIv:changeBand;return r && !r->points.empty()?&r->points[std::clamp(changeBias,0,int(r->points.size())-1)]:nullptr;}
    void ClearChange() {changeIv.reset();changeBand.reset();changeAc.clear();}
    std::vector<double> previewV, previewJ; // live slider-drag preview curve (J-V only): never saved, never in history/readouts/exports; cleared by the next exact run
    // Progressive exact run (never saved/stored/exported): partial I-V, photovoltaic estimate, finished batch members. Copied from the job's LiveFeed; cleared when the job ends.
    std::vector<double> liveV, liveJ;
    std::vector<Ref<const DlcpProfilePoint>> liveDlcp;
    std::vector<Ref<const DltsTrace>> liveDlts;
    PvEstimate pvEstimate;
    std::vector<LiveMember> liveMembers;
    std::size_t liveMemberTotal = 0;
    Ref<const BatchDraft> liveBatch;
    bool liveRunning = false;
    std::uint64_t resultRevision = 0; // device revision used by the live I-V; a new bias/range request does not edit the device
    bool Stale() const {return !result.empty() && resultRevision!=revision;}
    std::uint64_t bandRevision = 0; // d.revision the live band (request "live") was computed for
    bool BandFresh() const {return !bandResult.empty() && bandRevision==revision;} // live band/profile job of the current model: panels show it instead of the outdated run
    bool previewCoarse = false;
    std::uint64_t previewRev = 0; // d.revision the last preview was started for
};
void LimitProfiles(DeviceState &d,int keep=3);
struct Project {
    int schema_version = 4;
    std::string name = "Untitled project", active_device;
    std::vector<Ref<DeviceState>> devices;
    std::set<std::string> compare_set;
    Json ui = Json::object();
    Json fileExtensions=Json::object();
};
std::filesystem::path ExamplesDirectory();
std::string DocumentHash(const Json &document);
Json PackResults(const Json &value);
Json UnpackResults(const Json &value);
void ValidateWorkspace(const Json &workspace); // throws std::invalid_argument; touches no live state
void ValidateActions(const Json &actions);
int ValidateUi(const Json &ui,bool strictPlotRanges=true); // returns history_retention
Json NormalizeWorkspace(Json workspace); // validate + normalize device documents; pure, no Workflow/JobSystem
Json SingleDeviceWorkspace(Json document,std::string projectName);
std::string WorkspaceText(const Json &workspace);
class Workflow final : public Layer, public ILabStore {
public:
    std::set<std::tuple<std::string,std::string,int,int,int>> resultCompare; // device, run, member, Action, attempt
    explicit Workflow(Ref<JobSystem> jobs, Ref<CommandRegistry> registry = {}, Ref<EventBus> bus = {}, Ref<Notifier> notifier = {}); // no bus/notifier (headless, tests): own ones; events are published synchronously on the calling thread, no ProcessQueue needed
    ~Workflow() override;
    Workflow(const Workflow &) = delete;
    Workflow &operator=(const Workflow &) = delete;
    Project project;
    Library::Session library;
    std::optional<Library::Request> libraryDrop;
    bool libraryDropShown=false;
    bool bindingLibraryReply=false,bindingLibraryData=false;
    void LibraryTyped(std::shared_ptr<const Library::Request>) override;
    void PollLibrary();
    void RefreshLinkedAssets();
    DeviceState &Current();
    DeviceState &Device(const std::string &id);
    Json Workspace(bool withResults=true) const;
    void SetWorkspace(const Json &value,bool validated=false,const std::vector<DeviceResults> *typed=nullptr,const std::vector<Ref<const ProjectDraft>>* drafts=nullptr); // validated: value came from NormalizeWorkspace; typed: its results already taken out by ExtractResults
    void DeviceOp(const Json &request) override {DeviceOp(request,{});}
    void DeviceOp(const Json&,Ref<const ProjectDraft>); // device_*/project_edit/action_settings: in place on the UI thread (no job, no workspace copy); add/duplicate/remove/update/move/project_edit push a small undo delta
    static inline std::atomic_size_t constructed{0}; // test counter: Workflow instances ever built
    Json Snapshot() const;
    std::string MemoryReport() const; // SCAPSLAB_PERF: private bytes + history/result sizes
    std::vector<ResultRun> ResultRuns() const;
    Json Runs() const;
    void RefreshDirty();
    void StartOperation(const Json &request, const std::string &deviceId,Ref<const ProjectDraft> edit={},Ref<const BatchDraft> batch={},Ref<const RecorderDraft> recorder={},Ref<const RunPlan> plan={},std::string detailRun={},Ref<const AcResult> dlcp={},bool automatic=false);
    void HistoryOp(const Json &request) override; // history_select/update/clear: in place on the UI thread (no workspace copy; undoable as a run delta, see Commands/UndoOps.cpp)
    void RecalculateLive(ActionKind) override;
    void SelectBias(int) override;
    void SetLivePreviewStride(int every) override {livePreviewStride=every;settings["live_preview_stride"]=every;SaveSettings();}
    void SetLiveEditing(bool active) override {dragging=active;}
    void RecalculateStoredBias() override;
    Json response;
    std::string status = "Ready", path;
    Ref<JobSystem> jobs;
    Ref<CommandRegistry> commands;
    Ref<EventBus> bus;
    Ref<Notifier> notifier;
    bool boundsFileBusy=false;
    bool bindingBoundsReply=false; // the binding keeps file diagnostics scoped to its current request
    std::string boundsFileDiagnostic;
    void BoundsFileTyped(BoundsFileAction action) override;
    template<class E> void Emit(E e) {bus->Publish(e);}
    void WatchPlots(); // UI thread: start bumping plotGen on plot-affecting events
    std::uint64_t docGen=0;  // bumped by DocumentEdited only: result plots are keyed by plotGen (result data), so a drag does not rebuild them
    std::uint64_t plotGen=0; // S3: bumped by RunCompleted/HistoryChanged/SelectionChanged/DocumentEdited/DeviceListChanged/ProjectLoaded; panel plot caches compare it (no per-frame hashing/conversion)
    PlotSession plotSession;
    bool plotPreferences=false, clearSimulations=false, deleteTreeRecord=false;
    Ref<ContextManager> inputContexts;
    std::size_t StoredSimulationCount() const;
    // S6: one lane per device (serial inside, parallel across devices). `lanes[""]`-style project lane is `projectLane`: project-level
    // requests (load while calculating) wait there until every device lane is idle; the save job has its own lane `saveLane`.
    struct Queued {Json request; std::string key;Ref<const ProjectDraft> draft;}; // key: a newer queued request with the same non-empty key replaces this one (latest wins)
    struct Lane {
        std::string device; JobId active=0; Ref<Operation> pending; std::deque<Queued> queue;
        std::uint64_t liveSeq=0; std::chrono::steady_clock::time_point liveAt{}, runStarted{};
        bool Idle() const {return !active && queue.empty();}
        void Enqueue(Json request,std::string key={}) { // coalescing: only the newest request per key stays queued
            if(!key.empty()) std::erase_if(queue,[&](const Queued &q){return q.key==key;});
            queue.push_back({std::move(request),std::move(key)});
        }
        std::string Op() const {return pending?pending->request.value("operation",std::string{}):std::string{};}
    };
    std::map<std::string,Lane> lanes; // device id -> lane
    Lane projectLane, saveLane;
    Lane &LaneOf(const std::string &device) {auto &l=lanes[device];l.device=device;return l;}
    void Submit(const std::string &device,Json request,std::string key={}); // queue on the device's lane (starts at once when idle); same key = newest wins
    void CancelDevice(const std::string &device) override;                       // cancels that device's job and drops its queued batch runs; other lanes keep running
    bool AnyActive() const override {for(const auto &[id,l]:lanes) if(l.active) return true;return false;}
    JobId LaneJob(const std::string &device) const {auto it=lanes.find(device);return it==lanes.end()?0:it->second.active;}
    const Lane *PrimaryLane() const {auto it=lanes.find(project.active_device);if(it!=lanes.end() && it->second.active) return &it->second;for(const auto &[id,l]:lanes) if(l.active) return &l;return nullptr;}
    JobId ActiveJob() const {const auto *l=PrimaryLane();return l?l->active:0;}                // the active device's job, else any running one (progress display)
    Ref<Operation> ActiveOp() const {const auto *l=PrimaryLane();return l?l->pending:Ref<Operation>{};}
    const ProjectDraft *EditDraft() const;                                                            // newest queued/running edit document of the active device, or null
    bool ReplacingProject() const override;                                                          // the active device's lane runs load/project_load/layer
    std::vector<std::pair<std::string,std::string>> pinnedLayers; // open extra "Layer: <name>" windows: {device id, layer id}
    // Live recalculation while a slider is held (latest wins, <=10/s, coarse every 4th point when the last full run took >150 ms)
    bool dragging = false, dragSeen = false, fineLock = false, liveSeen = false; // dragSeen: set by the slider helper each frame, App copies it to dragging
    int livePreviewStride = 4; // 0: auto, 1..4: every n-th sweep point (both endpoints retained)
    double lastFullMs = 0; unsigned previewsStarted = 0, liveBandsStarted = 0; std::chrono::steady_clock::time_point lastLive, lastBandLive, lastAcLive;
    bool autoRecalculateBand = false, autoRecalculate = false, interacting = false, focusLayers = false, resetLayout = false, pendingSheet = false;
    std::string focusResultDevice,focusResultRun;
    void FocusResults(const DeviceState&);
    void RestoreResultFocus();
    int revealView = -1;
    int focusView = -1, focusPanel = -1, pendingDefect = -1;
    bool dirty = false, projectDirty = false, preferProjectUndo = false, includeResults = true, lightResponse = false;
    UndoStack projectUndo;
    Json batchResponse;
    std::size_t batchTotal = 0, batchCompleted = 0;
    Json recent = Json::array();
    // S10: disk IO never runs on the UI thread. AsyncIo runs `fn` on a worker (non-empty return = error -> warning toast), `done` runs on the UI thread afterwards.
    // Settings/recent/recovery/tutorial-progress writes and the project save/load/export jobs all go through Io:: (Io.hpp), which tests watch.
    void AsyncIo(std::string what,std::function<std::string()> fn,std::function<void()> done={});
    void PollIo();
    struct IoTask {JobId id; std::string what; Ref<std::string> error; std::function<void()> done;};
    std::vector<IoTask> ioTasks;
    bool IoPending() const {return !ioTasks.empty();}
    std::string recentPath; Json savedRecent=Json::array(); bool recentLoaded=true, recentWriting=false, recoveryAvailable=false, autosaveFailing=false;
    void SetRecentPath(const std::string &file);  // loads the recent list + probes the autosave file asynchronously; later changes of `recent` are written asynchronously
    void PersistRecent();
    std::string settingsPath; Json settings=Json::object(); bool settingsWriting=false, settingsDirty=false; // app settings (scapslab-ui.json): loaded at start-up, writes are async and never overlap
    void SaveSettings();
    void TouchRecent(const std::string &file);
    void DropRecovery(const std::string &keep);   // removes the autosave file unless `keep` is that file
    void RegisterIo(IOLayer &io);                 // IOLayer handlers: Core ProjectSaveRequested, ScapsLab Project{Save,Open,Export}Requested
    Json ioResult;                                // Dispatch result of the last IOLayer handler
    std::vector<SubscriptionHandle> plotSubs; // keep last: unsubscribes before the other members go
    Tutorial tutorial;
    TutorialOverlay tutorialOverlay;
    Json tutorialProgress = Json::object();
    std::string tutorialProgressPath;
    bool tutorialAuto = false, tutorialBand = false;
    Tutorial::Predicates TutorialPredicates();
    std::set<std::string> menuOpened;
    void MenuHook(const std::string &root, bool open) {if(open && tutorial.Active())menuOpened.insert(root);tutorialOverlay.Item("scapslab.Menu",root);}
    void TutorialRequest(const Json &request) override;
    void UpdateTutorial();
    std::string openRequest, fileAction;
    std::string recoveryPath;
    std::chrono::steady_clock::time_point autosavedAt=std::chrono::steady_clock::now();
    void Changed();
    // Save/autosave run as their own Core job (saveLane) beside the calculation lanes; one at a time, the next request is coalesced.
    bool savingAuto = false;
    unsigned epoch = 0, saveEpoch = 0; // bumped when a project is loaded/new
    bool Saving() const { return saveLane.active != 0 || !saveLane.queue.empty(); }
    void StartSave(const Json &request) override;
    void UpdateSave();
    Json Dispatch(const Json &request);        // JSON adapter: "operation" -> typed command (Commands/); Python, headless, tutorials
    template<class T> Json Exec(const T &command) { // typed API: panels call Exec(Cmd::Run::Make()); same registry path, same reply as Dispatch
        return Reply([&]{CommandContext c;c.Set<T>("args",command);return commands->Execute(CommandID{std::string("scapslab.")+T::info.op},std::move(c));});
    }
    Json Reply(const std::function<Result<CommandOutcome>()> &run);
    // ILabStore (Commands/Store.hpp): what the typed commands ask of the application
    bool DeviceRunning(const std::string &device) const override {return LaneJob(device)!=0;}
    void RequireDevice(const std::string &device) const override;
    std::vector<std::string> ActiveOps() const override;
    bool EditQueued() const override;
    void StartJob(Json request) override {StartOperation(request,project.active_device);}
    bool CancelAll() override;
    void DeferProjectLoad(const Json &request) override;
    void QueueEdit(const Json &request) override;
    void PublishSnapshot() override {response=Snapshot();}
    void SelectDevice(const std::string &device) override;
    void SetAutoRecalculate(bool automatic,std::optional<bool> band) override;
    Result<void> Step(bool undo) override; // synchronous: state changes now (no job); unapplied edits of the active device are discarded first, running calculations are cancelled and dropped
    bool CanUndo() const; bool CanRedo() const; // a discardable pending edit counts
    // S9 undo support (Commands/UndoOps.cpp)
    bool HasDevice(const std::string &id) const {return std::any_of(project.devices.begin(),project.devices.end(),[&](const auto &d){return d->id==id;});}
    void ReturnToLive(DeviceState &d);
    void ApplyDraft(DeviceState &d,Ref<const ProjectDraft> draft);
    void EditTyped(Ref<const ProjectDraft> draft,const std::string& gesture) override;
    void BatchTyped(Ref<const BatchDraft>,Ref<const RecorderDraft>) override; // undo/redo of an edit: new document, revision++, DocumentEdited, auto-recalculate arms
    void QuickSweepTyped(Ref<const BatchDraft>,Ref<const RecorderDraft>,Ref<const ProjectDraft>,Ref<const RunPlan> = {}) override;
    void RunBatchTyped(Ref<const BatchDraft>,Ref<const RecorderDraft>,Ref<const RunPlan>) override;
    void ExportDlcpTyped(Ref<const AcResult>,const std::string&) override;
    void SetRunPlanTyped(Ref<const RunPlan>,const std::string& gesture={}) override;
    void RunDlcpTyped(std::optional<RunAction>) override;
    void SingleShotTyped(Ref<const RunPlan>) override;
    bool CanUpdateSingleShot() const override;
    void UpdateSingleShotTyped() override;
    void CaptureLiveTyped(ActionKind) override;
    void ResumeActions() override;
    void StartActions(const std::string& device,Ref<const RunPlan>,bool automatic=false,bool preview=false);
    bool CancelLane(Lane& lane);
    bool DiscardPendingEdits(const std::string &device); // running/queued document-changing jobs of the device are dropped; true when there were any
    void SupersedeJobs(const std::string &device);       // running calculation of the device is cancelled and its result dropped on arrival
    void DropDeviceLane(const std::string &device);      // device leaves the project: its job is cancelled/dropped, its queue cleared
    void RunAll() override;
    void OnUpdate(float) override;
    void UpdateOperation();
    void PullLive(Lane &lane);
    void FinishOperation(Lane &lane);
    double viewIdleSeconds=0;                             // >0 (UI app): free Json views of results idle that long
    JobId warmJob=0;                                      // compatibility: typed panels need no warm-up job
    bool ViewsReady();                                    // typed results are ready to render immediately
    std::chrono::steady_clock::time_point viewTrimAt{};
    void TrimViews();
    bool Pending() const { return Busy() || Saving() || library.Busy(); } // calculations, saves or typed library jobs
    bool Busy() const override {for(const auto &[id,l]:lanes) if(!l.Idle()) return true;return !projectLane.Idle();}
    bool Busy(const std::string &device) const {auto it=lanes.find(device);return it!=lanes.end() && !it->second.Idle();}
};
void RegisterPython();
Json TutorialDefinition(const std::string &id);
Json TutorialCatalog();
Json VerifyTutorial(const std::string &id);
}
