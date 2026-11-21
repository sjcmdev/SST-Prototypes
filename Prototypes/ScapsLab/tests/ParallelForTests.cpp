// Core ParallelFor helper (Core/JobSystem/ParallelFor.hpp) tests.
#include <gtest/gtest.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <chrono>
#include <thread>
#include <vector>

#include "Core/JobSystem/ParallelFor.hpp"
namespace SST
{
    static bool PfWait(JobSystem &jobs, JobId id)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto j = jobs.GetJob(id);
            if (j && (j->status == JobStatus::Completed || j->status == JobStatus::Cancelled || j->status == JobStatus::Failed)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }

    class ParallelForJob final : public IJob
    {
    public:
        std::size_t n = 64;
        std::vector<int> out;
        std::atomic<std::size_t> maxConcurrent{0}, current{0};
        bool all = false;
        std::string GetName() const override { return "ParallelFor test"; }
        std::string GetType() const override { return "test.parallel_for"; }
        void Execute(JobContext &context) override
        {
            out.assign(n, 0);
            all = ParallelFor(context, n, [&](std::size_t i) {
                const auto c = ++current; auto m = maxConcurrent.load();
                while (c > m && !maxConcurrent.compare_exchange_weak(m, c)) {}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                out[i] = int(i) + 1; --current;
            });
        }
    };

    TEST(ParallelForTest, SingleWorkerDoesNotDeadlockAndFillsEverySlot)
    {
        JobSystem jobs(nullptr, 1);
        auto job = CreateRef<ParallelForJob>();
        const auto id = jobs.Submit(job);
        ASSERT_TRUE(PfWait(jobs, id));
        ASSERT_TRUE(job->all);
        for (std::size_t i = 0; i < job->n; ++i) ASSERT_EQ(job->out[i], int(i) + 1);
        EXPECT_EQ(job->maxConcurrent.load(), 1u);
    }

    TEST(ParallelForTest, FourWorkersRunConcurrentlyWithSameResult)
    {
        JobSystem jobs(nullptr, 4);
        auto job = CreateRef<ParallelForJob>();
        const auto id = jobs.Submit(job);
        ASSERT_TRUE(PfWait(jobs, id));
        ASSERT_TRUE(job->all);
        for (std::size_t i = 0; i < job->n; ++i) ASSERT_EQ(job->out[i], int(i) + 1);
        EXPECT_GT(job->maxConcurrent.load(), 1u);
    }

    class ParallelForCancelJob final : public IJob
    {
    public:
        bool all = true; std::atomic<int> ran{0};
        std::string GetName() const override { return "ParallelFor cancel"; }
        std::string GetType() const override { return "test.parallel_for_cancel"; }
        void Execute(JobContext &context) override
        {
            all = ParallelFor(context, 1000, [&](std::size_t) { ++ran; std::this_thread::sleep_for(std::chrono::milliseconds(5)); });
        }
    };

    TEST(ParallelForTest, CancelSkipsRemainingIndices)
    {
        JobSystem jobs(nullptr, 3);
        auto job = CreateRef<ParallelForCancelJob>();
        const auto id = jobs.Submit(job);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        (void)jobs.RequestCancel(id);
        ASSERT_TRUE(PfWait(jobs, id));
        EXPECT_FALSE(job->all);
        EXPECT_LT(job->ran.load(), 1000);
    }
}

#include "Workflow.hpp"
#include <thread>
namespace {
using namespace SST::ScapsLab;
Json RunBatchRows(std::size_t workers) {
    using namespace SST::ScapsLab;
    Workflow w(SST::CreateRef<SST::JobSystem>(nullptr,workers));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    auto spec=BatchDefaults();spec["axes"][0]["count"]=5;
    (void)w.Dispatch({{"operation","run_batch"},{"spec",spec}});
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    auto rows=w.Current().batchResult.at("rows");for(auto &r:rows)r.erase("elapsed_s");return rows;
}
}
TEST(ParallelForTest,BatchResultEqualsSerialAndOneWorkerDoesNotDeadlock) {
    const auto serial=RunBatchRows(1),parallel=RunBatchRows(4);
    ASSERT_EQ(serial.size(),5u);EXPECT_EQ(serial,parallel);
}
