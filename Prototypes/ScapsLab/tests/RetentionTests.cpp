#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
using namespace SST;
using namespace SST::ScapsLab;
TEST(PerfRetain, TwentySweep5Batches) {
    if (!std::getenv("SST_MEMORY_PROBE")) GTEST_SKIP();
    Workflow w(CreateRef<JobSystem>(nullptr, 23));
    w.lightResponse=true; w.autoRecalculate=false; w.autoRecalculateBand=false;
    auto wait=[&] {while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path","D:/SJCM/01_SemiconductorStudio/tools/agents/runs/slbench1/device/sweep5.scapslab"}}).at("ok").get<bool>()); wait();
    ASSERT_TRUE(w.response.value("ok",false));
    for(const auto &d:w.project.devices) if(d->name=="light") w.project.active_device=d->id;
    std::size_t tenth=0;
    for(int i=0;i<20;++i) {
        ASSERT_TRUE(w.Dispatch({{"operation","run_batch"}}).at("ok").get<bool>()); wait();
        ASSERT_TRUE(w.response.value("ok",false))<<w.status;
        PROCESS_MEMORY_COUNTERS_EX m{}; m.cb=sizeof(m);
        ASSERT_TRUE(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)));
        std::printf("RETAIN sweep5 iteration=%d private_bytes=%zu peak_commit_bytes=%zu jobs=%zu\n",i+1,m.PrivateUsage,m.PeakPagefileUsage,w.jobs->GetAllJobs().size()); std::fflush(stdout);
        if(i==9)tenth=m.PrivateUsage;
        if(i==19)EXPECT_LE(m.PrivateUsage,tenth+64*1024*1024);
    }
}
