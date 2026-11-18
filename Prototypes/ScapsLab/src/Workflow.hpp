#pragma once
#include "Core/Layer.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <nlohmann/json.hpp>
#include <scaps_core/device_project.hpp>
namespace SST::ScapsLab {
using Json = nlohmann::json;
Json ReadProject(const std::string &path);
scaps_core::DeviceProject Model(const Json &document);
void WriteProject(const Json &document, const std::string &path);
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
    Json document, result = Json::object(), acResults = Json::object(), response;
    std::string status = "Ready", path;
    Ref<JobSystem> jobs;
    Ref<CommandRegistry> commands;
    JobId active = 0;
    Ref<Operation> pending;
    bool dirty = false;
    Json Dispatch(const Json &request);
    void OnUpdate(float) override;
    bool Busy() const { return active != 0; }
};
void RegisterPython();
}

