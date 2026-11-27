#include <gtest/gtest.h>
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobSystemConfig.hpp"
#include "Core/App/Managers/ConfigManager.hpp"
#include "Core/App/Serialization/YamlCodecFacade.hpp"
#define NOMINMAX
#include <Windows.h>
#undef GetJob

namespace SST {
namespace {
class SchedulingJob final : public IJob {
public:
    explicit SchedulingJob(std::function<void(JobContext&)> body) : body(std::move(body)) {}
    std::string GetName() const override { return "Scheduling test"; }
    std::string GetType() const override { return "test.scheduling"; }
    void Execute(JobContext& context) override { body(context); }
private:
    std::function<void(JobContext&)> body;
};
template<class Predicate> bool Until(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return ready();
}
PROCESS_POWER_THROTTLING_STATE ProcessPower() {
    PROCESS_POWER_THROTTLING_STATE state{PROCESS_POWER_THROTTLING_CURRENT_VERSION, 0, 0};
    EXPECT_TRUE(GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))) << GetLastError();
    return state;
}
void ExpectHighPower() {
    const auto state = ProcessPower();
    EXPECT_EQ(state.ControlMask, PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION);
    EXPECT_EQ(state.StateMask, 0u);
    THREAD_POWER_THROTTLING_STATE thread{THREAD_POWER_THROTTLING_CURRENT_VERSION, 0, 0};
    EXPECT_TRUE(GetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &thread, sizeof(thread))) << GetLastError();
    EXPECT_EQ(thread.ControlMask, THREAD_POWER_THROTTLING_EXECUTION_SPEED);
    EXPECT_EQ(thread.StateMask, 0u);
}
}

TEST(JobScheduling, HardwareDefaultAndExplicitOverride) {
    const auto cpus = std::thread::hardware_concurrency();
    const auto expected = cpus > 1 ? cpus - 1 : 1;
    EXPECT_EQ(JobsConfig{}.defaultWorkerThreadCount, expected);
    EXPECT_EQ(ConfigManager::CreateDefaultConfig(Path("config")).jobs.defaultWorkerThreadCount, expected);
    auto config = ConfigManager::CreateDefaultConfig(Path("config"));
    std::string error;
    ASSERT_TRUE(YamlCodecFacade::Default().DeserializeUserSettings("jobs: {default_worker_threads: 4, reserve_urgent_worker: false}", config, error)) << error;
    EXPECT_EQ(config.jobs.defaultWorkerThreadCount, 4);
    EXPECT_FALSE(config.jobs.reserveUrgentWorker);
    JobSystem automatic;
    EXPECT_EQ(automatic.GetThreadCount(), expected);
    JobSystem override({}, 2, true);
    EXPECT_EQ(override.GetThreadCount(), 2u);
}

TEST(JobScheduling, ReservedWorkerRunsUrgentWorkWhileComputePoolIsFull) {
    JobSystem jobs({}, 2, true);
    auto release = std::make_shared<std::atomic_bool>(false);
    std::atomic<int> started{0};
    for (int i = 0; i < 2; ++i)
        (void)jobs.Submit(CreateRef<SchedulingJob>([&, release](JobContext& context) {
            EXPECT_EQ(context.GetWorkerCount(), 2u);
            ++started;
            Until([&] { return release->load(); });
        }));
    const bool full = Until([&] { return started == 2; });
    std::atomic_bool urgent{false};
    (void)jobs.Submit(CreateRef<SchedulingJob>([&](JobContext&) { urgent = true; }), JobPriority::Highest);
    const bool responsive = Until([&] { return urgent.load(); });
    *release = true;
    jobs.Shutdown();
    EXPECT_TRUE(full);
    EXPECT_TRUE(responsive);
}

TEST(JobScheduling, PauseAndLiveWorkerSettingsApplyToBothQueues) {
    JobSystem jobs({}, 1, true);
    ASSERT_TRUE(jobs.PauseAllJobs());
    std::atomic<int> completed{0};
    (void)jobs.Submit(CreateRef<SchedulingJob>([&](JobContext&) { ++completed; }));
    (void)jobs.Submit(CreateRef<SchedulingJob>([&](JobContext&) { ++completed; }), JobPriority::High);
    EXPECT_EQ(completed, 0);
    ASSERT_TRUE(jobs.ResumeAllJobs());
    EXPECT_TRUE(Until([&] { return completed == 2; }));
    jobs.SetReserveUrgentWorker(false);
    ASSERT_TRUE(jobs.SetThreadCount(3));
    EXPECT_TRUE(Until([&] { return jobs.GetThreadCount() == 3; }));
    jobs.SetReserveUrgentWorker(true);
    jobs.Shutdown();
}

TEST(JobScheduling, PowerPolicyCoversOverlappingSystemsAndRestoresIdleDefaults) {
    const auto inheritedPriority = GetPriorityClass(GetCurrentProcess());
    JobSystem first({}, 1), second({}, 1);
    auto releaseFirst = std::make_shared<std::atomic_bool>(false);
    auto releaseSecond = std::make_shared<std::atomic_bool>(false);
    std::atomic<int> started{0};
    std::atomic<DWORD> workerId{0};
    (void)first.Submit(CreateRef<SchedulingJob>([&, releaseFirst](JobContext&) {
        ExpectHighPower(); workerId = GetCurrentThreadId(); ++started;
        Until([&] { return releaseFirst->load(); });
    }));
    (void)second.Submit(CreateRef<SchedulingJob>([&, releaseSecond](JobContext&) {
        ExpectHighPower(); ++started;
        Until([&] { return releaseSecond->load(); });
    }));
    const bool overlapping = Until([&] { return started == 2; });
    const auto active = ProcessPower();
    *releaseFirst = true;
    first.Shutdown();
    const auto stillActive = ProcessPower();
    THREAD_POWER_THROTTLING_STATE idleThread{THREAD_POWER_THROTTLING_CURRENT_VERSION, 0, 0};
    const auto handle = OpenThread(THREAD_QUERY_INFORMATION, FALSE, workerId);
    if (handle) {
        EXPECT_TRUE(GetThreadInformation(handle, ThreadPowerThrottling, &idleThread, sizeof(idleThread)));
        CloseHandle(handle);
    }
    *releaseSecond = true;
    second.Shutdown();
    EXPECT_TRUE(overlapping);
    EXPECT_NE(handle, nullptr);
    EXPECT_EQ(idleThread.ControlMask, 0u);
    EXPECT_EQ(active.ControlMask, 5u);
    EXPECT_EQ(stillActive.ControlMask, 5u);
    EXPECT_EQ(ProcessPower().ControlMask, 0u);
    EXPECT_EQ(GetPriorityClass(GetCurrentProcess()), inheritedPriority);
}

TEST(JobScheduling, LatestWorkerRequestCanRestoreCountWhileResizeWaitsForWork) {
    for (const bool safe : {false, true}) {
    JobSystem jobs({}, 2);
    auto release = std::make_shared<std::atomic_bool>(false);
    std::atomic_bool started{false};
    (void)jobs.Submit(CreateRef<SchedulingJob>([&, release](JobContext&) {
        started = true;
        Until([&] { return release->load(); });
    }));
    const bool running = Until([&] { return started.load(); });
    EXPECT_TRUE(jobs.SetThreadCount(1));
    std::string message;
    EXPECT_TRUE(safe ? jobs.SafeSetThreadCount(2, message) : jobs.SetThreadCount(2));
    *release = true;
    // Let the resize waiting on the running job finish before checking the final request.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(running);
    EXPECT_TRUE(Until([&] { return jobs.GetThreadCount() == 2; }));
    jobs.Shutdown();
    }
}

TEST(JobScheduling, NestedFailureAndCancellationRestorePowerPolicy) {
    for (int outcome = 0; outcome < 3; ++outcome) {
        JobSystem jobs({}, 1, true);
        const auto id = jobs.Submit(CreateRef<SchedulingJob>([&](JobContext& context) {
            ExpectHighPower();
            auto child = CreateRef<SchedulingJob>([](JobContext&) { ExpectHighPower(); });
            EXPECT_NE(context.SubmitJobSequential(child), 0u);
            ExpectHighPower();
            if (outcome == 1) throw std::runtime_error("expected failure");
            if (outcome == 2) throw JobCancelledException();
        }));
        EXPECT_TRUE(Until([&] {
            const auto status = jobs.GetJob(id)->status;
            return status == JobStatus::Completed || status == JobStatus::Failed || status == JobStatus::Cancelled;
        }));
        jobs.Shutdown();
        EXPECT_EQ(jobs.GetJob(id)->status, outcome == 0 ? JobStatus::Completed : outcome == 1 ? JobStatus::Failed : JobStatus::Cancelled);
        EXPECT_EQ(ProcessPower().ControlMask, 0u);
    }
}
}
