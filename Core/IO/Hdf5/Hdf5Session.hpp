#pragma once

#include <future>
#include <nlohmann/json.hpp>
#include "Core/Commands/CommandService.hpp"
#include "Core/JobSystem/JobSystem.hpp"

namespace SST {
// GUI and embedded Python use these same stable command IDs and job implementation.
struct Hdf5Request {
    nlohmann::json input;
    std::promise<nlohmann::json> response;
    JobId jobId = 0;
};
Result<void> RegisterHdf5Commands(CommandRegistry& registry, Ref<JobSystem> jobs);

class Hdf5Session {
public:
    explicit Hdf5Session(Ref<JobSystem> jobs = {});
    nlohmann::json Start(const nlohmann::json& request);
    nlohmann::json Poll();
    nlohmann::json Cancel();
private:
    Ref<JobSystem> m_Jobs;
    Ref<CommandRegistry> m_Registry;
    CommandService m_Commands;
    std::future<nlohmann::json> m_Response;
    nlohmann::json m_LastResponse = nullptr;
    JobId m_JobId = 0;
};
void RegisterHdf5PythonModule();
}
