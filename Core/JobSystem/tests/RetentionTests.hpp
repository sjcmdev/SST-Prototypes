#pragma once
#include <gtest/gtest.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include <chrono>
#include <thread>
namespace SST::RetentionTests {
class PayloadJob final : public IJob {
public:
    Ref<const std::vector<double>> output;
    bool cancel=false;
    int calls=0;
    std::string GetName() const override {return "retention";}
    std::string GetType() const override {return "test.retention";}
    void Execute(JobContext &ctx) override {
        ++calls;
        if(cancel)throw JobCancelledException();
        output=CreateRef<const std::vector<double>>(1024*1024,42.);
        for(int i=0;i<300;++i)ctx.LogInfo(std::string(8192,'x'));
    }
};
inline bool Finished(JobSystem &jobs,JobId id) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    do {
        const auto s=jobs.GetJob(id);
        if(s && s->status!=JobStatus::Queued && s->status!=JobStatus::Running)return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<deadline);
    return false;
}
TEST(JobRetention, PayloadExpiresAfterConsumerAdoptsTypedResult) {
    JobSystem jobs(nullptr,2);
    for(int i=0;i<20;++i) {
        auto job=CreateRef<PayloadJob>(); const WeakRef<IJob> weak=job;
        const auto id=jobs.Submit(job); ASSERT_TRUE(Finished(jobs,id));
        const auto result=job->output; job.reset();
        for(int spin=0;spin<1000 && !weak.expired();++spin)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ASSERT_TRUE(weak.expired()); EXPECT_EQ(result->front(),42.); EXPECT_EQ(result->size(),1024*1024);
        std::size_t bytes=0; for(const auto &log:jobs.GetLogs(id))bytes+=log.message.size();
        EXPECT_LE(bytes,64*1024); EXPECT_LE(jobs.GetLogs(id).size(),256);
        EXPECT_FALSE(jobs.Retry(id)); EXPECT_FALSE(jobs.Reset(id));
    }
}
TEST(JobRetention, RetryAndResumeUseConsumerOwnedRequest) {
    JobSystem jobs(nullptr,1); auto job=CreateRef<PayloadJob>(); job->cancel=true;
    const auto id=jobs.Submit(job); ASSERT_TRUE(Finished(jobs,id));
    job->cancel=false; ASSERT_TRUE(jobs.Resume(id)); ASSERT_TRUE(Finished(jobs,id));
    ASSERT_TRUE(jobs.Retry(id)); ASSERT_TRUE(Finished(jobs,id)); EXPECT_EQ(job->calls,3);
}
class SmallJob : public IJob {
public:
    std::string GetName() const override {return "small";}
    std::string GetType() const override {return "test.small";}
    void Execute(JobContext &) override {}
};
TEST(JobRetention, ConsumerCanAdoptAfterHistoryWraps) {
    JobSystem jobs(nullptr,1); auto held=CreateRef<PayloadJob>();
    const auto heldId=jobs.Submit(held); ASSERT_TRUE(Finished(jobs,heldId));
    for(std::size_t i=0;i<JobHistoryLimit+20;++i) {
        const auto id=jobs.Submit(CreateRef<SmallJob>()); ASSERT_TRUE(Finished(jobs,id));
    }
    ASSERT_TRUE(jobs.GetJob(heldId)); EXPECT_EQ(jobs.GetJob(heldId)->status,JobStatus::Completed);
    const auto result=held->output; jobs.Shutdown(); held.reset();
    EXPECT_EQ(result->front(),42.); EXPECT_LE(jobs.GetAllJobs().size(),JobHistoryLimit);
}
TEST(JobRetention, TextBudgetPreservesUtf8AtTheTailBoundary) {
    class NamedJob final : public SmallJob {
    public:
        std::string GetName() const override {std::string name;for(int i=0;i<3000;++i)name+="€";return name;}
    };
    std::string expected="[truncated] ";for(int i=0;i<1361;++i)expected+="€";
    JobSystem jobs(nullptr,1);const auto id=jobs.Submit(CreateRef<NamedJob>());ASSERT_TRUE(Finished(jobs,id));
    EXPECT_EQ(jobs.GetJob(id)->name,expected);EXPECT_LE(jobs.GetJob(id)->name.size(),JobTextLimit);
}
TEST(JobRetention, ParentWaitsForRetiredChildWithoutUnboundedHistory) {
    class Parent final : public IJob {
        class Child final : public SmallJob {
        public:
            std::atomic<int> &done; Child(std::atomic<int> &done):done(done){}
            void Execute(JobContext &) override {++done;}
        };
    public:
        std::atomic<int> done{0}; bool waited=false; JobId first=0;
        std::string GetName() const override {return "parent";}
        std::string GetType() const override {return "test.parent";}
        void Execute(JobContext &ctx) override {
            first=ctx.SubmitJob(CreateRef<Child>(done));
            if(!ctx.WaitForJob(first))return;
            for(int i=0;i<300;++i)(void)ctx.SubmitJob(CreateRef<Child>(done));
            while(done<301)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            waited=ctx.WaitForJob(first);
        }
    };
    JobSystem jobs(nullptr,2);auto parent=CreateRef<Parent>();
    const auto id=jobs.Submit(parent);ASSERT_TRUE(Finished(jobs,id));EXPECT_TRUE(parent->waited);
    EXPECT_FALSE(jobs.GetJob(parent->first));
    EXPECT_LE(jobs.GetAllJobs().size(),JobHistoryLimit+2);
    parent.reset();jobs.Shutdown();EXPECT_LE(jobs.GetAllJobs().size(),JobHistoryLimit);EXPECT_TRUE(jobs.GetJob(id));
}
TEST(JobRetention, FinishedHistoryAndProgressAreBounded) {
    auto bus=CreateRef<EventBus>(); ProgressTracker tracker(bus); JobSystem jobs(bus,1);
    for(std::size_t i=0;i<JobHistoryLimit+20;++i) {
        const auto id=jobs.Submit(CreateRef<SmallJob>()); ASSERT_TRUE(Finished(jobs,id)); bus->ProcessQueue();
    }
    jobs.Shutdown(); bus->ProcessQueue();
    EXPECT_LE(jobs.GetAllJobs().size(),JobHistoryLimit);
    const auto entries=tracker.GetAllSnapshots(); ASSERT_TRUE(entries); EXPECT_LE(entries->size(),JobHistoryLimit);
}
}
