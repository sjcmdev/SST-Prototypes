#include <gtest/gtest.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/ParallelFor.hpp"
#include <future>
#include "ScientificRuntime/Python/PythonScriptJob.hpp"
#include "ScientificRuntime/Python/PythonGilScope.hpp"
#include "ScientificRuntime/Python/PythonInterpreter.hpp"

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
#if DS_PYTHON_CAPI_AVAILABLE
TEST(CorePool, ReleasedGilAllowsAnotherPythonSection) {
    if(!PythonInterpreter::IsRuntimeInitialized())GTEST_SKIP()<<"Embedding runtime belongs to the application fixture.";
    std::promise<void> released,acquired;auto ready=released.get_future();auto entered=acquired.get_future();std::atomic<bool> overlapped=false;
    std::thread first([&]{PythonGilAcquireScope gil;auto* state=PyEval_SaveThread();released.set_value();overlapped=entered.wait_for(std::chrono::seconds(2))==std::future_status::ready;PyEval_RestoreThread(state);});
    std::thread second([&]{ready.wait();PythonGilAcquireScope gil;acquired.set_value();});
    first.join();second.join();EXPECT_TRUE(overlapped);
}
#endif
} // namespace SST
