#pragma once
#include "Core/Layer.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <nlohmann/json.hpp>
#include "Core/Undo/UndoStack.hpp"
#include <chrono>
#include <deque>
#include <scaps_core/device_project.hpp>
namespace SST::ScapsLab {
using Json = nlohmann::json;
Json ReadProject(const std::string &path);
scaps_core::DeviceProject Model(const Json &document);
void WriteProject(const Json &document, const std::string &path);
Json NewDefect(bool sheet = false);
const Json &PointAtBias(const Json &sweep,const Json &band,int selected);
void ChangeSheetDistribution(Json &defect,const std::string &shape,const std::string &convention);
class Operation final : public IJob {
public:
    Json document, request, output, acResults;
    Operation(Json d, Json r) : document(std::move(d)), request(std::move(r)) {}
    std::string GetName() const override { return "ScapsLab " + request.at("operation").get<std::string>(); }
    std::string GetType() const override { return "scapslab.operation"; }
    void Execute(JobContext &context) override;
};
class Workflow final : public Layer {
public:
    explicit Workflow(Ref<JobSystem> jobs, Ref<CommandRegistry> registry = {});
    ~Workflow() override;
    Workflow(const Workflow &) = delete;
    Workflow &operator=(const Workflow &) = delete;
    Json document, bandResult = Json::object(), result = Json::object(), acResults = Json::object(), response;
    std::string status = "Ready", path;
    Ref<JobSystem> jobs;
    Ref<CommandRegistry> commands;
    JobId active = 0;
    Ref<Operation> pending;
    double biasVoltage_V = 0;
    bool autoRecalculateBand = false, needsBand = false;
    bool dirty = false, autoRecalculate = false, interacting = false, needsRecalculate = false, focusLayers = false;
    int selectedLayer = 0, selectedInterface = -1, selectedBias = 0;
    UndoStack undo;
    std::deque<Json> edits;
    std::chrono::steady_clock::time_point changedAt;
    void Changed();
    Json Dispatch(const Json &request);
    void OnUpdate(float) override;
    bool Busy() const { return active != 0 || !edits.empty(); }
};
void RegisterPython();
}

