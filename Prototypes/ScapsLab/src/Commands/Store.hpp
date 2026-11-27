#pragma once
// S8: the surface the typed ScapsLab commands need from the application state. Workflow implements it; tests implement it with a recording mock
// (no JobSystem). Commands decide (reject / queue / start); the store performs. Keep it narrow: every method is one decision-free action or one query.
#include <nlohmann/json.hpp>
#include "Domain/ProjectDraft.hpp"
#include "Domain/BatchDraft.hpp"
#include "Domain/RunPlan.hpp"
#include <optional>
#include <string>
#include <vector>
#include "Core/Diagnostics/StructuredError.hpp"
namespace SST::ScapsLab {
namespace Library {struct Request;}
using Json = nlohmann::json;
struct AcResult;
class ILabStore {
public:
    virtual ~ILabStore() = default;
    virtual void LibraryTyped(std::shared_ptr<const Library::Request>) {throw std::logic_error("Asset library unavailable");}
    virtual void BoundsFileTyped(BoundsFileAction) {throw std::logic_error("Parameter bounds unavailable");}
    // queries
    virtual bool Busy() const = 0;                                   // any device lane or the project lane has work
    virtual bool AnyActive() const = 0;                              // a job runs on some device
    virtual bool ReplacingProject() const = 0;                       // the active device runs load/project_load/layer
    virtual bool DeviceRunning(const std::string &device) const = 0; // that device has a running job
    virtual void RequireDevice(const std::string &device) const = 0; // throws std::invalid_argument for an unknown id
    virtual std::vector<std::string> ActiveOps() const = 0;          // operation names of the running jobs
    virtual bool EditQueued() const = 0;                             // some lane has a queued edit
    // actions
    virtual void ExportDlcpTyped(std::shared_ptr<const AcResult>,const std::string&) {throw std::logic_error("DLCP export unavailable");}
    virtual void StartJob(Json request) = 0;                         // job on the active device's lane (request carries "operation")
    virtual void StartSave(const Json &request) = 0;                 // own save lane, coalesced
    virtual bool CancelAll() = 0;                                    // true when something was running
    virtual void CancelDevice(const std::string &device) = 0;
    virtual void DeferProjectLoad(const Json &request) = 0;          // cancel calculations, open the project when the lanes are idle
    virtual void BatchTyped(std::shared_ptr<const BatchDraft>,std::shared_ptr<const RecorderDraft>){throw std::logic_error("Typed batch settings unavailable");}
    virtual void QuickSweepTyped(std::shared_ptr<const BatchDraft>,std::shared_ptr<const RecorderDraft>,std::shared_ptr<const ProjectDraft>,std::shared_ptr<const RunPlan> = {}){throw std::logic_error("Typed quick batch unavailable");}
    virtual void RunBatchTyped(std::shared_ptr<const BatchDraft>,std::shared_ptr<const RecorderDraft>,std::shared_ptr<const RunPlan>){throw std::logic_error("Typed batch unavailable");}
    virtual void EditTyped(std::shared_ptr<const ProjectDraft>,const std::string&) {throw std::logic_error("Typed editing unavailable");}
    virtual void SetRunPlanTyped(std::shared_ptr<const RunPlan>,const std::string& gesture={}) {throw std::logic_error("Run Actions unavailable");}
    virtual void RunDlcpTyped(std::optional<RunAction>) {throw std::logic_error("DLCP unavailable");}
    virtual void SingleShotTyped(std::shared_ptr<const RunPlan>) {throw std::logic_error("Single shot unavailable");}
    virtual bool CanUpdateSingleShot() const {return false;}
    virtual void UpdateSingleShotTyped() {throw std::logic_error("Single shot update unavailable");}
    virtual void CaptureLiveTyped(ActionKind) {throw std::logic_error("Capture live unavailable");}
    virtual void RecalculateLive(ActionKind) {throw std::logic_error("Live recalculation unavailable");}
    virtual void SelectBias(int) {throw std::logic_error("Bias selection unavailable");}
    virtual void SetLivePreviewStride(int) {throw std::logic_error("Live preview settings unavailable");}
    virtual void SetLiveEditing(bool) {throw std::logic_error("Live editing unavailable");}
    virtual void RecalculateStoredBias() {throw std::logic_error("Stored bias unavailable");}
    virtual void ResumeActions() {throw std::logic_error("No paused Actions");}
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
