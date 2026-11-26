#include <gtest/gtest.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/ParallelFor.hpp"
#include "ScientificRuntime/Python/PythonScriptJob.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"

namespace SST {
namespace {
bool PoolWait(JobSystem& jobs,JobId id) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline){const auto s=jobs.GetJob(id);if(s && (s->status==JobStatus::Completed || s->status==JobStatus::Failed || s->status==JobStatus::Cancelled))return s->status==JobStatus::Completed;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;
}
class PoolJob final:public IJob {
public:
    std::function<void(JobContext&)> fn;
    explicit PoolJob(std::function<void(JobContext&)> fn):fn(std::move(fn)){}
    std::string GetName()const override{return "Pool test";}std::string GetType()const override{return "test.pool";}
    void Execute(JobContext& ctx)override{fn(ctx);}
};
}
TEST(CorePool, OneTwoAndTwentyFourWorkersDrainNestedWork) {
    for(const auto workers:{1u,2u,24u}) {
        JobSystem jobs({},workers);std::atomic<int> done=0;
        std::vector<Ref<PoolJob>> parents;std::vector<JobId> ids;
        for(unsigned p=0;p<workers;++p){auto job=CreateRef<PoolJob>([&](JobContext& ctx){
            auto child=CreateRef<PoolJob>([&](JobContext&){++done;});EXPECT_NE(ctx.SubmitJobSequential(child),0u);
            EXPECT_TRUE(ParallelFor(ctx,17,[&](std::size_t){++done;},0,3));
        });parents.push_back(job);ids.push_back(jobs.Submit(job));}
        for(const auto id:ids)ASSERT_TRUE(PoolWait(jobs,id));EXPECT_EQ(done,int(workers)*18);
    }
}
TEST(CorePool, NestedBudgetAndFinalProgress) {
    std::atomic<int> done=0;int reports=0;float final=0;
    JobContext context([&](float n,float){++reports;final=n;},{},{},{},[]{return false;},{},{},8);
    EXPECT_TRUE(ParallelFor(context,4,[&](std::size_t){EXPECT_EQ(ParallelWorkerBudget,2u);EXPECT_TRUE(ParallelFor(context,100,[&](std::size_t){EXPECT_EQ(ParallelWorkerBudget,1u);++done;}));}));
    EXPECT_EQ(done,400);EXPECT_FLOAT_EQ(final,4.f);EXPECT_LT(reports,30);
}
TEST(CorePool, FailureAndCancellationDrainCapturedReferences) {
    JobSystem jobs({},4);std::atomic<int> running=0;std::atomic<bool> caught=false;
    auto job=CreateRef<PoolJob>([&](JobContext& ctx){
        try{(void)ParallelFor(ctx,100,[&](std::size_t i){if(i==0)throw std::logic_error("expected");++running;std::this_thread::sleep_for(std::chrono::milliseconds(2));--running;});}catch(const std::logic_error&){caught=true;}
        EXPECT_EQ(running,0);
    });ASSERT_TRUE(PoolWait(jobs,jobs.Submit(job)));EXPECT_TRUE(caught);
    std::stop_source stop;stop.request_stop();JobContext cancelled({},{},{},{},[]{return false;},{},{},4,stop.get_token());
    EXPECT_FALSE(ParallelFor(cancelled,100,[&](std::size_t){++running;},0,8,stop.get_token()));EXPECT_EQ(running,0);
}
TEST(CorePool, SubprocessDoesNotAcquireEmbeddedPythonGuard) {
    PythonScriptJob script("subprocess",Path("unused.py"));EXPECT_FALSE(script.UsesPythonRuntime());
}
TEST(CorePool, TerminalStatusAndReadableFailureReachProgressTracker) {
    for (int outcome=0; outcome<4; ++outcome) {
        auto bus=CreateRef<EventBus>(); ProgressTracker tracker(bus); JobSystem jobs(bus,1);
        auto job=CreateRef<PoolJob>([outcome](JobContext& context) {
            context.SetProgress(1,4);
            if(outcome==1)throw std::runtime_error("Solver failed");
            if(outcome==2)throw JobCancelledException("Cancelled by user");
            if(outcome==3)throw std::invalid_argument("Rejected: memory budget");
        });
        const auto id=jobs.Submit(job); (void)PoolWait(jobs,id); jobs.Shutdown(); bus->ProcessQueue();
        const JobStatus expected=outcome==0?JobStatus::Completed:outcome==2?JobStatus::Cancelled:JobStatus::Failed;
        const auto status=jobs.GetJob(id); ASSERT_TRUE(status); EXPECT_EQ(status->status,expected);
        const auto progress=tracker.GetSnapshot(id); ASSERT_TRUE(progress); EXPECT_EQ(progress->status,expected); EXPECT_TRUE(progress->finished);
        if(outcome==1 || outcome==3) {
            EXPECT_FALSE(status->errorMessage.empty()); EXPECT_EQ(progress->errorSummary,status->errorMessage);
            EXPECT_LT(progress->completedWork,progress->totalWork);
        }
    }
}
} // namespace SST

TEST(CorePool, ExternalStopLeavesErrorHandlingToCaller) {
    SST::JobContext context({},{},{},{},[]{return false;},{},{},4);
    std::stop_source stop;
    SST::JobExecutor executor{context};
    EXPECT_NO_THROW(executor(8,1,stop.get_token(),[&](std::size_t){stop.request_stop();}));
}
