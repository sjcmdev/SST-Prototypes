#pragma once
#include "Core/Layer.hpp"
#include "Core/IO/ProjectRecovery.hpp"
#include "Core/JobSystem/JobSystem.hpp"

namespace SST {
class ProjectRecoveryLayer final : public Layer {
public:
    using Checkpoint = std::function<void(ProjectRecoveryPlan&)>;
    using Restore = std::function<void(const RecoveryCandidate&)>;
    ProjectRecoveryLayer(Path root, Ref<JobSystem> jobs, Checkpoint checkpoint, Restore restore);
    ~ProjectRecoveryLayer() override { (void)finish(true); }
    void OnUpdate(float seconds) override;
    void OnImGuiRender() override;
    void OnDetach() override;
private:
    bool save(bool wait=false);
    bool finish(bool wait);
    ProjectRecovery recovery;
    Checkpoint checkpoint;
    Restore restore;
    std::vector<RecoveryCandidate> candidates;
    std::string error;
    float elapsed=30;
    bool show=true;
    Ref<JobSystem> jobs;
    Ref<IJob> pending;
    JobId active=0;
};
}
