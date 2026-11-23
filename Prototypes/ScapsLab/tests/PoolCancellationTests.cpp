#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Core/JobSystem/ParallelFor.hpp"
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
class StopAtEngine final : public IJob {
public:
    JobSystem& jobs;
    bool direct;
    Operation operation;
    std::atomic<bool> reached{false};
    StopAtEngine(JobSystem& pool,bool batch,bool direct):jobs(pool),direct(direct),operation([]{
        auto project=scaps_core::make_default_device_project();project.mesh.points_per_layer=11;
        project.voltage={0,.01,.01};for(auto& layer:project.layers)layer.defects.clear();return DraftFromProject(std::move(project));
    }(),{{"operation",batch?"run_batch":"single_shot"}}) {
        auto plan=CreateRef<RunPlan>();
        if(batch){for(auto& a:plan->actions)a.include=false;plan->actions[3].include=true;operation.batch=ReadBatchDraft(BatchDefaults());operation.recorder=ReadRecorderDraft(RecorderDefaults());}
        operation.runPlan=std::move(plan);operation.jsonResponse=false;
    }
    std::string GetName() const override{return "Cancel inside engine";}
    std::string GetType() const override{return "test.cancel_engine";}
    void Execute(JobContext& ctx) override {
        JobContext forwarded(
            [&](float n,float total){ctx.SetProgress(n,total);},
            [&](const std::string& stage){
                ctx.SetStage(stage);
                if(stage=="Equilibrium" || stage=="DC operating points") {
                    reached=true;
                    if(direct)operation.cancellation.request_stop();
                    else for(const auto& job:jobs.GetActiveJobs())if(job.type==GetType())EXPECT_TRUE(jobs.RequestCancel(job.id));
                }
            },{},{},[]{return false;},
            [&](const Ref<IJob>& job,JobPriority priority){return ctx.SubmitJob(job,priority);},
            [&](JobId id){return ctx.WaitForJob(id);},ctx.GetWorkerCount(),ctx.GetStopToken());
        // The query deliberately stays false: only the engine's stop token can interrupt these solves.
        operation.Execute(forwarded);
    }
};
}
TEST(CorePool,SingleShotAndBatchTranslateEngineCancellationToCancelled) {
    for(const auto workers:{1u,4u})for(bool batch:{false,true})for(bool direct:{false,true}) {
        SCOPED_TRACE(workers);
        SCOPED_TRACE(batch);
        SCOPED_TRACE(direct);JobSystem jobs({},workers);
        auto job=CreateRef<StopAtEngine>(jobs,batch,direct);const auto id=jobs.Submit(job);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(std::chrono::steady_clock::now()<deadline){const auto state=jobs.TryGetStatus(id);if(state && (*state==JobStatus::Completed || *state==JobStatus::Failed || *state==JobStatus::Cancelled))break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ASSERT_EQ(jobs.TryGetStatus(id),JobStatus::Cancelled);EXPECT_TRUE(job->reached.load());EXPECT_FALSE(job->operation.outputValid);
        if(batch){ASSERT_TRUE(job->operation.actionBatch);bool cancelled=false;for(const auto& row:job->operation.actionBatch->rows){EXPECT_NE(row->status,"Failed");cancelled|=row->status=="Cancelled";}EXPECT_TRUE(cancelled);}
    }
}
