// Performance probe (UI-thread cost of Dispatch/OnUpdate). Run: ScapsLabTests --gtest_filter=PerfProbe.*
#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <scaps_core/api.hpp>
#include <scaps_core/batch.hpp>
#include <scaps_core/device_project_def.hpp>
#include <scaps_core/device_project_yaml.hpp>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
using Clock=std::chrono::steady_clock;
double Ms(Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();}
struct Timing {double wall=0,uiTotal=0,uiMax=0;};
Timing Timed(Workflow &w,const Json &r) {
    Timing t;const auto start=Clock::now();auto s=Clock::now();(void)w.Dispatch(r);double d=Ms(s);t.uiTotal+=d;t.uiMax=std::max(t.uiMax,d);
    while(w.Pending()) {s=Clock::now();w.OnUpdate(0);d=Ms(s);t.uiTotal+=d;t.uiMax=std::max(t.uiMax,d);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    t.wall=Ms(start);return t;
}
void Print(const char *name,const Timing &t) {std::printf("PERF %-28s wall %9.1f ms | UI total %8.1f ms | UI max-call %8.1f ms\n",name,t.wall,t.uiTotal,t.uiMax);std::fflush(stdout);}
}
TEST(PerfProbe,All) {
    if(!std::getenv("SCAPSLAB_PERF_TEST"))GTEST_SKIP()<<"set SCAPSLAB_PERF_TEST=1";
    Workflow w(CreateRef<JobSystem>(nullptr,4));w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    Print("iv default",Timed(w,{{"operation","run"}}));
    Print("iv default again",Timed(w,{{"operation","run"}}));
    Print("band",Timed(w,{{"operation","band"},{"voltage_V",0.0}}));
    std::printf("PERF iv points %zu, result json %zu bytes, workspace json %zu bytes\n",w.Current().result.at("points").size(),w.Current().result.dump().size(),w.Workspace().dump().size());
    // drag: 60 edits, one per frame, each completes before the next frame like a slow drag
    {
        double total=0,mx=0;const int n=60;
        for(int i=0;i<n;++i) {
            auto doc=w.Current().document;doc["layers"][0]["thickness_m"]=doc["layers"][0]["thickness_m"].get<double>()*(1+0.002*(i%2?1:-1));
            const auto s=Clock::now();(void)w.Dispatch({{"operation","edit"},{"document",doc},{"gesture_id","perf"}});w.OnUpdate(0);const double d=Ms(s);
            total+=d;mx=std::max(mx,d);std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        std::printf("PERF drag %d frames: avg UI cost/frame %.2f ms, max %.2f ms\n",n,total/n,mx);
    }
    std::string recovery=(std::filesystem::temp_directory_path()/"perf-recovery.scapslab").string();w.recoveryPath=recovery;
    Print("autosave (with results)",Timed(w,{{"operation","project_save"},{"path",recovery},{"autosave",true}}));
    Print("autosave no results",Timed(w,{{"operation","project_save"},{"path",recovery},{"autosave",true},{"include_results",false}}));
    for(int n:{3}) {auto spec=BatchDefaults();spec["axes"][0]["count"]=n;char name[40];std::snprintf(name,sizeof(name),"batch %d members",n);Print(name,Timed(w,{{"operation","run_batch"},{"spec",spec}}));}
    Print("autosave (with batch)",Timed(w,{{"operation","project_save"},{"path",recovery},{"autosave",true}}));
    for(const auto &e:std::filesystem::directory_iterator(SCAPSLAB_EXAMPLES)) if(e.path().extension()==".scapslab") {
        Workflow x(CreateRef<JobSystem>(nullptr,4));x.lightResponse=true;(void)Timed(x,{{"operation","project_load"},{"path",e.path().string()}});
        const auto name="iv "+e.path().stem().string();Print(name.c_str(),Timed(x,{{"operation","run"}}));
    }
    std::filesystem::remove(recovery);
}

// The single-continuation I-V (illuminated / Rs / Rsh) must equal the old per-point prefix replay exactly.
TEST(PerfEquivalence,SingleSweepEqualsPrefixReplay) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));
    auto doc=ReadProject((std::filesystem::path(SCAPSLAB_EXAMPLES)/"08-pn-illumination.yaml").string());
    doc["voltage"]={{"start_V",-0.2},{"stop_V",0.4},{"step_V",0.1}};
    (void)Timed(w,{{"operation","edit"},{"document",doc}});(void)Timed(w,{{"operation","run"}});
    const auto &pts=w.Current().result.at("points");ASSERT_GE(pts.size(),5u);
    const auto p=Model(w.Current().document);const auto device=scaps_core::make_device_from_project(p);const auto pc=scaps_core::physical_constants(p.constants_mode);
    auto volts=scaps_core::make_voltage_list(p.voltage);for(auto &v:volts)v=-v;
    const auto mesh=scaps_core::prepare_mesh(device,p.mesh,volts,{p.temperature_K},p.solver,pc);
    ASSERT_EQ(volts.size(),pts.size());
    for(std::size_t i=0;i<volts.size();++i) {
        const auto old=scaps_core::solve_iv(device,mesh,std::vector<double>(volts.begin(),volts.begin()+i+1),p.temperature_K,p.solver,pc).back();
        EXPECT_EQ(pts[i].at("J_A_m2").get<double>(),old.J_total_Am2)<<"point "<<i;
    }
}
TEST(PerfProbe,Batch) {
    if(!std::getenv("SCAPSLAB_PERF_TEST"))GTEST_SKIP()<<"set SCAPSLAB_PERF_TEST=1";
    Workflow w(CreateRef<JobSystem>(nullptr,4));w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    for(int n:{1,3,8}) {auto spec=BatchDefaults();spec["axes"][0]["count"]=n;char name[40];std::snprintf(name,sizeof(name),"batch %d members",n);Print(name,Timed(w,{{"operation","run_batch"},{"spec",spec}}));
        std::printf("PERF   spec voltages: %s | elapsed_s sum %.2f\n",w.Current().batchResult.at("spec").value("voltages_V",Json{}).dump().c_str(),w.Current().batchResult.value("elapsed_s",0.));}
}

TEST(PerfProbe,BatchEngineDirect) {
    if(!std::getenv("SCAPSLAB_PERF_TEST"))GTEST_SKIP()<<"set SCAPSLAB_PERF_TEST=1";
    auto doc=ReadProject(SCAPSLAB_PROJECT);auto spec=BatchDefaults();spec["axes"][0]["count"]=1;
    auto plan=BatchPreview(doc,spec);auto single=plan.at("spec");for(auto &axis:single["axes"]) {axis["values"]=Json::array({plan.at("combinations")[0].at(axis.at("path").get<std::string>())});axis.erase("count");axis.erase("start");axis.erase("stop");}
    for(bool curves:{true,false}) for(int threads:{1,0}) {
        auto s=single;s["include_curves"]=curves;if(threads)s["threads"]=threads;else s.erase("threads");
        const auto text=doc.dump();const auto sd=s.dump();const auto a=Clock::now();const auto out=scaps_core::run_batch_json(text,sd);const double wall=Ms(a);
        const auto b=Clock::now();const auto table=Json::parse(out);const double parse=Ms(b);
        std::printf("PERF run_batch_json curves=%d threads=%d: wall %.0f ms, engine elapsed_s %.2f, output %zu bytes, parse %.0f ms\n",curves,threads,wall,table.at("rows")[0].value("elapsed_s",0.),out.size(),parse);
    }
}
// UI-thread cost of starting a save (snapshot) of a big project. Run: SCAPSLAB_PERF_TEST=1 ScapsLabTests --gtest_filter=PerfProbe.SaveCost
TEST(PerfProbe,SaveCost) {
    if(!std::getenv("SCAPSLAB_PERF_TEST"))GTEST_SKIP()<<"set SCAPSLAB_PERF_TEST=1";
    Workflow w(CreateRef<JobSystem>(nullptr,4));w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    (void)Timed(w,{{"operation","run"}});(void)Timed(w,{{"operation","band"},{"voltage_V",0.0}});
    auto spec=BatchDefaults();spec["axes"][0]["count"]=8;(void)Timed(w,{{"operation","run_batch"},{"spec",spec}});
    std::printf("PERF workspace json %zu bytes\n",w.Workspace().dump().size());
    const std::string file=(std::filesystem::temp_directory_path()/"perf-save.scapslab").string();
    for(bool autosave:{false,true}) {auto s=Clock::now();(void)w.Dispatch({{"operation","project_save"},{"path",file},{"autosave",autosave}});const double d=Ms(s);
        double rest=0;while(w.Pending()){s=Clock::now();w.OnUpdate(0);rest=std::max(rest,Ms(s));std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        std::printf("PERF save autosave=%d: Dispatch (UI) %.2f ms, max OnUpdate %.2f ms\n",int(autosave),d,rest);}
    std::filesystem::remove(file);
}

// Memory growth of repeated Auto I-V runs + result clicks. Run: SCAPSLAB_PERF_TEST=1 SCAPSLAB_MEM_PROJECT=<file.scapslab> SCAPSLAB_MEM_RUNS=20 ScapsLabTests --gtest_filter=PerfProbe.MemoryGrowth
TEST(PerfProbe,MemoryGrowth) {
    if(!std::getenv("SCAPSLAB_PERF_TEST"))GTEST_SKIP()<<"set SCAPSLAB_PERF_TEST=1";
    const char *project=std::getenv("SCAPSLAB_MEM_PROJECT");const int n=std::getenv("SCAPSLAB_MEM_RUNS")?std::atoi(std::getenv("SCAPSLAB_MEM_RUNS")):20;
    Workflow w(CreateRef<JobSystem>(nullptr,4));w.lightResponse=true;
    if(project)(void)Timed(w,{{"operation","project_load"},{"path",project}});else w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    std::printf("MEM start: %s\n",w.MemoryReport().c_str());
    double clickMax=0;
    for(int i=0;i<n;++i) {
        auto doc=w.Current().document;doc["layers"][0]["thickness_m"]=doc["layers"][0]["thickness_m"].get<double>()*(1+0.001*(i%2?1:-1));
        (void)Timed(w,{{"operation","edit"},{"document",doc},{"gesture_id","mem"}});
        (void)Timed(w,{{"operation","run"}});
        if(i%5==4) { // click an older result like the Project tree does
            const auto id=w.Current().history[w.Current().history.size()/2].at("run_id");const auto t=Timed(w,{{"operation","history_select"},{"device_id",w.Current().id},{"run_id",id}});clickMax=std::max(clickMax,t.wall);
            std::printf("MEM run %d: %s | click wall %.1f ms\n",i+1,w.MemoryReport().c_str(),t.wall);std::fflush(stdout);
        }
    }
    std::printf("MEM end: %s | max click %.1f ms\n",w.MemoryReport().c_str(),clickMax);
}

// Old runs must drop their spatial profiles (bounded memory); pinned and selected runs keep them.
TEST(PerfEquivalence,OldRunsDropProfiles) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    auto doc=w.Current().document;doc["voltage"]={{"start_V",0.},{"stop_V",.1},{"step_V",.05}};(void)Timed(w,{{"operation","edit"},{"document",doc}});
    auto ui=w.project.ui;ui["history_retention"]=20;(void)Timed(w,{{"operation","project_edit"},{"ui",ui}});
    for(int i=0;i<8;++i){doc["temperature_K"]=300.+i;(void)Timed(w,{{"operation","edit"},{"document",doc}});(void)Timed(w,{{"operation","run"}});}
    const auto stored=[&]{int n=0;for(const auto &run:w.Current().history)n+=run.value("profiles_stored",true);return n;};
    ASSERT_EQ(w.Current().history.size(),8);EXPECT_EQ(stored(),3);
    const auto oldest=w.Current().history.front().at("run_id").get<std::string>();
    (void)Timed(w,{{"operation","history_select"},{"run_id",oldest}});EXPECT_EQ(w.Current().selectedRun,oldest);
    EXPECT_EQ(w.Current().history.front().value("profiles_stored",true),false); // already dropped: selecting does not resurrect it
    const auto newest=w.Current().history.back().at("run_id").get<std::string>();
    (void)Timed(w,{{"operation","history_update"},{"run_id",newest},{"pinned",true}});
    (void)Timed(w,{{"operation","history_clear"},{"run_ids",Json::array({oldest})}});EXPECT_EQ(w.Current().history.size(),7);EXPECT_EQ(w.Current().selectedRun,"");
}
