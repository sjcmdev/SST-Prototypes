// S3: panel plot caches. Draw paths read cached vectors; events (RunCompleted/HistoryChanged/...) invalidate them.
#include <gtest/gtest.h>
#include "Panel.hpp"
#include "Core/App/Application.hpp"
#include <imgui.h>
#include <implot.h>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib,"dbghelp.lib")
#include <scaps_core/device_project_yaml.hpp>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
thread_local bool g_count=false;
thread_local long g_allocs=0,g_scoped=0,g_inPlot=0;
}
// Allocation counter (only the thread that enabled it counts): replaces global new for this test executable.
static void Trace();
void *operator new(std::size_t n) {if(g_count){++g_allocs;if(std::getenv("SCAPSLAB_ALLOC_TRACE")){g_count=false;Trace();g_count=true;}}if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](std::size_t n) {if(g_count)++g_allocs;if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete[](void *p) noexcept {std::free(p);}
void operator delete(void *p,std::size_t) noexcept {std::free(p);}
void operator delete[](void *p,std::size_t) noexcept {std::free(p);}
static void Trace() { // SCAPSLAB_ALLOC_TRACE=1: print the call stack of every counted allocation
    static bool init=false;if(!init){SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME);SymInitialize(GetCurrentProcess(),nullptr,TRUE);init=true;}
    void *frames[24];const auto n=CaptureStackBackTrace(1,24,frames,nullptr);char buf[sizeof(SYMBOL_INFO)+256];auto *sym=reinterpret_cast<SYMBOL_INFO *>(buf);
    std::printf("ALLOC #%ld:",g_allocs);
    for(unsigned i=0;i<n;++i){sym->SizeOfStruct=sizeof(SYMBOL_INFO);sym->MaxNameLen=200;if(SymFromAddr(GetCurrentProcess(),DWORD64(frames[i]),nullptr,sym))std::printf(" < %s",sym->Name);}
    std::putchar(10);
}
namespace {
struct Ui {
    Ui() {ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,900);io.Fonts->AddFontDefault();io.Fonts->Build();}
    ~Ui() {ImPlot::DestroyContext();ImGui::DestroyContext();}
};
void Finish(Workflow &w) {while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_TRUE(w.response.value("ok",false))<<w.response.dump();}
void Frame(Panel &panel) {
    auto &io=ImGui::GetIO();io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Plot cache");panel.RenderContent();ImGui::End();ImGui::Render();
}
void SeedRuns(Workflow &w,int runs) {
    w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.Current().document["voltage"]={{"start_V",0.},{"stop_V",.1},{"step_V",.05}};w.project.ui["history_retention"]=100;
    ASSERT_TRUE(w.Dispatch({{"operation","run"}}).value("ok",false));Finish(w);
    const auto seed=w.Current().history.front();
    for(int i=0;i<runs;++i) {auto run=seed;run["run_id"]="cache-"+std::to_string(i);run["pinned"]=true;run["visible"]=true;run["name"]="#"+std::to_string(i+1);w.Current().history.mut().push_back(run);auto runs=CreateRef<RunMap>(*w.Current().runs);(*runs)[run["run_id"].get<std::string>()]=w.Current().RunResult(seed.at("run_id"));w.Current().runs=std::move(runs);}
    w.Emit(HistoryChanged(w.Current().id));
}
}
TEST(PlotCache,PlotDataPathDoesNotAllocateOrRebuildWithTwentyRuns) {
    Ui ui;Workflow w(CreateRef<JobSystem>(nullptr,1));SeedRuns(w,20);ASSERT_GE(w.Current().history.size(),20u);
    Panel panel(&w,10);
    for(int i=0;i<4;++i)Frame(panel); // warm: caches built, ImGui/ImPlot buffers sized
    const int builds=PlotCacheBuildCount();g_allocs=0;g_scoped=0;g_count=true;
    SetPlotDataHook([](bool on){g_scoped=on?g_allocs:g_scoped;if(!on)g_inPlot+=g_allocs-g_scoped;});
    for(int i=0;i<30;++i)Frame(panel);
    SetPlotDataHook(nullptr);g_count=false;
    EXPECT_EQ(PlotCacheBuildCount(),builds)<<"warm frames must not rebuild plot data";
    EXPECT_EQ(g_inPlot,0)<<"cached plot data lookup/drawing allocated";
    std::printf("PLOTCACHE whole RenderContent allocations/frame (toolbar+chrome included): %.1f\n",g_allocs/30.);
}
TEST(PlotCache,StaleCacheIsRebuiltByEventsOnly) {
    Ui ui;Workflow w(CreateRef<JobSystem>(nullptr,1));SeedRuns(w,3);
    Panel panel(&w,10);
    for(int i=0;i<3;++i)Frame(panel);
    const double before=PlotCacheSum();const int builds=PlotCacheBuildCount();EXPECT_NE(before,0.);
    // an in-place change without an event is not seen (the contract: mutators publish events)...
    {const auto id=w.Current().history.at(1).at("run_id").get<std::string>();auto iv=CreateRef<IvResult>(*std::static_pointer_cast<const IvResult>(w.Current().RunResult(id)));
     for(auto &pt:iv->points)if(std::isfinite(pt.J_A_m2))pt.J_A_m2*=3;auto runs=CreateRef<RunMap>(*w.Current().runs);(*runs)[id]=iv;w.Current().runs=std::move(runs);}
    Frame(panel);EXPECT_EQ(PlotCacheSum(),before);EXPECT_EQ(PlotCacheBuildCount(),builds);
    // ...and the HistoryChanged event makes the next frame rebuild it.
    w.Emit(HistoryChanged(w.Current().id));Frame(panel);
    EXPECT_NE(PlotCacheSum(),before);EXPECT_GT(PlotCacheBuildCount(),builds);
    // a real edit (job -> RunCompleted/DocumentEdited) changes the live curve in the plot
    const double mid=PlotCacheSum();auto doc=w.Current().document;doc["layers"][0]["thickness_m"]=doc["layers"][0]["thickness_m"].get<double>()*1.2;
    ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"document",doc}}).value("ok",false));Finish(w);ASSERT_TRUE(w.Dispatch({{"operation","run"}}).value("ok",false));Finish(w);
    Frame(panel);Frame(panel);EXPECT_NE(PlotCacheSum(),mid);
}
TEST(PlotCache,HistoryOperationsPublishEvents) {
    Ui ui;Workflow w(CreateRef<JobSystem>(nullptr,1));w.WatchPlots();SeedRuns(w,2);
    const auto gen=w.plotGen;ASSERT_TRUE(w.Dispatch({{"operation","history_update"},{"device_id",w.Current().id},{"run_id","cache-0"},{"visible",false}}).value("ok",false));
    EXPECT_NE(w.plotGen,gen);
}
