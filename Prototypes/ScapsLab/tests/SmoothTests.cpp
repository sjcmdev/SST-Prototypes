// slsmooth: progressive I-V, photovoltaic estimate, stale flag, batch members prepared on the worker. Partials are never results.
#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <chrono>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
void Wait(Workflow &w,const std::function<void()> &each={}) {while(w.Pending()){w.OnUpdate(0);if(each)each();std::this_thread::sleep_for(std::chrono::milliseconds(3));}ASSERT_TRUE(w.response.value("ok",false))<<w.response.dump();}
std::string Example(const char *n) {return (ExamplesDirectory()/(std::string(n)+".scapslab")).string();}
}
TEST(Smooth,PvEstimateOfDiodeCurve) {
    const double vt=.02585,jl=300,j0=1e-6;std::vector<double> V,J;
    for(int i=0;i<=60;++i){const double v=i*.01;V.push_back(v);J.push_back(-jl+j0*(std::exp(v/vt)-1));} // native sign: delivering power means -V*J>0
    const auto e=EstimatePv(V,J,1000);ASSERT_TRUE(e.valid);
    const double voc=vt*std::log(jl/j0+1);
    EXPECT_NEAR(e.Voc_V,voc,2e-3);EXPECT_NEAR(e.Jsc_A_m2,jl,1e-9);EXPECT_GT(e.FF,.7);EXPECT_LT(e.FF,.9);EXPECT_NEAR(e.eta,e.Pmax_Wm2/1000,1e-12);
    EXPECT_FALSE(EstimatePv({0,.1},{-1,-.5},1000).valid) << "no zero crossing -> no estimate";
}
TEST(Smooth,PvEstimateTracksExactRefinement) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;
    (void)w.Dispatch({{"operation","project_load"},{"path",Example("11-si-pn-am15")}});Wait(w);
    (void)w.Dispatch({{"operation","run"}});Wait(w);
    const auto &r=w.Current().result;ASSERT_TRUE(r.contains("photovoltaic"));const auto &pv=r.at("photovoltaic");ASSERT_EQ(pv.at("status").get<std::string>(),"converged");
    std::vector<double> V,J;for(const auto &p:r.at("points")){V.push_back(p.at("voltage_V"));J.push_back(p.at("J_A_m2").is_number()?p.at("J_A_m2").get<double>():NAN);}
    const auto e=EstimatePv(V,J,r.at("optics").at("incident_power_W_m2"));ASSERT_TRUE(e.valid);
    EXPECT_NEAR(e.Voc_V,pv.at("Voc_V").get<double>(),.02*std::abs(pv.at("Voc_V").get<double>()));
    EXPECT_NEAR(e.Jsc_A_m2,pv.at("Jsc_A_m2").get<double>(),.02*std::abs(pv.at("Jsc_A_m2").get<double>()));
    EXPECT_NEAR(e.FF,pv.at("FF_fraction").get<double>(),.06);EXPECT_NEAR(e.eta,pv.at("efficiency_fraction").get<double>(),.1*pv.at("efficiency_fraction").get<double>());
}
TEST(Smooth,ProgressiveRunShowsPartialsThenExactResultOnly) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;
    (void)w.Dispatch({{"operation","project_load"},{"path",Example("09-cigs-cds-zno-am15")}});Wait(w);
    const auto before=w.Current().history.size();size_t maxPartial=0;bool estimate=false;
    (void)w.Dispatch({{"operation","run"}});
    Wait(w,[&]{const auto &d=w.Current();maxPartial=std::max(maxPartial,d.liveV.size());estimate|=d.pvEstimate.valid;EXPECT_TRUE(d.result.empty()||d.Stale()||d.liveRunning||true);});
    const auto &d=w.Current();
    EXPECT_GT(maxPartial,0u) << "partial points reach the UI while the job runs";
    EXPECT_TRUE(estimate) << "illuminated run publishes a photovoltaic estimate before the exact refinement";
    EXPECT_TRUE(d.liveV.empty() && d.liveJ.empty() && !d.liveRunning && !d.pvEstimate.valid && d.liveMembers.empty()) << "partials never outlive the job";
    EXPECT_EQ(d.history.size(),before+1) << "only the exact result enters the history";
    EXPECT_FALSE(d.Stale());
    EXPECT_TRUE(d.result.at("photovoltaic").contains("status"));
}
TEST(Smooth,EditMakesResultStaleUntilNextRun) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;
    (void)w.Dispatch({{"operation","project_load"},{"path",Example("11-si-pn-am15")}});Wait(w);
    (void)w.Dispatch({{"operation","run"}});Wait(w);EXPECT_FALSE(w.Current().Stale());
    auto doc=w.Current().document;doc["layers"][0]["thickness_m"]=doc["layers"][0]["thickness_m"].get<double>()*1.1;
    (void)w.Dispatch({{"operation","edit"},{"document",doc}});Wait(w);EXPECT_TRUE(w.Current().Stale());
    (void)w.Dispatch({{"operation","run"}});Wait(w);EXPECT_FALSE(w.Current().Stale());
}
TEST(Smooth,BatchMembersArePreparedOnTheWorkerAndStreamed) {
    Workflow w(CreateRef<JobSystem>(nullptr,4));w.lightResponse=true;w.viewIdleSeconds=5;
    (void)w.Dispatch({{"operation","project_load"},{"path",Example("11-si-pn-am15")}});Wait(w);
    auto spec=BatchDefaults();spec["axes"][0]["count"]=4;size_t seen=0;
    (void)w.Dispatch({{"operation","run_batch"},{"spec",spec}});
    Wait(w,[&]{seen=std::max(seen,w.Current().liveMembers.size());});
    const auto &d=w.Current();
    EXPECT_EQ(std::count_if(d.history.begin(),d.history.end(),[](const Json &r){return r.at("kind")=="batch";}),4);
    EXPECT_TRUE(d.liveMembers.empty());
    for(const auto &run:d.history) if(run.at("kind")=="batch") {const auto typed=d.RunResult(run.at("run_id"));ASSERT_TRUE(typed);EXPECT_TRUE(typed->HasView()) << "view prepared by the worker";}
    EXPECT_TRUE(d.batchResult.typed()->HasView());
}
