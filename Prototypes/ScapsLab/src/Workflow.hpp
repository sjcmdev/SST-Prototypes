#pragma once
#include "Core/Layer.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <nlohmann/json.hpp>
#include "Core/Undo/UndoStack.hpp"
#include <chrono>
#include <deque>
#include <set>
#include <scaps_core/device_project.hpp>
namespace SST::ScapsLab {
using Json = nlohmann::json;
Json ReadProject(const std::string &path);
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
Json BatchPreview(const Json &document,const Json &spec);
class Operation final : public IJob {
public:
    Json document, request, output, acResults, workspace, batchResult;
    Json batchSpec=BatchDefaults(), recorderColumns=RecorderDefaults();
    Operation(Json d, Json r) : document(std::move(d)), request(std::move(r)) {}
    std::string GetName() const override { return "ScapsLab " + request.at("operation").get<std::string>(); }
    std::string GetType() const override { return "scapslab.operation"; }
    void Execute(JobContext &context) override;
};
bool BatchOperation(Operation &operation,JobContext &context);
struct DeviceState {
    std::string id, name, description, status = "no results";
    Json colour = Json::array({0.25, 0.65, 0.95, 1.0});
    Json document, bandResult = Json::object(), result = Json::object(), acResults = Json::object();
    Json actions=Json::object();
    Json batchResult=Json::object(), batchSpec=BatchDefaults(), recorderColumns=RecorderDefaults();
    int selectedMember=0;
    UndoStack undo;
    bool dirty = false, metadataDirty = false, needsBand = false, needsRecalculate = false;
    double biasVoltage_V = 0;
    int selectedLayer = 0, selectedInterface = -1, selectedBias = 0;
    std::chrono::steady_clock::time_point changedAt;
};
struct Project {
    int schema_version = 2;
    std::string name = "Untitled project", active_device;
    std::vector<Ref<DeviceState>> devices;
    std::set<std::string> compare_set;
    Json ui = Json::object();
};
std::filesystem::path ExamplesDirectory();
std::string DocumentHash(const Json &document);
Json PackResults(const Json &value);
Json UnpackResults(const Json &value);
std::string WorkspaceText(const Json &workspace);
class Workflow final : public Layer {
public:
    explicit Workflow(Ref<JobSystem> jobs, Ref<CommandRegistry> registry = {});
    ~Workflow() override;
    Workflow(const Workflow &) = delete;
    Workflow &operator=(const Workflow &) = delete;
    Project project;
    DeviceState &Current();
    DeviceState &Device(const std::string &id);
    Json Workspace() const;
    void SetWorkspace(const Json &value);
    Json Snapshot() const;
    Json Runs() const;
    void RefreshDirty();
    void StartOperation(const Json &request, const std::string &deviceId);
    Json response;
    std::string status = "Ready", path, pendingDevice;
    Ref<JobSystem> jobs;
    Ref<CommandRegistry> commands;
    JobId active = 0;
    Ref<Operation> pending;
    bool autoRecalculateBand = false, autoRecalculate = false, interacting = false, focusLayers = false;
    bool dirty = false, projectDirty = false, preferProjectUndo = false, includeResults = true;
    UndoStack projectUndo;
    std::deque<Json> edits;
    std::deque<std::string> runQueue;
    Json batchResponse;
    std::size_t batchTotal = 0, batchCompleted = 0;
    Json recent = Json::array();
    std::string openRequest, fileAction;
    std::string recoveryPath;
    std::chrono::steady_clock::time_point autosavedAt=std::chrono::steady_clock::now();
    void Changed();
    Json Dispatch(const Json &request);
    void OnUpdate(float) override;
    bool Busy() const { return active != 0 || !edits.empty() || !runQueue.empty(); }
};
void RegisterPython();
}
