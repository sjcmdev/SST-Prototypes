#pragma once
// S8: the surface the typed ScapsLab commands need from the application state. Workflow implements it; tests implement it with a recording mock
// (no JobSystem). Commands decide (reject / queue / start); the store performs. Keep it narrow: every method is one decision-free action or one query.
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>
#include "Core/Diagnostics/StructuredError.hpp"
namespace SST::ScapsLab {
using Json = nlohmann::json;
class ILabStore {
public:
    virtual ~ILabStore() = default;
    // queries
    virtual bool Busy() const = 0;                                   // any device lane or the project lane has work
    virtual bool AnyActive() const = 0;                              // a job runs on some device
    virtual bool ReplacingProject() const = 0;                       // the active device runs load/project_load/layer
    virtual bool DeviceRunning(const std::string &device) const = 0; // that device has a running job
    virtual void RequireDevice(const std::string &device) const = 0; // throws std::invalid_argument for an unknown id
    virtual std::vector<std::string> ActiveOps() const = 0;          // operation names of the running jobs
    virtual bool EditQueued() const = 0;                             // some lane has a queued edit
    // actions
    virtual void StartJob(Json request) = 0;                         // job on the active device's lane (request carries "operation")
    virtual void StartSave(const Json &request) = 0;                 // own save lane, coalesced
    virtual bool CancelAll() = 0;                                    // true when something was running
    virtual void CancelDevice(const std::string &device) = 0;
    virtual void DeferProjectLoad(const Json &request) = 0;          // cancel calculations, open the project when the lanes are idle
    virtual void QueueEdit(const Json &request) = 0;                 // merge into the active device's edit queue (latest wins per gesture)
    virtual void PublishSnapshot() = 0;                              // response = workspace snapshot (read-only commands)
    virtual void HistoryOp(const Json &request) = 0;                 // in place on the UI thread
    virtual void DeviceOp(const Json &request) = 0;                  // in place on the UI thread
    virtual void TutorialRequest(const Json &request) = 0;           // legacy JSON-driven handler
    virtual void SelectDevice(const std::string &device) = 0;
    virtual void SetAutoRecalculate(bool automatic, std::optional<bool> band) = 0;
    virtual Result<void> Step(bool undo) = 0;                        // undo/redo on the preferred stack
    virtual void RunAll() = 0;                                       // one run per compared device, lanes in parallel
};
}
