// S6: per-device job lanes (serial inside a lane, parallel across devices), coalescing, independent save lane; Core JobSystem::TryGetStatus.
#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
// Holds every calculation Operation inside the pre-execute hook until released: proves what runs at the same time without timing luck.
struct Gate final {
    std::atomic<bool> armed{false}, release{false};
    std::atomic<int> entered{0};
    std::mutex lock;std::vector<std::string> order;
    explicit Gate(JobSystem &jobs) {
        jobs.RegisterPreExecuteHook([this](const IJob &job) -> Unique<IJobExecutionGuard> {
            const auto *op=dynamic_cast<const Operation*>(&job);
            if(!op || op->request.value("operation",std::string{})=="project_save") return nullptr;
            {std::lock_guard g(lock);order.push_back(op->request.value("operation",std::string{})+(op->request.contains("tag")?std::to_string(op->request.at("tag").get<int>()):std::string{}));}
            if(!armed) return nullptr;
            ++entered;
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(!release && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return nullptr;
        });
    }
};
template<class P> bool Pump(Workflow &w,P done,int ms=20000) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    while(!done()) {if(std::chrono::steady_clock::now()>until) return false;w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return true;
}
void Idle(Workflow &w) {ASSERT_TRUE(Pump(w,[&]{return !w.Pending();}));}
struct TwoDevices {
    Workflow w{CreateRef<JobSystem>(nullptr,4)};Gate gate{*w.jobs};std::string a,b;
    TwoDevices() {
        w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);a=w.project.active_device;
        EXPECT_TRUE(w.Dispatch({{"operation","device_duplicate"},{"name","Second"}}).at("ok").get<bool>());Idle(w);b=w.project.active_device;
        w.project.compare_set={a,b};
    }
};
}
TEST(Lanes,TwoDevicesRunAtTheSameTime) {
    TwoDevices t;t.gate.armed=true;
    ASSERT_TRUE(t.w.Dispatch({{"operation","run_all"}}).at("ok").get<bool>());
    EXPECT_NE(t.w.LaneJob(t.a),0u);EXPECT_NE(t.w.LaneJob(t.b),0u);EXPECT_NE(t.w.LaneJob(t.a),t.w.LaneJob(t.b));
    ASSERT_TRUE(Pump(t.w,[&]{return t.gate.entered==2;},10000))<<"both device lanes must be inside their job together (serial execution would never reach 2)";
    t.gate.release=true;Idle(t.w);
    EXPECT_EQ(t.w.response.at("runs").size(),2u);EXPECT_TRUE(t.w.response.at("ok").get<bool>())<<t.w.response.dump();
    EXPECT_FALSE(t.w.Device(t.a).result.empty());EXPECT_FALSE(t.w.Device(t.b).result.empty());
}
TEST(Lanes,CancellingOneDeviceLeavesTheOtherRunning) {
    TwoDevices t;t.gate.armed=true;
    ASSERT_TRUE(t.w.Dispatch({{"operation","run_all"}}).at("ok").get<bool>());
    ASSERT_TRUE(Pump(t.w,[&]{return t.gate.entered==2;},10000));
    const auto ja=t.w.LaneJob(t.a),jb=t.w.LaneJob(t.b);
    ASSERT_TRUE(t.w.Dispatch({{"operation","cancel"},{"device_id",t.a}}).at("ok").get<bool>());
    t.gate.release=true;Idle(t.w);
    EXPECT_EQ(t.w.jobs->GetJob(ja)->status,JobStatus::Cancelled);
    EXPECT_EQ(t.w.jobs->GetJob(jb)->status,JobStatus::Completed);
    EXPECT_TRUE(t.w.Device(t.a).result.empty());EXPECT_FALSE(t.w.Device(t.b).result.empty());
}
TEST(Lanes,PerDeviceOrderEditEditRunIsKept) {
    TwoDevices t;auto d1=t.w.Device(t.a).document,d2=d1;d1["temperature_K"]=301;d2["temperature_K"]=302;
    ASSERT_TRUE(t.w.jobs->PauseAllJobs());
    t.w.Submit(t.a,{{"operation","edit"},{"document",d1}});
    t.w.Submit(t.a,{{"operation","edit"},{"document",d2}});
    t.w.Submit(t.a,{{"operation","run"}});
    EXPECT_EQ(t.w.LaneOf(t.a).queue.size(),2u);EXPECT_FALSE(t.w.Busy(t.b));
    t.gate.order.clear();ASSERT_TRUE(t.w.jobs->ResumeAllJobs());Idle(t.w);
    ASSERT_EQ(t.gate.order.size(),3u);EXPECT_EQ(t.gate.order[0],"edit");EXPECT_EQ(t.gate.order[1],"edit");EXPECT_EQ(t.gate.order[2],"run");
    EXPECT_EQ(t.w.Device(t.a).document.at("temperature_K"),302);EXPECT_FALSE(t.w.Device(t.a).result.empty());
}
TEST(Lanes,NewestQueuedRequestOfAKeyWins) {
    TwoDevices t;auto d=t.w.Device(t.a).document;d["temperature_K"]=303;
    ASSERT_TRUE(t.w.jobs->PauseAllJobs());
    t.w.Submit(t.a,{{"operation","edit"},{"document",d}});
    t.w.Submit(t.a,{{"operation","run"},{"tag",1}},"auto");
    t.w.Submit(t.a,{{"operation","run"},{"tag",2}},"auto");
    t.w.Submit(t.a,{{"operation","run"},{"tag",3}},"auto");
    ASSERT_EQ(t.w.LaneOf(t.a).queue.size(),1u);
    t.gate.order.clear();ASSERT_TRUE(t.w.jobs->ResumeAllJobs());Idle(t.w);
    ASSERT_EQ(t.gate.order.size(),2u);EXPECT_EQ(t.gate.order[0],"edit");EXPECT_EQ(t.gate.order[1],"run3");
}
TEST(Lanes,SaveRunsBesideCalculations) {
    TwoDevices t;t.gate.armed=true;
    const auto file=std::filesystem::temp_directory_path()/"slm6-save-during-runs.scapslab";std::filesystem::remove(file);
    ASSERT_TRUE(t.w.Dispatch({{"operation","run_all"}}).at("ok").get<bool>());
    ASSERT_TRUE(Pump(t.w,[&]{return t.gate.entered==2;},10000));
    ASSERT_TRUE(t.w.Dispatch({{"operation","project_save"},{"path",file.string()}}).at("ok").get<bool>());
    EXPECT_TRUE(t.w.Saving());
    ASSERT_TRUE(Pump(t.w,[&]{return !t.w.Saving();},15000))<<"the save must finish while both calculations are still held";
    EXPECT_TRUE(std::filesystem::exists(file));EXPECT_NE(t.w.LaneJob(t.a),0u);EXPECT_NE(t.w.LaneJob(t.b),0u);
    t.gate.release=true;Idle(t.w);
    EXPECT_FALSE(t.w.Device(t.a).result.empty());EXPECT_FALSE(t.w.Device(t.b).result.empty());
    std::filesystem::remove(file);
}
namespace {
class Tiny final : public IJob {
public:
    std::string GetName() const override {return "tiny";}
    std::string GetType() const override {return "test.tiny";}
    void Execute(JobContext &) override {}
};
}
TEST(CoreJobs,TryGetStatusIsACheapStatusRead) {
    JobSystem jobs(nullptr,1);
    EXPECT_FALSE(jobs.TryGetStatus(987654).has_value());
    const auto id=jobs.Submit(CreateRef<Tiny>());
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<until) {const auto s=jobs.TryGetStatus(id);if(s && *s==JobStatus::Completed) break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(jobs.TryGetStatus(id).has_value());EXPECT_EQ(*jobs.TryGetStatus(id),JobStatus::Completed);
    EXPECT_EQ(jobs.GetJob(id)->status,*jobs.TryGetStatus(id));
}
