#include <gtest/gtest.h>
#include "Panel.hpp"
#include "PlotUi.hpp"
#include <imgui_internal.h>
#include <implot_internal.h>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
class PolishUi : public ::testing::Test {
protected:
    std::string logged;
    void SetUp() override {
        ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,1000};io.DeltaTime=1.f/60;
        io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    }
    void TearDown() override {ImPlot::DestroyContext();ImGui::DestroyContext();}
    void Frame(Panel& panel,float width,bool log=false) {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,950});ImGui::Begin("Polish UI");if(log)ImGui::LogToBuffer();panel.RenderContent();if(log){logged=GImGui->LogBuffer.c_str();ImGui::LogFinish();}ImGui::End();ImGui::Render();
        EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);
    }
    void Small(Workflow& w) {
        w.lightResponse=true;auto d=CreateRef<ProjectDraft>(*DraftFromDocument(ReadProject(SCAPSLAB_PROJECT)));
        d->project.mesh.points_per_layer=21;d->project.voltage={0,.02,.01};w.Current().SetDraft(std::move(d));
    }
    void Finish(Workflow& w) {
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(w.Pending() && std::chrono::steady_clock::now()<end){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ASSERT_FALSE(w.Pending());
    }
    std::string Text(Panel& panel) {
        Frame(panel,1100,true);return logged;
    }
    bool Click(Panel& panel,const char* label,const char* scope="shotbar") {
        Frame(panel,1100);Frame(panel,1100);const auto* window=ImGui::FindWindowByName("Polish UI");
        const auto id=ImHashStr(label,0,*scope?ImHashStr(scope,0,window->ID):window->ID);auto& io=ImGui::GetIO();
        for(float y=35;y<230;y+=4)for(float x=10;x<1000;x+=8){io.AddMousePosEvent(x,y);Frame(panel,1100);Frame(panel,1100);if(GImGui->HoveredId==id){io.AddMouseButtonEvent(0,true);Frame(panel,1100);io.AddMouseButtonEvent(0,false);Frame(panel,1100);return true;}}
        return false;
    }
};
}
TEST_F(PolishUi,SingleShotTemperatureDragReleaseShowsExactLive) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);auto plan=CreateRef<RunPlan>();plan->actions[0].onEdit=plan->actions[1].onEdit=true;
    ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Finish(w);
    const auto run=w.Current().selectedRun;const auto stored=w.Current().RunResult(run);ASSERT_FALSE(run.empty());
    w.dragging=w.interacting=true;auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=320;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,"temperature-drag")).value("ok",false));Finish(w);w.lastFullMs=200;
    for(int i=0;i<4;++i){w.lastLive=w.lastBandLive={};w.OnUpdate(0);Finish(w);}
    EXPECT_GT(w.previewsStarted,0u);EXPECT_FALSE(w.Current().previewV.empty());
    w.dragging=w.interacting=false;w.Current().changedAt-=std::chrono::seconds(1);
    for(int i=0;i<4;++i){w.OnUpdate(0);Finish(w);}
    EXPECT_EQ(w.Current().resultRevision,w.Current().revision);EXPECT_TRUE(w.Current().previewV.empty());EXPECT_TRUE(w.Current().selectedRun.empty());
    ASSERT_FALSE(w.Current().Iv()->points.empty())<<w.status;ASSERT_TRUE(w.Current().Iv()->snapshot)<<w.status;
    EXPECT_DOUBLE_EQ(w.Current().Iv()->snapshot->project.temperature_K,320);EXPECT_EQ(w.Current().RunResult(run),stored);EXPECT_EQ(w.Current().history.size(),1u);
    Panel band(&w,5);EXPECT_NE(Text(band).find("LIVE result"),std::string::npos);EXPECT_EQ(Text(band).find("STORED result"),std::string::npos);
}
TEST_F(PolishUi,OnEditLoadNewAndDeviceEditRecalculateWithoutStoring) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);auto plan=CreateRef<RunPlan>();plan->actions[0].onEdit=plan->actions[1].onEdit=true;
    ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));const auto workspace=w.Workspace();
    Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.lightResponse=true;loaded.SetWorkspace(workspace);
    loaded.Current().changedAt-=std::chrono::seconds(1);for(int i=0;i<5;++i){loaded.OnUpdate(0);Finish(loaded);}
    EXPECT_FALSE(loaded.Current().Iv()->points.empty());EXPECT_FALSE(loaded.Current().Band()->points.empty());EXPECT_TRUE(loaded.Current().history.empty());
    auto edited=CreateRef<ProjectDraft>(*loaded.Current().draft);edited->project.layers.front().Nd_m3*=2;
    ASSERT_TRUE(loaded.Exec(Cmd::EditDocument::Make(edited)).value("ok",false));Finish(loaded);loaded.Current().changedAt-=std::chrono::seconds(1);
    for(int i=0;i<5;++i){loaded.OnUpdate(0);Finish(loaded);}
    ASSERT_TRUE(loaded.Current().Iv()->snapshot);EXPECT_DOUBLE_EQ(loaded.Current().Iv()->snapshot->project.layers.front().Nd_m3,edited->project.layers.front().Nd_m3);
    ASSERT_TRUE(loaded.Dispatch({{"operation","device_add"}}).value("ok",false));
    EXPECT_TRUE(loaded.Current().runPlan->actions[0].onEdit);EXPECT_TRUE(loaded.Current().runPlan->actions[1].onEdit);
    loaded.Current().changedAt-=std::chrono::seconds(1);for(int i=0;i<5;++i){loaded.OnUpdate(0);Finish(loaded);}
    EXPECT_FALSE(loaded.Current().Iv()->points.empty());EXPECT_TRUE(loaded.Current().history.empty());
}
TEST_F(PolishUi,BiasSelectionUpdatesLiveAndStoredProfilesWithoutStoring) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Finish(w);
    const auto run=w.Current().selectedRun;const auto stored=w.Current().RunResult(run);const auto count=w.Current().history.size();
    ASSERT_TRUE(w.Dispatch({{"operation","select_bias"},{"index",2}}).value("ok",false));
    Panel band(&w,5);EXPECT_NE(Text(band).find("Recalculate this bias"),std::string::npos);
    ASSERT_TRUE(Click(band,"Recalculate this bias",""));Finish(w);EXPECT_EQ(w.Current().selectedRun,run);EXPECT_EQ(w.Current().RunResult(run),stored);EXPECT_EQ(w.Current().history.size(),count);
    EXPECT_EQ(Text(band).find("Profiles not stored at this bias"),std::string::npos);
    ASSERT_TRUE(w.Current().storedBand);EXPECT_DOUBLE_EQ(w.Current().storedBand->points.front().voltage_V,.02);
    Panel storedGr(&w,22);EXPECT_EQ(Text(storedGr).find("Profiles not stored at this bias"),std::string::npos);
    ASSERT_TRUE(w.Dispatch({{"operation","select_bias"},{"index",0}}).value("ok",false));EXPECT_EQ(Text(band).find("Profiles not stored at this bias"),std::string::npos);
    EXPECT_FALSE(w.Dispatch({{"operation","select_bias"},{"index",99}}).value("ok",true));EXPECT_EQ(w.Current().selectedBias,0);
    ASSERT_TRUE(w.Exec(Cmd::SelectRun::Make(w.Current().id,"")).value("ok",false));
    ASSERT_TRUE(w.Dispatch({{"operation","select_bias"},{"index",1}}).value("ok",false));w.Current().changedAt-=std::chrono::seconds(1);
    for(int i=0;i<3;++i){w.OnUpdate(0);Finish(w);}EXPECT_DOUBLE_EQ(w.Current().Band()->points.at(0).voltage_V,.01);EXPECT_EQ(w.Current().history.size(),count);
    Panel gr(&w,22);EXPECT_EQ(Text(gr).find("G/R details were not requested"),std::string::npos);
}
TEST_F(PolishUi,ManualRecalculateButtonsUseLiveWithoutStoring) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);
    for(const auto [view,label]:{std::pair{5,"Recalculate band"},{10,"Recalculate I-V"},{6,"Recalculate C-V"},{8,"Recalculate C-f"},{19,"Recalculate QE"},{22,"Recalculate band"}}){
        auto plan=CreateRef<RunPlan>();plan->actions[2].range=SweepRange{SweepSampling::Points,false,0,.01,.01,2};plan->actions[3].range=SweepRange{SweepSampling::Points,true,1000,2000,1,2};plan->actions[4].range=SweepRange{SweepSampling::Points,false,500,600,1,2};
        ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));Panel panel(&w,view);ASSERT_TRUE(Click(panel,label))<<label;Finish(w);
        EXPECT_TRUE(w.response.value("ok",false))<<w.status;EXPECT_TRUE(w.Current().history.empty());EXPECT_TRUE(w.Current().selectedRun.empty());
    }
}
TEST_F(PolishUi,RunRangeKeepsReadableColumnsAtSavedDefaultAndWideWidths) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.autoRecalculate=w.autoRecalculateBand=false;w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));Panel panel(&w,4);
    for(float width:{405.f,620.f,1100.f}) {
        for(int f=0;f<3;++f)Frame(panel,width);
        const auto* window=ImGui::FindWindowByName("Polish UI");ASSERT_NE(window,nullptr);
        const auto* range=GImGui->Tables.GetByKey(ImHashStr("Run Actions",0,window->ID));ASSERT_NE(range,nullptr);
        EXPECT_EQ(range->ColumnsCount,9);EXPECT_EQ(range->CurrentRow,int(w.Current().runPlan->actions.size()));
        for(int col:{3,4,5,7})EXPECT_GE(range->Columns[col].WidthGiven,110.f);
        EXPECT_LT(range->Columns[6].WidthGiven,range->Columns[3].WidthGiven);
        if(width<1000)EXPECT_TRUE(range->Flags&ImGuiTableFlags_ScrollX);
    }
}
TEST_F(PolishUi,IvRunningAndPreviewStatusStaysAboveEveryLegendPosition) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.autoRecalculate=w.autoRecalculateBand=false;Panel panel(&w,10);
    auto& d=w.Current();auto iv=CreateRef<IvResult>();for(int i=0;i<2;++i){IvPoint point;point.voltage_V=i*.1;point.J_A_m2=1.+i;point.valid=true;iv->points.push_back(std::move(point));}d.result.adopt(iv);d.liveRunning=true;d.liveV={0,.1};d.liveJ={1.,2.};
    for(int mode:{0,1,2})for(int corner:{ImPlotLocation_NorthWest,ImPlotLocation_NorthEast,ImPlotLocation_SouthEast})for(float width:{405.f,620.f}) {
        d.liveRunning=mode==0;d.previewV={0,.1};d.previewJ={1.,2.};d.previewCoarse=mode==1;
        w.plotSession.settings.plots["I-V compare"].legendLocation=corner;
        for(int f=0;f<3;++f)Frame(panel,width);
        const auto* window=ImGui::FindWindowByName("Polish UI");const ImPlotPlot* plot=nullptr;
        for(int i=0;i<GImPlot->Plots.GetBufSize();++i){const auto* p=GImPlot->Plots.GetByIndex(i);if(p->HasTitle() && std::string(p->GetTitle())=="I-V compare")plot=p;}
        ASSERT_NE(plot,nullptr);ASSERT_GT(plot->Items.GetLegendCount(),0);
        int header=0;
        const auto orange=ImGui::ColorConvertFloat4ToU32({1.f,.55f,.1f,1.f});
        for(const auto& v:window->DrawList->VtxBuffer) {
            if(v.col==orange && v.pos.y<plot->FrameRect.Min.y){++header;EXPECT_FALSE(plot->Items.Legend.Rect.Contains(v.pos));}
            EXPECT_NE(v.col,IM_COL32(255,140,25,230));
        }
        EXPECT_GT(header,0);
    }
}
TEST_F(PolishUi,LogYAutoFitIgnoresZerosDenormalsHiddenNoiseAndEmptySeries) {
    PlotSession session;
    for(bool empty:{false,true})for(int frame=0;frame<3;++frame) {
        ImGui::NewFrame();ImGui::SetNextWindowSize({900,700});ImGui::Begin("Log fit");
        ASSERT_TRUE(BeginSavedPlot(session,"All log Y",{-1,500}));ImPlot::SetupAxes("x","rate");ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupAxis(ImAxis_Y2,"sheet",ImPlotAxisFlags_AuxDefault);ImPlot::SetupAxisScale(ImAxis_Y2,ImPlotScale_Log10);SetupSavedPlot(session,"All log Y");
        const double x[]={0,1,2,3,4,5,6},y[]={0,-1,std::numeric_limits<double>::denorm_min(),1e-198,1e6,1e8,NAN};
        const double zeros[]={0,0,0,0,0,0,0},hidden[]={1e30,1e30};
        auto* plot=ImPlot::GetCurrentPlot();
        for(auto axis:{ImAxis_Y1,ImAxis_Y2}) {
            ImPlot::SetAxes(ImAxis_X1,axis);const auto name=axis==ImAxis_Y1?"bulk":"sheet";
            ImPlot::PlotLine(name,x,empty?zeros:y,7);
            const auto hiddenName=std::string(name)+" hidden";ImPlot::HideNextItem(true,ImPlotCond_Always);session.settings.plots["All log Y"].hidden.insert(ImGui::GetID(hiddenName.c_str()));
            ImPlot::PlotLine(hiddenName.c_str(),x,hidden,2);
        }
        EndSavedPlot(session,"All log Y");
        for(auto axis:{ImAxis_Y1,ImAxis_Y2}) {
            const auto& range=plot->Axes[axis].Range;
            EXPECT_GT(range.Min,0);EXPECT_LT(std::log10(range.Max)-std::log10(range.Min),14.);
            if(!empty){EXPECT_LT(range.Min,1e6);EXPECT_GT(range.Max,1e8);EXPECT_LT(range.Max,1e10);}
        }
        ImGui::End();ImGui::Render();
    }
    EXPECT_FALSE(session.settings.plots["All log Y"].manual);EXPECT_EQ(session.revision,0u);
}
TEST_F(PolishUi,PinnedStoredViewSurvivesEditsAndOffersBackToLive) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Finish(w);
    const auto run=w.Current().selectedRun;const auto stored=w.Current().RunResult(run);w.Current().pinStoredView=true;
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=320;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Finish(w);
    EXPECT_EQ(w.Current().selectedRun,run);Panel panel(&w,5);
    EXPECT_NE(Text(panel).find("Viewing stored run"),std::string::npos);
    ASSERT_TRUE(Click(panel,"Back to Live"));EXPECT_TRUE(w.Current().selectedRun.empty());EXPECT_FALSE(w.Current().pinStoredView);
    EXPECT_EQ(w.Current().RunResult(run),stored);
}
TEST_F(PolishUi,BandOnlyOnEditLoadsAndTracksDeviceEdits) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);auto plan=CreateRef<RunPlan>();plan->actions[0].onEdit=true;
    ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));
    w.Current().changedAt-=std::chrono::seconds(1);for(int i=0;i<3;++i){w.OnUpdate(0);Finish(w);}
    EXPECT_TRUE(w.Current().Iv()->points.empty());ASSERT_FALSE(w.Current().Band()->points.empty());
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=310;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Finish(w);w.Current().changedAt-=std::chrono::seconds(1);w.lastBandLive={};
    for(int i=0;i<3;++i){w.OnUpdate(0);Finish(w);}
    ASSERT_TRUE(w.Current().Band()->snapshot);EXPECT_DOUBLE_EQ(w.Current().Band()->snapshot->project.temperature_K,310);
    EXPECT_TRUE(w.Current().BandFresh());EXPECT_TRUE(w.Current().history.empty());
}
TEST_F(PolishUi,StoredIvClickSelectsBiasEvenWhenLiveCurveIsArchived) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Finish(w);Panel panel(&w,10);
    Frame(panel,1100);Frame(panel,1100);
    const ImPlotPlot* plot=nullptr;for(int i=0;i<GImPlot->Plots.GetBufSize();++i){const auto* p=GImPlot->Plots.GetByIndex(i);if(p->HasTitle() && std::string(p->GetTitle())=="I-V compare")plot=p;}
    ASSERT_NE(plot,nullptr);
    const auto& axis=plot->Axes[ImAxis_X1];const double value=w.Current().Iv()->points.at(2).voltage_V;
    auto& io=ImGui::GetIO();io.AddMousePosEvent(float(axis.PlotToPixels(value)),plot->PlotRect.GetCenter().y);Frame(panel,1100);Frame(panel,1100);
    io.AddMouseButtonEvent(0,true);Frame(panel,1100);io.AddMouseButtonEvent(0,false);Frame(panel,1100);
    EXPECT_EQ(w.Current().selectedBias,2);EXPECT_DOUBLE_EQ(w.Current().biasVoltage_V,.02);EXPECT_EQ(w.Current().history.size(),1u);
}
TEST_F(PolishUi,TemperatureMouseDragAfterSingleShotReturnsExactLive) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);auto plan=CreateRef<RunPlan>();plan->actions[0].onEdit=plan->actions[1].onEdit=true;
    ASSERT_TRUE(w.Exec(Cmd::SetRunPlan::Make(plan)).value("ok",false));ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Finish(w);
    const auto run=w.Current().selectedRun;const auto stored=w.Current().RunResult(run);
    Panel panel(&w,4);Frame(panel,1100);Frame(panel,1100);
    const auto* window=ImGui::FindWindowByName("Polish UI");const auto scope=ImHashStr("Temperature [K]",0,window->ID);
    const auto* row=GImGui->Tables.GetByKey(ImHashStr("Temperature [K]",0,scope));ASSERT_NE(row,nullptr);
    const float x=row->Columns[1].MinX+row->Columns[1].WidthGiven*.4f,y=row->OuterRect.GetCenter().y;
    auto& io=ImGui::GetIO();io.AddMousePosEvent(x,y);Frame(panel,1100);Frame(panel,1100);
    w.dragging=w.interacting=true;io.AddMouseButtonEvent(0,true);Frame(panel,1100);io.AddMousePosEvent(x+80,y);Frame(panel,1100);Finish(w);
    EXPECT_TRUE(w.dragSeen);const double temperature=w.Current().draft->project.temperature_K;EXPECT_NE(temperature,300.);
    w.lastFullMs=200;for(int i=0;i<4;++i){w.lastLive=w.lastBandLive={};w.OnUpdate(0);Finish(w);}
    EXPECT_FALSE(w.Current().previewV.empty());EXPECT_GT(w.previewsStarted,0u);
    io.AddMouseButtonEvent(0,false);Frame(panel,1100);Finish(w);w.dragging=w.interacting=false;w.Current().changedAt-=std::chrono::seconds(1);
    for(int i=0;i<4;++i){w.OnUpdate(0);Finish(w);}
    ASSERT_TRUE(w.Current().Iv()->snapshot);EXPECT_DOUBLE_EQ(w.Current().Iv()->snapshot->project.temperature_K,temperature);
    EXPECT_EQ(w.Current().resultRevision,w.Current().revision);EXPECT_TRUE(w.Current().selectedRun.empty());EXPECT_TRUE(w.Current().previewV.empty());
    EXPECT_EQ(w.Current().RunResult(run),stored);EXPECT_EQ(w.Current().history.size(),1u);Panel band(&w,5);EXPECT_NE(Text(band).find("LIVE result"),std::string::npos);
}
TEST_F(PolishUi,MissingStoredBiasUsesEffectiveSnapshotAfterLiveModelEdit) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);auto plan=CreateRef<RunPlan>();plan->actions[1].temperature_K=315;
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Finish(w);
    const auto run=w.Current().selectedRun;const auto stored=w.Current().RunResult(run);ASSERT_TRUE(stored->snapshot);EXPECT_DOUBLE_EQ(stored->snapshot->project.temperature_K,315);
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=330;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));Finish(w);
    ASSERT_TRUE(w.Exec(Cmd::SelectRun::Make(w.Current().id,run)).value("ok",false));
    ASSERT_TRUE(w.Exec(Cmd::SelectBias::Make(2)).value("ok",false));
    ASSERT_TRUE(w.Exec(Cmd::RecalculateStoredBias::With()).value("ok",false));Finish(w);
    ASSERT_TRUE(w.Current().storedBand);EXPECT_DOUBLE_EQ(w.Current().storedBand->snapshot->project.temperature_K,315);
    EXPECT_DOUBLE_EQ(w.Current().storedBand->points.front().voltage_V,.02);EXPECT_DOUBLE_EQ(w.Current().draft->project.temperature_K,330);
    EXPECT_EQ(w.Current().RunResult(run),stored);EXPECT_EQ(w.Current().selectedRun,run);EXPECT_EQ(w.Current().history.size(),1u);
}
TEST_F(PolishUi,DarkDeviceGenerationRecombinationUsesMeaningfulLogRange) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;
    auto draft=CreateRef<ProjectDraft>(*DraftFromDocument(ReadProject(SCAPSLAB_PROJECT)));
    draft->project.mesh.points_per_layer=11;draft->project.voltage={0,0,.01};draft->project.illumination.enabled=false;w.Current().SetDraft(std::move(draft));
    ASSERT_TRUE(w.Dispatch({{"operation","run"}}).value("ok",false));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(w.Pending() && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_FALSE(w.Pending());ASSERT_TRUE(w.response.value("ok",false));Panel panel(&w,22);
    for(int frame=0;frame<3;++frame)Frame(panel,900);
    const ImPlotPlot* plot=nullptr;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i){const auto* p=GImPlot->Plots.GetByIndex(i);if(p->HasTitle() && std::string(p->GetTitle())=="G(x), R(x)")plot=p;}
    ASSERT_NE(plot,nullptr);const auto& range=plot->Axes[ImAxis_Y1].Range;
    EXPECT_GT(range.Min,0);EXPECT_TRUE(std::isfinite(range.Max));EXPECT_LT(std::log10(range.Max)-std::log10(range.Min),14.);
}
