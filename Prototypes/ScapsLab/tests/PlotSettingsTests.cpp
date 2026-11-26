#include <gtest/gtest.h>
#include "PlotUi.hpp"
#include "Panel.hpp"
#include "Io.hpp"
#include "Core/Presentation/Panels/PanelRegistry.hpp"
#include "Core/Presentation/Panels/WindowWorkspace.hpp"
#include <imgui_internal.h>
#include <implot_internal.h>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
constexpr auto GoodDock="[Window][sst-window-9]\nDockId=0x00000004,0\n\n[Docking][Data]\nDockSpace ID=0x11111111 Pos=0,28 Size=1000,772 Split=X\n DockNode ID=0x00000004 Parent=0x11111111 SizeRef=250,772\n";
class SavedPlots : public ::testing::Test {
protected:
    PlotSession session;ImPlotPlot *plot=nullptr;
    void SetUp() override {ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1000,800);io.Fonts->AddFontDefault();io.Fonts->Build();}
    void TearDown() override {ImPlot::DestroyContext();ImGui::DestroyContext();}
    void Frame(int count=2,bool log=false,std::function<void()> edit={}) {
        auto &io=ImGui::GetIO();io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(900,700));ImGui::Begin("Plot test");
        ASSERT_TRUE(BeginSavedPlot(session,"Test",ImVec2(-1,500)));ImPlot::SetupAxes("V","J");if(log)ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);SetupSavedPlot(session,"Test");
        const double x[]={10,20},y[]={1e-6,1e-2};ImPlot::PlotLine("Data",x,y,count);plot=ImPlot::GetCurrentPlot();if(edit)edit();EndSavedPlot(session,"Test");ImGui::End();ImGui::Render();
    }
};
}
TEST(PlotSettings,LinearAndLogMargin) {
    const auto a=PadPlotRange({10,20},5,false);EXPECT_DOUBLE_EQ(a.min,9.5);EXPECT_DOUBLE_EQ(a.max,20.5);
    const auto b=PadPlotRange({1e-6,1e-2},5,true);EXPECT_NEAR(std::log10(b.min),-6.2,1e-12);EXPECT_NEAR(std::log10(b.max),-1.8,1e-12);
    EXPECT_GE(PadPlotRange({1e-200,1},30,true).min,1e-200);EXPECT_EQ(PadPlotRange({10,20},0,false),(PlotRange{10,20}));
    const auto wide=PadPlotRange({1e-190,1e8},5,true);EXPECT_NEAR(std::log10(wide.max),8.5,1e-12);EXPECT_NEAR(std::log10(wide.min),-190.5,1e-12);
}
TEST(SmoothPlotAxis,UnionAndExpandOnlyDuringGestureAndPending) {
    SmoothPlotAxis axis;axis.Set({0,10},false);
    axis.Fit({-5,20},false,true);EXPECT_EQ(axis.target,(PlotRange{-5,20}));
    axis.Step(.1,true);const auto drawn=axis.current;
    axis.Fit({2,4},false,true);EXPECT_EQ(axis.target,(PlotRange{-5,20}));
    axis.Step(.1,true);EXPECT_LE(axis.current.min,drawn.min);EXPECT_GE(axis.current.max,drawn.max);
    // Pending uses the same hold after the gesture has ended.
    axis.Fit({1,3},false,true);axis.Step(.2,true);EXPECT_EQ(axis.current,(PlotRange{-5,20}));
}
TEST(SmoothPlotAxis,InterpolationIsMonotoneAndFinishesIn200ms) {
    for(bool log:{false,true}) {
        SmoothPlotAxis axis;axis.Set({1,10},log);axis.Fit({.01,1000},log,true);
        auto previous=axis.current;
        for(int i=0;i<20;++i){const auto now=axis.Step(.01,true);EXPECT_LE(now.min,previous.min);EXPECT_GE(now.max,previous.max);EXPECT_TRUE(now.Valid(log));previous=now;}
        EXPECT_EQ(axis.current,axis.target);
        EXPECT_GT(axis.Step(0,true).min,0);
    }
}
TEST(SmoothPlotAxis,ManualLockAndPostReleaseEase) {
    SmoothPlotAxis axis;axis.Set({0,20},false);
    axis.Fit({5,10},false,false,true);axis.Step(1,false,true);EXPECT_EQ(axis.current,(PlotRange{0,20}));
    axis.Fit({5,10},false,false);const auto halfway=axis.Step(.1,false);
    EXPECT_GT(halfway.min,0);EXPECT_LT(halfway.min,5);EXPECT_GT(halfway.max,10);EXPECT_LT(halfway.max,20);
    axis.Step(.1,false);EXPECT_EQ(axis.current,(PlotRange{5,10}));
    axis.Fit({7,8},false,false);axis.Step(.05,false);const auto release=axis.current;
    axis.Step(.05,true);EXPECT_EQ(axis.current,release); // a new gesture cancels an in-progress shrink
}
TEST(SmoothPlotAxis,InvalidExtentAndScaleChange) {
    SmoothPlotAxis axis;axis.Set({1,10},false);axis.Fit({NAN,2},false,true);EXPECT_EQ(axis.target,(PlotRange{1,10}));
    axis.Fit({1e-9,1e-3},true,true);EXPECT_TRUE(axis.log);EXPECT_EQ(axis.current,(PlotRange{1e-9,1e-3}));
}
TEST_F(SavedPlots,LiveFitEasesUnionAndKeepsManualZoom) {
    Frame();Frame();session.smoothFit=session.holdFit=true;
    const auto initial=plot->Axes[0].Range;
    const auto more=[] {const double x[]={-100,200},y[]={1,2};ImPlot::PlotLine("Live",x,y,2);};
    Frame(2,false,more);EXPECT_DOUBLE_EQ(plot->Axes[0].Range.Min,initial.Min);
    auto previous=plot->Axes[0].Range;
    for(int i=0;i<15;++i){Frame(2,false,more);const auto now=plot->Axes[0].Range;EXPECT_LE(now.Min,previous.Min);EXPECT_GE(now.Max,previous.Max);previous=now;}
    EXPECT_LT(previous.Min,-100);EXPECT_GT(previous.Max,200);
    for(int i=0;i<3;++i){Frame();EXPECT_DOUBLE_EQ(plot->Axes[0].Range.Min,previous.Min);EXPECT_DOUBLE_EQ(plot->Axes[0].Range.Max,previous.Max);}
    auto &v=session.settings.plots["Test"];v.manual=true;v.ranges[0]=PlotRange{11,12};Frame(2,false,more);EXPECT_EQ(plot->Axes[0].Range.Min,11);EXPECT_EQ(plot->Axes[0].Range.Max,12);
    v.manual=false;v.ranges={};session.holdFit=false;Frame();
    for(int i=0;i<15;++i)Frame();EXPECT_NEAR(plot->Axes[0].Range.Min,9.5,1e-6);
}
TEST(PlotSettings,InvalidRangesAndOptionalProjectOverrides) {
    PlotSettings global;global.margin=12;global.layout=GoodDock;global.panels["I-V"]=false;global.plots["Test"].log[3]=true;global.plots["Test"].manual=true;global.plots["Test"].ranges[3]=PlotRange{1e-8,1};
    global.plots["Test"].legendLocation=ImPlotLocation_Center;
    PlotSettings loaded;loaded.Read(global.Write());EXPECT_EQ(loaded,global);
    loaded.Read(nlohmann::json::object());EXPECT_EQ(loaded,global);
    loaded.Read({{"auto_fit_margin_percent",7},{"panels",{{"I-V",true}}}});EXPECT_EQ(loaded.margin,7);EXPECT_TRUE(loaded.panels["I-V"]);EXPECT_EQ(loaded.layout,GoodDock);
    loaded.Read({{"plots",{{"Bad",{1.,0.,0.,1.}}}}});EXPECT_FALSE(loaded.plots["Bad"].ranges[0]);
    loaded.Read({{"plot_settings",{{"Bad",{{"manual",true},{"axes",{{{"log",true},{"range",{1e-300,1.}}}}}}}}}});EXPECT_FALSE(loaded.plots["Bad"].ranges[0]);
    EXPECT_FALSE((PlotRange{NAN,1}).Valid());EXPECT_FALSE((PlotRange{1,1}).Valid());EXPECT_FALSE((PlotRange{0,1}).Valid(true));
    const nlohmann::json bad={{"plots",{{"Bad",{1.,0.,0.,1.}}}}};EXPECT_NO_THROW(ValidateUi(bad,false));EXPECT_THROW(ValidateUi(bad),std::invalid_argument);
}
TEST_F(SavedPlots,EmptyFirstFrameFitsWhenDataArrivesAndDoesNotSaveLimits) {
    Frame(0,true);EXPECT_FALSE(session.runtime["Test"].hasData);Frame(2,true);
    EXPECT_NEAR(plot->Axes[0].Range.Min,9.5,1e-6);EXPECT_NEAR(std::log10(plot->Axes[3].Range.Min),-6.2,1e-6);
    for(int i=0;i<5;++i)Frame(2,true);
    EXPECT_FALSE(session.settings.plots["Test"].manual);EXPECT_FALSE(session.settings.plots["Test"].ranges[0]);EXPECT_EQ(session.revision,0u);
    EXPECT_TRUE(session.settings.Write()["plot_settings"].empty());
}
TEST_F(SavedPlots,ZoomKeepsViewUntilDoubleClickAndHiddenItemsRoundTrip) {
    Frame();Frame();auto &io=ImGui::GetIO();const auto center=plot->PlotRect.GetCenter();io.AddMousePosEvent(center.x,center.y);Frame();io.AddMouseWheelEvent(0,1);Frame();
    ASSERT_TRUE(session.settings.plots["Test"].manual);const auto zoom=plot->Axes[0].Range;const auto revision=session.revision;
    Frame(2,false,[]{const double x[]={100,200},y[]={1,2};ImPlot::PlotLine("New data",x,y,2);});EXPECT_EQ(plot->Axes[0].Range.Min,zoom.Min);EXPECT_EQ(session.revision,revision);
    for(int click=0;click<2;++click){io.AddMouseButtonEvent(ImPlot::GetInputMap().Fit,true);Frame();io.AddMouseButtonEvent(ImPlot::GetInputMap().Fit,false);Frame();}
    EXPECT_FALSE(session.settings.plots["Test"].manual);EXPECT_NEAR(plot->Axes[0].Range.Min,9.5,1e-6);
    Frame(2,false,[&]{plot->Items.GetItemByIndex(0)->Show=false;});EXPECT_EQ(session.settings.plots["Test"].hidden.size(),1u);
    PlotSettings restored;restored.Read(session.settings.Write());EXPECT_EQ(restored.plots,session.settings.plots);
}
TEST_F(SavedPlots,SavedRangeOnlyRestoresWithDataAndInvalidLogRangeIsIgnored) {
    auto &v=session.settings.plots["Test"];v.manual=true;v.ranges[0]=PlotRange{11,12};v.ranges[3]=PlotRange{1e-5,1e-3};
    Frame(0,true);EXPECT_NE(plot->Axes[0].Range.Min,11);Frame(2,true);EXPECT_EQ(plot->Axes[0].Range.Min,11);
    v.ranges[3]=PlotRange{1e-300,1};session.runtime.clear();Frame(2,true);EXPECT_FALSE(v.manual);EXPECT_NEAR(std::log10(plot->Axes[3].Range.Min),-6.2,1e-6);
    session.settings.Read({{"plots",{{"Test",{1.,1.,1e-5,1e-3}}}}});session.runtime.clear();Frame(2,true);EXPECT_FALSE(v.manual);EXPECT_NEAR(plot->Axes[0].Range.Min,9.5,1e-6);
    session.ResetRanges();EXPECT_FALSE(v.ranges[0]);
}
TEST_F(SavedPlots,ExplicitFitsUseMarginAndAllowPanning) {
    Frame(2,true,[&]{FitSavedAxis(session,"Test",ImAxis_Y1,1e-8,1.);});EXPECT_NEAR(std::log10(plot->Axes[3].Range.Min),-8.4,1e-6);
    EXPECT_FALSE(plot->Axes[3].IsInputLocked());
}
TEST_F(SavedPlots,ConstantLinearSeriesAndExplicitFitStillIncludeAllData) {
    Frame(0,false,[]{const double x[]={10,10},y[]={500,500};ImPlot::PlotLine("Constant",x,y,2);});
    EXPECT_LT(plot->Axes[0].Range.Min,10);EXPECT_GT(plot->Axes[0].Range.Max,10);EXPECT_LT(plot->Axes[3].Range.Min,500);EXPECT_GT(plot->Axes[3].Range.Max,500);
    session.liveData=true;Frame(2,false,[&]{FitSavedAxis(session,"Test",ImAxis_X1,12,15);});EXPECT_NEAR(plot->Axes[0].Range.Min,9.5,1e-6);EXPECT_NEAR(plot->Axes[0].Range.Max,20.5,1e-6);
}
TEST_F(SavedPlots,LargeBatchKeepsPositivePlotArea) {
    EXPECT_FALSE(CompactBatchLegend(12));EXPECT_TRUE(CompactBatchLegend(13));
    for(int count:{3,342,3})for(int frame=0;frame<3;++frame){
        ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(800,500));ImGui::Begin("Batch test");const bool compact=CompactBatchLegend(count);
        ASSERT_TRUE(BeginSavedPlot(session,"Batch",ImVec2(-1,300)));ImPlot::SetupAxes("V","J");ImPlot::SetupLegend(ImPlotLocation_NorthWest);SetupSavedPlot(session,"Batch",compact);
        const double x[]={0,1},y[]={1,2};for(int i=0;i<count;++i)ImPlot::PlotLine(("member "+std::to_string(i)).c_str(),x,y,2);
        auto *p=ImPlot::GetCurrentPlot();EndSavedPlot(session,"Batch");EXPECT_GT(p->PlotRect.GetHeight(),150);EXPECT_EQ(bool(p->Flags&ImPlotFlags_NoLegend),compact);ImGui::End();ImGui::Render();
    }
}
TEST_F(SavedPlots,PanelRenderingDoesNotDirtyOrChangeProjectUi) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));w.Current().liveRunning=true;const auto ui=w.project.ui;Panel panel(&w,10);
    for(int i=0;i<4;++i){ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(900,700));ImGui::Begin("Compare");panel.RenderContent();ImGui::End();ImGui::Render();}
    EXPECT_EQ(w.project.ui,ui);EXPECT_FALSE(w.dirty);EXPECT_FALSE(w.projectDirty);
    w.Current().liveV={10,20};w.Current().liveJ={1e-6,1e-2};
    for(int i=0;i<2;++i){ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(900,700));ImGui::Begin("Compare");panel.RenderContent();ImGui::End();ImGui::Render();}
    bool found=false;for(int i=0;i<GImPlot->Plots.GetBufSize();++i){const auto *p=GImPlot->Plots.GetByIndex(i);if(p->HasTitle() && std::string(p->GetTitle())=="I-V compare"){found=true;EXPECT_LT(p->Axes[0].Range.Min,10);EXPECT_GT(p->Axes[0].Range.Max,20);EXPECT_LT(p->Axes[3].Range.Min,1e-6);}}
    EXPECT_TRUE(found);EXPECT_EQ(w.project.ui,ui);EXPECT_FALSE(w.dirty);
}
TEST_F(SavedPlots,SingleShotBlueAndRetainedLiveDarkerOrangeWhilePending) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.autoRecalculate=w.autoRecalculateBand=false;auto &d=w.Current();d.SetDocument(ReadProject(SCAPSLAB_PROJECT));
    auto result=CreateRef<IvResult>();for(int i=0;i<2;++i){IvPoint p;p.voltage_V=i;p.J_A_m2=i+1;p.valid=true;result->points.push_back(p);}d.result.adopt(result);
    Panel panel(&w,10);
    ImPlotPlot *p=nullptr;
    const auto frame=[&]{ImGui::NewFrame();ImGui::SetNextWindowSize({900,700});ImGui::Begin("Live style");panel.RenderContent();p=ImPlot::GetPlot("I-V compare");ImGui::End();ImGui::Render();};
    frame();frame();ASSERT_NE(p,nullptr);ASSERT_GT(p->Items.GetLegendCount(),0);
    EXPECT_EQ(p->Items.GetLegendItem(0)->Color,ImGui::ColorConvertFloat4ToU32(SingleShotColour));
    w.dragging=true;frame();w.dragging=false;frame();EXPECT_TRUE(w.plotSession.smoothFit); // bias release must keep easing
    d.changeIv=result;d.previewV={0,1};d.previewJ={2,3};d.needsRecalculate=true;w.dragging=true;
    for(int i=0;i<6;++i){frame();int blue=0,orange=0;for(int j=0;j<p->Items.GetLegendCount();++j){const auto *item=p->Items.GetLegendItem(j);blue+=item->Color==ImGui::ColorConvertFloat4ToU32(SingleShotColour);orange+=item->Color==ImGui::ColorConvertFloat4ToU32(LiveCurveColour);}EXPECT_EQ(blue,1);EXPECT_EQ(orange,1);EXPECT_EQ(d.previewJ,(std::vector<double>{2,3}));}
    EXPECT_EQ(SingleShotColour.w,1);EXPECT_EQ(LiveCurveColour.w,1);EXPECT_LT(LiveCurveColour.y,.55f);
}
TEST_F(SavedPlots,RealBatchPanelsKeepSpaceFor342Members) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));auto &d=w.Current();d.liveMemberTotal=342;
    d.SetBatchSpec({{"mode","nested"},{"axes",Json::array({{{"parameter_id","temperature_K"},{"unit","K"},{"values",{270.,300.,350.}}}})}});
    auto result=CreateRef<BatchResult>();result->native=true;result->body.prim={{"spec",{{"axes",Json::array({{{"path","temperature_K"},{"unit","K"}}})}}}};
    const auto v=Column::Intern({0,1}),j=Column::Intern({1,2});
    for(int i=0;i<342;++i){d.liveMembers.push_back({i,v,j});auto row=CreateRef<scaps_core::BatchRow>();row->id="member "+std::to_string(i);row->parameters["temperature_K"]=300.+i;row->iv={{0.,1.,1.,scaps_core::SolverStatus::Converged},{1.,2.,2.,scaps_core::SolverStatus::Converged}};result->rows.push_back(std::move(row));}
    Panel panel(&w,18);
    for(bool running:{true,false}){if(!running){d.liveMembers.clear();d.batchResult.adopt(result);}for(int f=0;f<3;++f){ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(900,700));ImGui::Begin("Real batch panels");panel.RenderContent();ImGui::End();ImGui::Render();}
        bool found=false;for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto *p=GImPlot->Plots.GetByIndex(i);if(p->HasTitle() && std::string(p->GetTitle())==(running?"Batch I-V running":"Batch I-V overlay")){found=true;EXPECT_GT(p->PlotRect.GetHeight(),200);EXPECT_TRUE(p->Flags&ImPlotFlags_NoLegend);if(running)EXPECT_EQ(p->Items.GetItemByIndex(170)->Color,ImGui::ColorConvertFloat4ToU32(ImPlot::SampleColormap(.375f,ImPlotColormap_Viridis)));}}
        EXPECT_TRUE(found);EXPECT_FALSE(w.dirty);
    }
    EXPECT_FALSE(result->HasView());
}
TEST(PlotSettings,SettingsFileUsesExistingAsyncIoAndUnchangedProjectStaysUnchanged) {
    const auto file=std::filesystem::temp_directory_path()/"slfit1-settings.json";
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.settingsPath=file.string();PlotSettings original;original.margin=17;original.layout=GoodDock;original.panels["I-V"]=false;w.settings["session"]=original.Write();
    Io::Watch(true);w.SaveSettings();while(w.IoPending()){w.PollIo();std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(Io::UiCalls().empty());Io::Watch(false);
    std::string text,error;ASSERT_TRUE(Io::Load(file,text,error));PlotSettings restored;restored.Read(nlohmann::json::parse(text).at("session"));EXPECT_EQ(restored,original);
    nlohmann::json ui={{"history_retention",5}};const auto before=ui;restored.WriteChanges(ui,restored);EXPECT_EQ(ui,before);
    restored.margin=9;restored.WriteChanges(ui,original);EXPECT_EQ(ui["auto_fit_margin_percent"],9);EXPECT_EQ(ui["imgui_ini"],GoodDock);Io::Remove(file);
}
TEST(PlotSettings,BrokenEvidenceLayoutIsRejectedForGlobalAndProjectSettings) {
    std::string broken,error;ASSERT_TRUE(Io::Load(std::filesystem::path(SCAPSLAB_PYTHON_TEST).parent_path()/"fixtures"/"broken-dock-layout.ini",broken,error))<<error;
    EXPECT_FALSE(ValidDockLayout(broken));EXPECT_TRUE(ValidDockLayout(GoodDock));EXPECT_FALSE(ValidDockLayout("not an ini"));
    const nlohmann::json ui={{"imgui_ini",broken},{"panels",{{"Run",false}}}};
    PlotSettings global;global.Read(ui);EXPECT_TRUE(global.layout.empty());EXPECT_FALSE(global.panels.at("Run"));
    PlotSettings project;project.layout=GoodDock;project.Read(ui);EXPECT_EQ(project.layout,GoodDock);EXPECT_FALSE(project.panels.at("Run"));
    project.layout=broken;EXPECT_EQ(project.Write()["imgui_ini"],"");
    std::string cycle=GoodDock;cycle.replace(cycle.find("Parent=0x11111111"),17,"Parent=0x00000004");EXPECT_FALSE(ValidDockLayout(cycle));
}
TEST_F(SavedPlots,ShutdownKeepsLastSubmittedDockSpaceAndRestoresClosedRun) {
    auto &io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));PanelRegistry registry;
    auto run=CreateRef<Panel>(&w,4);registry.AddExisting(run,9);
    auto recorder=CreateRef<Panel>(&w,17);registry.AddExisting(recorder,16);
    WindowWorkspace workspace(registry);workspace.InstallSettingsHandler();
    const auto root=ImGui::GetMainViewport()->ID;
    const auto frame=[&](bool dockSpace,bool modal=false) {
        ImGui::NewFrame();if(dockSpace)ImGui::DockSpaceOverViewport(root);
        run->Render();recorder->Render();
        if(modal){ImGui::OpenPopup("Unsaved ScapsLab project");if(ImGui::BeginPopupModal("Unsaved ScapsLab project")){EXPECT_FALSE(CaptureDockLayout(session.settings));ImGui::EndPopup();}}
    };
    ImGui::NewFrame();ImGui::DockSpaceOverViewport(root);
    for(auto panel:{run,recorder}){auto state=panel->GetState();state.dockId=root;panel->ApplyState(state);}
    ImGui::DockBuilderDockWindow(run->ImGuiName().c_str(),root);ImGui::DockBuilderDockWindow(recorder->ImGuiName().c_str(),root);ImGui::DockBuilderFinish(root);
    run->Render();recorder->Render();ImGui::Render();
    frame(true);ASSERT_TRUE(CaptureDockLayout(session.settings));ImGui::Render();
    const auto good=session.settings.layout;ASSERT_TRUE(ValidDockLayout(good));
    run->SetVisible(false);session.settings.panels["Run"]=run->IsVisible();
    frame(true,true);EXPECT_FALSE(CaptureDockLayout(session.settings,true));ImGui::Render();
    frame(false);EXPECT_FALSE(CaptureDockLayout(session.settings));ImGui::Render();
    EXPECT_FALSE(CaptureDockLayout(session.settings));EXPECT_EQ(session.settings.layout,good);
    w.settingsPath=(std::filesystem::temp_directory_path()/"slfit1b-shutdown.json").string();w.settings["session"]=session.settings.Write();w.SaveSettings();
    while(w.IoPending()){w.PollIo();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    std::string text,error;ASSERT_TRUE(Io::Load(w.settingsPath,text,error));PlotSettings restored;restored.Read(nlohmann::json::parse(text).at("session"));
    EXPECT_EQ(restored.layout,good);ASSERT_TRUE(ApplyDockLayout(restored,workspace));
    for(auto panel:{run,recorder})if(const auto it=restored.panels.find(panel->GetTitle());it!=restored.panels.end())panel->SetVisible(it->second);
    EXPECT_FALSE(run->IsVisible());
    for(int f=0;f<3;++f){frame(true);EXPECT_FALSE(ApplyDockLayout(restored,workspace));ImGui::Render();const auto *window=ImGui::FindWindowByName(recorder->ImGuiName().c_str());ASSERT_NE(window->DockNode,nullptr);EXPECT_EQ(ImGui::DockNodeGetRootNode(window->DockNode)->ID,root);EXPECT_FALSE(run->IsVisible());}
    Io::Remove(w.settingsPath);workspace.RemoveSettingsHandler();
}
TEST_F(SavedPlots,OutsideLegendsKeepPlotInsideItsFrame) {
    for(int flags:{int(ImPlotLegendFlags_Outside),ImPlotLegendFlags_Outside|ImPlotLegendFlags_Horizontal})for(int count:{3,18,100})for(int f=0;f<3;++f) {
        ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(800,600));ImGui::Begin("Outside legend test");ImGui::Dummy(ImVec2(0,200));const float top=ImGui::GetCursorScreenPos().y;
        ASSERT_TRUE(BeginSavedPlot(session,"Outside",ImVec2(-1,220)));ImPlot::SetupAxes("V","J");ImPlot::SetupLegend(ImPlotLocation_South,flags);SetupSavedPlot(session,"Outside");
        const double x[]={0,1},y[]={1,2};for(int i=0;i<count;++i)ImPlot::PlotLine(("channel "+std::to_string(i)).c_str(),x,y,2);
        auto *p=ImPlot::GetCurrentPlot();EndSavedPlot(session,"Outside");EXPECT_GT(p->PlotRect.GetHeight(),count>12 && f>0?100:60);EXPECT_GE(p->PlotRect.Min.y,top);EXPECT_LE(p->PlotRect.Max.y,p->FrameRect.Max.y);if(count>12 && f>0)EXPECT_FALSE(p->Items.Legend.Flags&ImPlotLegendFlags_Outside);
        ImGui::End();ImGui::Render();
    }
}
TEST_F(SavedPlots,Recorder18SeriesDoesNotOverlapWorksheet) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    auto result=CreateRef<BatchResult>();result->native=true;
    result->body.prim={{"columns",Json::array({{{"id","J"},{"unit","A/m^2"}}})},{"spec",{{"axes",Json::array({{{"path","voltage_V"},{"unit","V"}},{{"path","temperature_K"},{"unit","K"}}})}}}};
    for(int t=0;t<18;++t)for(int v=0;v<19;++v){auto row=CreateRef<scaps_core::BatchRow>();row->id=std::to_string(t*19+v);row->status="ok";row->parameters["temperature_K"]=110.+t*280./17;row->parameters["voltage_V"]=v*.05;row->values["J"].value=double(t+v);row->values["J"].status="ok";result->rows.push_back(std::move(row));}
    w.Current().batchResult.adopt(result);Panel panel(&w,17);
    for(int height:{700,450})for(bool legend:{true,false})for(int f=0;f<3;++f) {
        w.plotSession.settings.plots["Recorder vs parameter"].legend=legend;
        ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(1000,float(height)));ImGui::Begin("Real recorder");panel.RenderContent();ImGui::End();ImGui::Render();
        auto *table=GImGui->Tables.GetByKey(ImHashStr("Recorder worksheet",0,ImGui::FindWindowByName("Real recorder")->ID));ASSERT_NE(table,nullptr);
        bool found=false;for(int i=0;i<GImPlot->Plots.GetBufSize();++i){auto *p=GImPlot->Plots.GetByIndex(i);if(p->HasTitle() && std::string(p->GetTitle())=="Recorder vs parameter"){found=true;EXPECT_EQ(p->Items.GetItemCount(),18);EXPECT_TRUE(p->Flags&ImPlotFlags_NoLegend);EXPECT_GT(p->PlotRect.GetHeight(),100);EXPECT_GE(p->FrameRect.Min.y,table->OuterRect.Max.y);EXPECT_GE(p->PlotRect.Min.y,p->FrameRect.Min.y);}}
        EXPECT_TRUE(found);
    }
    EXPECT_FALSE(result->HasView());EXPECT_FALSE(w.dirty);
}

TEST(PlotSettingsOwner, ContactAnnotationsDefaultOffAndKeepExplicitPresence) {
    PlotSettings settings;EXPECT_FALSE(settings.contactAnnotations.value_or(false));EXPECT_FALSE(settings.Write().contains("contact_annotations"));
    PlotSettings loaded;settings.contactAnnotations=true;Json ui=Json::object();settings.WriteChanges(ui,loaded);EXPECT_TRUE(ui.at("contact_annotations"));
    settings.contactAnnotations=false;settings.WriteChanges(ui,loaded);EXPECT_FALSE(ui.at("contact_annotations"));PlotSettings restored;restored.Read(ui);EXPECT_TRUE(restored.contactAnnotations.has_value());EXPECT_FALSE(*restored.contactAnnotations);EXPECT_FALSE(restored.Write().at("contact_annotations"));
    const auto explicitFalse=restored;restored.contactAnnotations=true;restored.WriteChanges(ui,explicitFalse);EXPECT_TRUE(ui.at("contact_annotations"));restored.contactAnnotations=false;restored.WriteChanges(ui,explicitFalse);EXPECT_FALSE(ui.at("contact_annotations"));
}

TEST(PlotSettingsOwner, BatchLogAxesAndAbsoluteCurrentAreOptionalAndPersistent) {
    PlotSettings initial,s;EXPECT_FALSE(s.Write().contains("batch_absolute"));s.batchAbsolute=true;s.plots["Batch I-V overlay"].log[ImAxis_X1]=true;s.plots["Batch I-V overlay"].log[ImAxis_Y1]=true;s.plots["Stored Actions"].log[ImAxis_Y1]=true;
    Json ui=Json::object();s.WriteChanges(ui,initial);PlotSettings loaded;loaded.Read(ui);EXPECT_EQ(loaded,s);
    s.batchAbsolute=false;s.WriteChanges(ui,loaded);loaded.Read(ui);EXPECT_FALSE(*loaded.batchAbsolute);EXPECT_TRUE(*loaded.plots["Batch I-V overlay"].log[ImAxis_X1]);
}

TEST(PlotBounds,PairedSamplesFiniteLogEmptyAndMismatch) {
    const double x[]={-1,0,1,2,3,1e90,NAN,7,8},y[]={-1,2,3,4,INFINITY,NAN,1e90,0,-2};
    auto bounds=FitPlotBounds(x,y);ASSERT_TRUE(bounds);EXPECT_EQ(bounds->x,(PlotRange{-1,8}));EXPECT_EQ(bounds->y,(PlotRange{-2,4}));
    bounds=FitPlotBounds(x,y,true,true);ASSERT_TRUE(bounds);EXPECT_EQ(bounds->x,(PlotRange{1,2}));EXPECT_EQ(bounds->y,(PlotRange{3,4}));
    EXPECT_FALSE(FitPlotBounds({},{}));EXPECT_FALSE(FitPlotBounds(x,{}));const double bad[]={NAN,INFINITY};EXPECT_FALSE(FitPlotBounds(bad,bad));
    const double zero[]={0,-1};EXPECT_FALSE(FitPlotBounds(zero,zero,true,true));
    const double tiny[]={1e-190,1e-160};bounds=FitPlotBounds(tiny,tiny,true,true);ASSERT_TRUE(bounds);EXPECT_EQ(bounds->y,(PlotRange{1e-190,1e-160}));
}
TEST_F(SavedPlots,EveryPlotFamilyFitsVisibleDrawnPairsAndMarkersOnly) {
    for(const char* title:{"I-V linear","I-V log","C-V","QE","Bands / contacts","Carriers","G(x), R(x)","DLTS transient","DLCP profile","Contact work functions","Library preview"}) {
        for(int frame=0;frame<2;++frame) {
            ImGui::NewFrame();ImGui::SetNextWindowSize({900,700});ImGui::Begin("Fit families");
            ASSERT_TRUE(BeginSavedPlot(session,title,{-1,500}));ImPlot::SetupAxes("x","y");const bool log=std::string_view(title)=="I-V log" || std::string_view(title)=="Carriers" || std::string_view(title)=="G(x), R(x)";
            if(log)ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);SetupSavedPlot(session,title);
            const double x[]={10,20,1e90,NAN},y[]={2,4,NAN,1e54};DrawPlotLine("Primary",x,y,4);
            const double liveX[]={8,22},liveY[]={1,5};DrawPlotLine("##live",liveX,liveY,2);
            const double markX=25,markY=6;DrawPlotScatter("Selected",&markX,&markY,1);
            ImPlot::HideNextItem(true,ImPlotCond_Always);DrawPlotLine("Hidden",x+2,y+3,1);
            const double deco[]={-1e30,1e30};DrawPlotLine("##decoration",deco,deco,2,{ImPlotProp_Flags,ImPlotItemFlags_NoFit});
            auto* current=ImPlot::GetCurrentPlot();EndSavedPlot(session,title);
            EXPECT_NEAR(current->Axes[0].Range.Min,7.15,1e-10)<<title;EXPECT_NEAR(current->Axes[0].Range.Max,25.85,1e-10)<<title;
            EXPECT_LT(current->Axes[3].Range.Max,10)<<title;EXPECT_GT(current->Axes[3].Range.Max,6)<<title;
            ImGui::End();ImGui::Render();
        }
    }
}
TEST_F(SavedPlots,NoStaleFitAfterHidingOrReplacingDataAndLargeConstants) {
    for(int frame=0;frame<3;++frame) {
        ImGui::NewFrame();ImGui::SetNextWindowSize({900,700});ImGui::Begin("Fresh fit");ASSERT_TRUE(BeginSavedPlot(session,"Fresh",{-1,500}));ImPlot::SetupAxes("x","y");SetupSavedPlot(session,"Fresh");
        const double x[]={1,2},y[]={1,2},huge[]={1e54,1e54};
        ImPlot::HideNextItem(frame>0,ImPlotCond_Always);DrawPlotLine("Old spike",x,huge,2);DrawPlotLine("Now",x,y,2);
        auto* current=ImPlot::GetCurrentPlot();EndSavedPlot(session,"Fresh");if(frame>0)EXPECT_LT(current->Axes[3].Range.Max,3);
        ImGui::End();ImGui::Render();
    }
}
TEST_F(SavedPlots,RestoredGlobalLockDoesNotFreezeScapsLabPanels) {
    auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;PanelRegistry registry;auto workspace=CreateRef<WindowWorkspace>(registry);workspace->InstallSettingsHandler();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));w.autoRecalculate=w.autoRecalculateBand=false;
    for(int view=0;view<26;++view){auto panel=CreateRef<Panel>(&w,view);registry.AddExisting(panel,view+1);}
    ASSERT_TRUE(workspace->Apply({WindowAction::LockLayout}));EXPECT_TRUE(workspace->IsLocked());
    const std::string saved=ImGui::SaveIniSettingsToMemory();const auto section=saved.find("[SSTWindows][Workspace]");ASSERT_NE(section,std::string::npos);
    PlotSettings restored;restored.layout=GoodDock+saved.substr(section);ASSERT_TRUE(ApplyDockLayout(restored,*workspace));EXPECT_FALSE(workspace->IsLocked());
    ImGui::NewFrame();ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);workspace->RenderOverlays();
    for(const auto& panel:workspace->Windows()) {
        panel->Render();const auto* native=ImGui::FindWindowByName(panel->ImGuiName().c_str());ASSERT_NE(native,nullptr);
        EXPECT_EQ(native->Flags&(ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoDocking),0)<<panel->GetTitle();
        EXPECT_EQ(native->WindowClass.DockNodeFlagsOverrideSet&(ImGuiDockNodeFlags_NoResize|ImGuiDockNodeFlags_NoUndocking|ImGuiDockNodeFlags_NoDocking),0)<<panel->GetTitle();
    }
    ImGui::Render();workspace->RemoveSettingsHandler();
    ASSERT_TRUE(workspace->Apply({WindowAction::LockLayout}));EXPECT_TRUE(workspace->IsLocked()); // explicit locking remains available
}
