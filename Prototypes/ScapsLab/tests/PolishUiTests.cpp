#include <gtest/gtest.h>
#include "Panel.hpp"
#include "PlotUi.hpp"
#include <scaps_core/solver_core.hpp>
#include <imgui_internal.h>
#include <implot_internal.h>
#include <thread>
#include <fstream>
#include <iomanip>
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
    w.interacting=true;Panel band(&w,5),gr(&w,22);
    for(bool live:{false,true}) {
        if(live)ASSERT_TRUE(w.Exec(Cmd::SelectRun::Make(w.Current().id,"")).value("ok",false));
        for(int index:{2,1,0,2,0}) {
            ASSERT_TRUE(w.Dispatch({{"operation","select_bias"},{"index",index}}).value("ok",false));
            const auto& point=PointAtBias(*w.Current().ProfileSweep(),*w.Current().ProfileBand(),index);
            EXPECT_TRUE(point.details);EXPECT_DOUBLE_EQ(point.voltage_V,.01*index);w.OnUpdate(0);
            EXPECT_FALSE(w.Pending());EXPECT_FALSE(w.Current().needsBiasDetails);
            EXPECT_EQ(Text(band).find("Loading details"),std::string::npos);EXPECT_EQ(Text(gr).find("Loading details"),std::string::npos);
        }
    }
    EXPECT_EQ(w.Current().RunResult(run),stored);EXPECT_EQ(w.Current().history.size(),count);
    EXPECT_EQ(w.liveBandsStarted,0u);EXPECT_FALSE(w.Dispatch({{"operation","select_bias"},{"index",99}}).value("ok",true));
    EXPECT_EQ(w.Current().selectedBias,0);
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
        EXPECT_EQ(range->ColumnsCount,9);EXPECT_EQ(range->CurrentRow,int(std::count_if(w.Current().runPlan->actions.begin(),w.Current().runPlan->actions.end(),[](const auto& a){return !IsDlts(a.kind);})));
        const auto* transients=GImGui->Tables.GetByKey(ImHashStr("Transient Actions",0,window->ID));ASSERT_NE(transients,nullptr);EXPECT_EQ(transients->ColumnsCount,3);EXPECT_EQ(transients->CurrentRow+1,kActionCount-6);
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
        const auto orange=ImGui::ColorConvertFloat4ToU32(LiveCurveColour);
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
            EXPECT_GT(range.Min,0);
            if(!empty){EXPECT_LE(range.Min,1e-198);EXPECT_GT(range.Max,1e8);EXPECT_LT(range.Max,1e12);}
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
TEST_F(PolishUi,FlatBandSliderTakeoverIsOneUndoGesture) {
    for(const auto* side:{"left","right"}) {
        Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);w.autoRecalculate=w.autoRecalculateBand=false;
        auto draft=CreateRef<ProjectDraft>(*w.Current().draft);draft->project.left_contact.flat_band=draft->project.right_contact.flat_band=true;draft->project.left_contact.phi_m_eV=draft->project.right_contact.phi_m_eV=7.75;w.Current().SetDraft(draft);
        const auto device=scaps_core::make_device_from_project(draft->project);const auto mesh=scaps_core::build_mesh(device,draft->project.mesh);const auto pc=scaps_core::physical_constants(draft->project.constants_mode);
        const auto node=std::string_view(side)=="left"?std::size_t(0):mesh.size()-1;std::vector<scaps_core::DefectLevel> scratch;const auto& defects=scaps_core::node_defects(device,mesh,node,scratch,draft->project.temperature_K,pc);
        const double derived=-scaps_core::equilibrium_contact_bc(device.contact_left,mesh.nodes[node],draft->project.temperature_K,pc,defects).psi_V;
        Panel panel(&w,2);Frame(panel,1100);Frame(panel,1100);
        const auto depth=w.Current().undo.GetUndoDepth();const auto* window=ImGui::FindWindowByName("Polish UI");
        const auto row=ImHashStr("Work function phi_m [eV]",0,ImHashStr(side,0,window->ID));const auto table=ImHashStr("Work function phi_m [eV]",0,row);const auto id=ImHashStr("##value",0,table);
        auto& io=ImGui::GetIO();ImVec2 at;bool found=false;
        for(float y=60;y<800 && !found;y+=6){io.AddMousePosEvent(700,y);Frame(panel,1100);Frame(panel,1100);if(GImGui->HoveredId==id){at={700,y};found=true;}}
        ASSERT_TRUE(found)<<side;io.AddKeyEvent(ImGuiMod_Shift,true);Frame(panel,1100);io.AddMouseButtonEvent(0,true);Frame(panel,1100);
        for(int step=1;step<=10;++step){io.AddMousePosEvent(at.x+step,at.y);Frame(panel,1100);}
        io.AddMouseButtonEvent(0,false);Frame(panel,1100);
        const auto& contact=std::string_view(side)=="left"?w.Current().draft->project.left_contact:w.Current().draft->project.right_contact;
        EXPECT_NEAR(contact.phi_m_eV,derived,.05);EXPECT_GT(std::abs(contact.phi_m_eV-7.75),.1);
        io.AddKeyEvent(ImGuiMod_Shift,false);Frame(panel,1100);
        EXPECT_FALSE(contact.flat_band.value());EXPECT_EQ(w.Current().undo.GetUndoDepth(),depth+1);EXPECT_NE(w.status.find("took over flat band"),std::string::npos);
        io.AddMousePosEvent(at.x,at.y);io.AddMouseButtonEvent(1,true);Frame(panel,1100);io.AddMouseButtonEvent(1,false);Frame(panel,1100);Frame(panel,1100,true);
        EXPECT_NE(logged.find("Quick batch:"),std::string::npos)<<side;
        io.AddKeyEvent(ImGuiKey_Escape,true);Frame(panel,1100);io.AddKeyEvent(ImGuiKey_Escape,false);Frame(panel,1100);
        ASSERT_TRUE(w.Exec(Cmd::Undo::With()).value("ok",false));const auto& restored=std::string_view(side)=="left"?w.Current().draft->project.left_contact:w.Current().draft->project.right_contact;
        EXPECT_TRUE(restored.flat_band.value());EXPECT_EQ(restored.phi_m_eV,std::string_view(side)=="left"?draft->project.left_contact.phi_m_eV:draft->project.right_contact.phi_m_eV);
        io.AddMousePosEvent(at.x,at.y);io.AddMouseButtonEvent(0,true);Frame(panel,1100);io.AddMousePosEvent(at.x+20,at.y);Frame(panel,1100);
        io.AddKeyEvent(ImGuiKey_Escape,true);Frame(panel,1100);io.AddKeyEvent(ImGuiKey_Escape,false);io.AddMouseButtonEvent(0,false);Frame(panel,1100);
        const auto& cancelled=std::string_view(side)=="left"?w.Current().draft->project.left_contact:w.Current().draft->project.right_contact;
        EXPECT_TRUE(cancelled.flat_band.value());EXPECT_EQ(cancelled.phi_m_eV,std::string_view(side)=="left"?draft->project.left_contact.phi_m_eV:draft->project.right_contact.phi_m_eV);EXPECT_EQ(w.Current().undo.GetUndoDepth(),depth);
    }
}
TEST_F(PolishUi,DrawnContactAndDopingFramesKeepGestureMeshAndSampling) {
    const char* evidence=std::getenv("SCAPSLAB_LIVE_EVIDENCE");std::ofstream output;if(evidence)output.open(evidence);
    if(output)output<<std::setprecision(17)<<"gesture,frame,parameter,render_ms,frame_period_ms,band_revision,x_um,Ec_eV\n";
    for(int parameter=0;parameter<3;++parameter) {
        Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);w.autoRecalculate=false;w.autoRecalculateBand=true;
        auto draft=CreateRef<ProjectDraft>(*w.Current().draft);draft->project.left_contact.flat_band=draft->project.right_contact.flat_band=false;draft->project.left_contact.phi_m_eV=4.3;draft->project.right_contact.phi_m_eV=4.7;w.Current().SetDraft(draft);
        ASSERT_TRUE(w.Exec(Cmd::RecalculateLive::Make(ActionKind::Band)).value("ok",false));Finish(w);ASSERT_TRUE(w.Current().warmBand);
        const auto grid=w.Current().warmBand->x_m;Panel panel(&w,5);Frame(panel,1100);Frame(panel,1100);
        w.dragging=w.interacting=true;w.lastFullMs=200;w.OnUpdate(0);const auto samples=w.Current().liveBiases;
        const auto start=std::chrono::steady_clock::now();auto lastFrame=start;std::vector<double> previous;double maxDelta=0;int drawn=0;
        for(int frame=0;frame<120;++frame) {
            const auto frameStart=std::chrono::steady_clock::now();const double period=frame==0?1000./60:std::chrono::duration<double,std::milli>(frameStart-lastFrame).count();lastFrame=frameStart;ImGui::GetIO().DeltaTime=float(period/1000.);
            double value=parameter==2?1.+.001*frame:(parameter==0?4.3:4.7)+.0001*frame;
            auto next=CreateRef<ProjectDraft>(*w.Current().draft);if(parameter==0)next->project.left_contact.phi_m_eV=value;else if(parameter==1)next->project.right_contact.phi_m_eV=value;else {auto& layer=next->project.layers.front();const auto& base=draft->project.layers.front();if(base.Nd_m3>=base.Na_m3)layer.Nd_m3=base.Nd_m3*value;else layer.Na_m3=base.Na_m3*value;}
            ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,"recorded-drag")).value("ok",false));w.lastFullMs=frame%2?50:500;w.OnUpdate(0);
            const auto t=std::chrono::steady_clock::now();Frame(panel,1100);const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
            EXPECT_EQ(w.Current().liveStride,4);EXPECT_EQ(w.Current().liveBiases,samples);ASSERT_TRUE(w.Current().liveMesh);EXPECT_EQ(*w.Current().liveMesh,grid);
            EXPECT_EQ(w.Current().Band()->mesh_x_m.get(),grid);EXPECT_EQ(w.Current().warmBand->x_m,grid);
            for(const auto& [id,c]:w.plotSession.curves)if(id.ends_with("/Ec_eV")){
                if(previous.size()==c.y.size())for(std::size_t i=0;i<c.y.size();++i)maxDelta=std::max(maxDelta,std::abs(c.y[i]-previous[i]));previous=c.y;++drawn;
                if(output)for(std::size_t i=0;i<c.x.size();++i)output<<parameter<<','<<frame<<','<<value<<','<<ms<<','<<period<<','<<w.Current().bandRevision<<','<<c.x[i]<<','<<c.y[i]<<'\n';
            }
            std::this_thread::sleep_until(frameStart+std::chrono::milliseconds(15));
        }
        EXPECT_GT(drawn,100);EXPECT_LT(maxDelta,.02);EXPECT_GT(w.liveBandsStarted,2u);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::cout<<"drawn gesture "<<parameter<<": max Ec frame delta "<<maxDelta<<" eV, "<<w.liveBandsStarted/seconds<<" solves/s, "<<120/seconds<<" frames/s\n";
        w.dragging=w.interacting=false;w.autoRecalculateBand=false;Finish(w);w.OnUpdate(0);EXPECT_FALSE(w.Current().liveGesture);EXPECT_FALSE(w.Current().liveMesh);
    }
}

TEST_F(PolishUi,SpatialLegendsShowOnlyPrimaryQuantitiesAndLiveIsGhost) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);w.autoRecalculate=w.autoRecalculateBand=false;
    w.plotSession.settings.grFollow=false;
    const auto band=[&]{EXPECT_TRUE(w.Dispatch({{"operation","band"},{"live",true},{"voltage_V",0.}}).value("ok",false));Finish(w);};band();
    for(int view:{5,22}){Panel panel(&w,view);Frame(panel,1100);Frame(panel,1100);}
    std::map<ImGuiID,std::set<std::string>> primaryLabels;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto* plot=GImPlot->Plots.GetByIndex(i);for(int j=0;j<plot->Items.GetLegendCount();++j)primaryLabels[plot->ID].insert(plot->Items.GetLegendLabel(j));}
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=301;AddDraftDefect(*next,0,false);ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));band();
    for(int view:{5,22}){Panel panel(&w,view);Frame(panel,1100);Frame(panel,1100);if(view==5)EXPECT_EQ(w.plotSession.morphFrame,ImGui::GetFrameCount());}
    int blue=0,red=0,references=0,ghosts=0;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto* plot=GImPlot->Plots.GetByIndex(i);for(int j=0;j<plot->Items.GetLegendCount();++j){
        const std::string label=plot->Items.GetLegendLabel(j);const auto colour=plot->Items.GetLegendItem(j)->Color;const auto alpha=(colour>>IM_COL32_A_SHIFT)&255u;
        EXPECT_TRUE(primaryLabels[plot->ID].contains(label))<<label;
        EXPECT_EQ(alpha,255u);
        if(label=="Reference (40%)"){++references;}
        else if(label=="Ec_eV" || label=="n"){EXPECT_EQ(colour,ImGui::ColorConvertFloat4ToU32(XColour("Ec_eV")));++blue;}
        else if(label=="Ev_eV" || label=="p"){EXPECT_EQ(colour,ImGui::ColorConvertFloat4ToU32(XColour("Ev_eV")));++red;}
    }for(int j=0;j<plot->Items.ItemPool.GetBufSize();++j)if(((plot->Items.ItemPool.GetByIndex(j)->Color>>IM_COL32_A_SHIFT)&255u)==128u)++ghosts;}EXPECT_EQ(blue,2);EXPECT_EQ(red,2);EXPECT_EQ(references,0);EXPECT_GE(ghosts,7);
}

TEST_F(PolishUi,InactiveDeviceWithRetainedPreviewKeepsItsComparisonCurve) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.autoRecalculate=w.autoRecalculateBand=false;w.lightResponse=true;
    const auto first=w.Current().id;ASSERT_TRUE(w.Dispatch({{"operation","device_add"}}).value("ok",false));
    for(const auto& d:w.project.devices){auto iv=CreateRef<IvResult>();for(int i=0;i<2;++i){IvPoint p;p.voltage_V=.1*i;p.J_A_m2=1+i;p.valid=true;iv->points.push_back(p);}d->result.adopt(iv);w.project.compare_set.insert(d->id);}
    auto& inactive=w.Device(first);ASSERT_NE(first,w.Current().id);inactive.previewV={0,.1};inactive.previewJ={2,3};Panel panel(&w,10);Frame(panel,1100);Frame(panel,1100);
    int curves=0;for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto* plot=GImPlot->Plots.GetByIndex(i);for(int j=0;j<plot->Items.GetLegendCount();++j)if(plot->Items.GetLegendItem(j)->Color==ImGui::ColorConvertFloat4ToU32(SingleShotColour))++curves;}
    EXPECT_EQ(curves,2);
}


TEST_F(PolishUi,ContactSweepReportsCarrierDataAndSettlesAfterRelease) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));Small(w);w.autoRecalculate=false;w.autoRecalculateBand=true;
    auto draft=CreateRef<ProjectDraft>(*w.Current().draft);draft->project.left_contact.flat_band=draft->project.right_contact.flat_band=false;draft->project.left_contact.phi_m_eV=4.3;draft->project.right_contact.phi_m_eV=4.7;w.Current().SetDraft(draft);
    Panel panel(&w,5);
    for(double phi:{4.3,4.4,4.7,5.,5.5,6.,2.4,4.7}) {
        auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.left_contact.phi_m_eV=phi;
        ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,"carrier-sweep")).value("ok",false));
        ASSERT_TRUE(w.Exec(Cmd::RecalculateLive::Make(ActionKind::Band)).value("ok",false));Finish(w);
        for(int f=0;f<30;++f)Frame(panel,1100);
        const auto& band=*w.Current().Band();ASSERT_FALSE(band.points.empty());const auto& p=band.points.front();double maximum=0;
        for(const auto* values:{&p.n_m3,&p.p_m3})for(double value:values->get())if(std::isfinite(value))maximum=std::max(maximum,value*1e-6);
        const auto& runtime=w.plotSession.runtime["Carriers"];
        std::cout<<"carrier phi="<<phi<<" status="<<p.status<<" valid="<<p.valid<<" max_cm3="<<maximum<<" n0_cm3="<<p.n_m3.get().front()*1e-6<<" n1_cm3="<<p.n_m3.get()[1]*1e-6<<" nmid_cm3="<<p.n_m3.get()[p.n_m3.size()/2]*1e-6<<" range="<<runtime.axes[ImAxis_Y1].current.min<<","<<runtime.axes[ImAxis_Y1].current.max<<" hold="<<w.plotSession.holdFit<<"\n";
        EXPECT_FALSE(w.plotSession.holdFit);EXPECT_EQ(runtime.axes[ImAxis_Y1].current,runtime.axes[ImAxis_Y1].target);if(phi==4.7)EXPECT_LT(runtime.axes[ImAxis_Y1].current.max,1e25);
    }
}

TEST_F(PolishUi,ReferenceOnlySheetChannelKeepsAxisAndLegend) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.autoRecalculate=w.autoRecalculateBand=false;w.plotSession.settings.grFollow=false;
    auto draft=CreateRef<ProjectDraft>(*DraftFromDocument(ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string())));draft->project.mesh.points_per_layer=11;draft->project.voltage={0,.02,.01};AddDraftDefect(*draft,0,true);w.Current().SetDraft(draft);
    const auto solve=[&]{EXPECT_TRUE(w.Exec(Cmd::RecalculateLive::Make(ActionKind::Band)).value("ok",false));Finish(w);};
    const auto positiveSheet=[](const IvPoint& p){if(p.details)for(const auto& c:p.details->generation_recombination.channels)if(c.valid)for(double value:c.R_sheet_m2_s.get())if(std::isfinite(value) && value>0)return true;return false;};
    solve();ASSERT_FALSE(w.Current().Band()->points.empty());ASSERT_TRUE(positiveSheet(w.Current().Band()->points.front()));
    Panel panel(&w,22);Frame(panel,1100);Frame(panel,1100);ImPlotPlot* plot=nullptr;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto* candidate=GImPlot->Plots.GetByIndex(i);if(candidate->HasTitle() && std::string(candidate->GetTitle())=="G(x), R(x)")plot=candidate;}
    ASSERT_NE(plot,nullptr);ASSERT_TRUE(plot->Axes[ImAxis_Y2].Enabled);std::map<std::string,ImU32> before;
    for(int i=0;i<plot->Items.GetLegendCount();++i)before[plot->Items.GetLegendLabel(i)]=plot->Items.GetLegendItem(i)->Color;
    auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.interface_defects.clear();ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next)).value("ok",false));solve();
    ASSERT_FALSE(w.Current().Band()->points.empty());EXPECT_FALSE(positiveSheet(w.Current().Band()->points.front()));ASSERT_NE(w.Current().ChangePoint(),nullptr);EXPECT_TRUE(positiveSheet(*w.Current().ChangePoint()));
    Frame(panel,1100);Frame(panel,1100);EXPECT_TRUE(plot->Axes[ImAxis_Y2].Enabled);std::map<std::string,ImU32> after;
    for(int i=0;i<plot->Items.GetLegendCount();++i)after[plot->Items.GetLegendLabel(i)]=plot->Items.GetLegendItem(i)->Color;EXPECT_EQ(after,before);
    EXPECT_TRUE(w.plotSession.runtime["G(x), R(x)"].hasData);
    const RecombinationChannel* sheet=nullptr;
    for(const auto& c:w.Current().ChangePoint()->details->generation_recombination.channels)if(c.valid && !c.R_sheet_m2_s.empty() && std::isfinite(c.integrated_current_A_m2) && c.integrated_current_A_m2!=0){sheet=&c;break;}
    ASSERT_NE(sheet,nullptr);const auto name=sheet->mechanism+" / "+sheet->id;
    w.plotSession.settings.grTab=1;Panel budget(&w,22);Frame(budget,1100);Frame(budget,1100);ImPlotPlot* bars=nullptr;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto* candidate=GImPlot->Plots.GetByIndex(i);if(candidate->HasTitle() && std::string(candidate->GetTitle())=="Integrated loss breakdown")bars=candidate;}
    ASSERT_NE(bars,nullptr);ImGuiID sheetId=0;
    for(int i=0;i<bars->Items.GetLegendCount();++i)if(name==bars->Items.GetLegendLabel(i)){const auto* item=bars->Items.GetLegendItem(i);sheetId=item->ID;EXPECT_EQ(item->Color,ImGui::ColorConvertFloat4ToU32(XColour(sheet->mechanism)));}
    ASSERT_NE(sheetId,0u);
    for(int i=0;i<bars->Items.ItemPool.GetBufSize();++i){auto* item=bars->Items.ItemPool.GetByIndex(i);item->Show=item->ID==sheetId;}
    for(int i=0;i<30;++i)Frame(budget,1100);
    const double lo=std::min(0.,sheet->integrated_current_A_m2),hi=std::max(0.,sheet->integrated_current_A_m2),margin=(hi-lo)*w.plotSession.settings.margin*.01,tolerance=std::abs(sheet->integrated_current_A_m2)*1e-8+1e-20;
    EXPECT_NEAR(bars->Axes[ImAxis_Y1].Range.Min,lo-margin,tolerance);EXPECT_NEAR(bars->Axes[ImAxis_Y1].Range.Max,hi+margin,tolerance);
}
