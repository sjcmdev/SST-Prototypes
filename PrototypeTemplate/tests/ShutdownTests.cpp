#include <gtest/gtest.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <chrono>
#include <future>
#include <thread>

namespace SST
{
    class ShutdownJob final : public IJob
    {
    public:
        std::promise<void> started;
        std::atomic_bool executed = false;
        std::string GetName() const override { return "Shutdown regression"; }
        std::string GetType() const override { return "test.shutdown"; }
        void Execute(JobContext &context) override
        {
            executed = true;
            started.set_value();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!context.IsCancellationRequested() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };

    TEST(ApplicationShutdown, CancelsRunningQueuedAndDelayedJobsBeforeJoining)
    {
        JobSystem jobs(nullptr, 1);
        auto running = CreateRef<ShutdownJob>();
        auto started = running->started.get_future();
        const auto runningId = jobs.Submit(running);
        ASSERT_EQ(started.wait_for(std::chrono::seconds(1)), std::future_status::ready);
        ASSERT_TRUE(jobs.PauseAllJobs());
        auto queued = CreateRef<ShutdownJob>();
        const auto queuedId = jobs.Submit(queued);
        const auto delayedId = jobs.SubmitAfter(CreateRef<ShutdownJob>(), Time::Milliseconds(60000));
        ASSERT_TRUE(jobs.SetThreadCount(2));
        const auto before = std::chrono::steady_clock::now();
        jobs.Shutdown(true);
        EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds(2));
        EXPECT_EQ(jobs.GetJob(runningId)->status, JobStatus::Cancelled);
        EXPECT_EQ(jobs.GetJob(queuedId)->status, JobStatus::Cancelled);
        EXPECT_EQ(jobs.GetJob(delayedId)->status, JobStatus::Cancelled);
        EXPECT_FALSE(queued->executed);
        EXPECT_EQ(jobs.Submit(CreateRef<ShutdownJob>()), 0);
        jobs.Shutdown(true);
    }

    TEST(ApplicationShutdown, IdleWorkersAlwaysWakeForShutdown)
    {
        const auto before = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i)
        {
            JobSystem jobs(nullptr, 1);
            jobs.Shutdown(true);
        }
        EXPECT_LT(std::chrono::steady_clock::now() - before, std::chrono::seconds(2));
    }
}
