#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Core/Commands/CommandPalette.hpp"
#include "Core/Presentation/CommandMenu.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include "Core/Utils/FramebufferScreenshot.hpp"
#include "Panel.hpp"
#include "scripts/tests/UiScreenshotContext.hpp"
#include "Core/Presentation/Panels/HostedEditorWindow.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "Vendor/ImGui/backends/imgui_impl_opengl3.h"
#include <imgui_internal.h>
#include <implot.h>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Json Definition(int count=1) {
    Json steps=Json::array();
    for(int i=0;i<count;++i)steps.push_back({{"id",std::to_string(i)},{"target",{{"window","test"},{"item","button"}}},{"instruction","Press the button"},{"command",{{"operation","test"}}},{"condition",{{"predicate","value"},{"op","ge"},{"value",2}}}});
    return {{"schema_version",1},{"id","test"},{"title","Test"},{"steps",steps}};
}
}
TEST(TutorialEngine,LearnObservesStateWithoutExecutingAndResumes) {
    int value=0,executions=0;Tutorial t;Tutorial::Predicates predicates{{"value",[&]{return Json(value);}}};
    auto execute=[&](const Json &){++executions;return Json{{"ok",true}};};
    t.Start(Definition(2),"learn",predicates,1);
    t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_TRUE(t.Active());
    value=2;t.Tick(predicates,execute,true,{{"ok",true}});EXPECT_TRUE(t.Active());
    t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_EQ(executions,0);EXPECT_EQ(t.Report().at("status"),"passed");
}
TEST(TutorialEngine,LearnNeverAutoAdvancesOnAlreadyTrueStepAndSupportsBackSkipShowMe) {
    int value=2,executions=0;Tutorial t;Tutorial::Predicates predicates{{"value",[&]{return Json(value);}}};
    auto execute=[&](const Json &){++executions;return Json{{"ok",true}};};
    t.Start(Definition(2),"learn",predicates);
    t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_TRUE(t.Active());EXPECT_TRUE(t.AlreadyDone());EXPECT_EQ(t.Report().at("step_index"),0);
    t.Skip();EXPECT_EQ(t.Report().at("step_index"),1);t.Back();EXPECT_EQ(t.Report().at("step_index"),0);EXPECT_FALSE(t.CanGoBack());
    value=0;t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_FALSE(t.AlreadyDone());
    t.ShowMe();t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_EQ(executions,1);
    value=2;t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_EQ(t.Report().at("step_index"),1);
}
TEST(TutorialEngine,VerifyWaitsForJobThenChecksAndReportsSkippedSteps) {
    int value=0,executions=0;Tutorial t;Tutorial::Predicates predicates{{"value",[&]{return Json(value);}}};
    auto execute=[&](const Json &){++executions;return Json{{"ok",true}};};
    t.Start(Definition(3),"verify",predicates);
    t.Tick(predicates,execute,true,{{"ok",true}});EXPECT_EQ(executions,0);
    t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_EQ(executions,1);
    t.Tick(predicates,execute,true,nullptr);EXPECT_TRUE(t.Report().at("steps").empty());
    value=2;t.Tick(predicates,execute,false,{{"ok",true}});EXPECT_EQ(t.Report().at("steps")[0].at("status"),"PASS");
    t.Tick(predicates,execute,false,{{"ok",true}});value=0;
    t.Tick(predicates,execute,false,{{"ok",true}});
    EXPECT_EQ(t.Report().at("status"),"failed");EXPECT_EQ(t.Report().at("steps")[1].at("error").at("code"),"tutorial.condition_failed");
    EXPECT_EQ(t.Report().at("steps")[2].at("status"),"SKIP");
}
TEST(TutorialEngine,RejectsMalformedDataAndReportsCommandFailureAndTimeout) {
    Tutorial t;Tutorial::Predicates predicates{{"value",[]{return Json(2);}}};
    EXPECT_THROW(t.Start(Definition(),"bad",predicates),std::invalid_argument);
    EXPECT_THROW(t.Start(Definition(),"learn",{}),std::invalid_argument);
    auto bad=Definition(2);bad["steps"][1]["id"]="0";EXPECT_THROW(t.Start(bad,"learn",predicates),std::invalid_argument);
    bad=Definition();bad["steps"][0]["condition"]["value"]="bad";EXPECT_THROW(t.Start(bad,"verify",predicates),std::invalid_argument);
    t.Start(Definition(),"demo",predicates);
    t.Tick(predicates,[](const Json &){return Json{{"ok",false},{"error",{{"code","busy"}}}};},false,{});
    EXPECT_EQ(t.Report().at("steps")[0].at("error").at("code"),"tutorial.command_failed");
    auto timed=Definition();timed["steps"][0]["timeout_s"]=1e-9;t.Start(timed,"verify",predicates);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    t.Tick(predicates,[](const Json &){ADD_FAILURE()<<"Timed out step executed";return Json{{"ok",true}};},true,{});
    EXPECT_EQ(t.Report().at("steps")[0].at("error").at("code"),"tutorial.timeout");
    t.Start(Definition(),"learn",predicates);t.Stop();EXPECT_EQ(t.Report().at("status"),"stopped");
}
TEST(TutorialEngine,MenuAndPaletteShareEnabledStateShortcutAndExecution) {
    class Action final:public ICommand {
        int &count;
    public:explicit Action(int &count):count(count) {} std::string Description() const override{return "Action";}
        Result<void> Execute(CommandContext &) override {++count;return {};}
    };
    CommandRegistry registry;int count=0;bool enabled=false;
    ASSERT_TRUE(registry.Register({CommandID{"test.action"},"Action","Test"},[&](CommandContext &){return CreateUnique<Action>(count);}));
    ASSERT_TRUE(registry.SetMenu(CommandID{"test.action"},"File/Action","Ctrl+N",[&]{return enabled;}));
    CommandPaletteIndex palette(registry);auto items=palette.Search("Action");ASSERT_EQ(items.size(),1);EXPECT_FALSE(items[0].enabled);EXPECT_EQ(items[0].shortcut,"Ctrl+N");
    EXPECT_FALSE(registry.Execute(CommandID{"test.action"}));EXPECT_EQ(count,0);
    enabled=true;EXPECT_TRUE(palette.Search("Action")[0].enabled);EXPECT_TRUE(palette.Execute(CommandID{"test.action"}));EXPECT_EQ(count,1);
}
const char *const kLessons[]={"first-device","heterojunction","bulk-defect","interface-defect","contacts","light-iv-qe","history-sweep","load-grading-profile","nt-defect-profile","generation-from-file","spectrum-absorption-files","compare-runs-sweep","export-profile","filter-optical-files","material-file","table-undo-redo","menus-tour"};
class ScapsLabTutorialVerify:public testing::TestWithParam<const char *> {};
TEST_P(ScapsLabTutorialVerify,HeadlessCommandsAndRealSolverPassEveryStep) {
    const auto report=VerifyTutorial(GetParam());
    EXPECT_TRUE(report.value("ok",false))<<report.dump(2);
    EXPECT_EQ(report.at("steps").size(),report.at("step_count"));
    for(const auto &step:report.at("steps"))EXPECT_EQ(step.at("status"),"PASS")<<step.dump();
}
INSTANTIATE_TEST_SUITE_P(AllThree,ScapsLabTutorialVerify,testing::ValuesIn(kLessons));
TEST_P(ScapsLabTutorialVerify,LearnAndDemoUseTheSameCommandsAndPassEveryStep) {
    for(const auto *mode:{"learn","demo"}) {
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
        ASSERT_TRUE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id",GetParam()},{"mode",mode}}).value("ok",false));
        int ticks=0;
        while(w.tutorial.Active() || w.Busy()) {
            ASSERT_LT(++ticks,120000)<<w.tutorial.Report().dump();
            if(std::string(mode)=="learn" && !w.Busy() && w.tutorial.Active()) {
                if(w.tutorial.AlreadyDone()) w.tutorial.Skip(); // condition held at step start: learn waits for the user's Next
                else {
                auto command=w.tutorial.Step()->at("command");
                if(command.value("run_id",std::string{})=="$selected_run")command["run_id"]=w.Current().selectedRun;
                ASSERT_TRUE(w.Dispatch(command).value("ok",false))<<w.response.dump();}
            }
            w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto report=w.tutorial.Report();
        EXPECT_EQ(report.at("status"),"passed")<<mode<<report.dump();
        for(const auto &step:report.at("steps"))EXPECT_TRUE(step.at("status")=="PASS" || (std::string(mode)=="learn" && step.at("status")=="SKIP"));
    }
}
TEST(ScapsLabTutorial,StopRestoresAutomaticSettingsAndInvalidInputIsTyped) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.autoRecalculate=true;
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_stop"}}).value("ok",false));EXPECT_TRUE(w.autoRecalculate);
    EXPECT_FALSE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id","../bad"}}).value("ok",true));
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id","first-device"}}).value("ok",false));
    EXPECT_FALSE(w.autoRecalculate);w.OnUpdate(0);EXPECT_TRUE(w.tutorial.Report().at("steps").empty());
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_stop"}}).value("ok",false));EXPECT_TRUE(w.autoRecalculate);EXPECT_FALSE(w.tutorial.Active());
}
TEST(ScapsLabTutorial,LearnPersistsProgressAndVerifyDoesNotWriteUserConfig) {
    const auto path=std::filesystem::path(SCAPSLAB_TUTORIAL_ARTIFACTS)/"progress-test.json";
    std::filesystem::create_directories(path.parent_path());
    std::error_code ec;std::filesystem::remove(path,ec);
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    w.tutorialProgressPath=path.string();
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id","first-device"}}).value("ok",false));
    w.OnUpdate(0);w.Current().name="Tutorial: First device"; // the user finishes step 1 after the lesson started
    w.OnUpdate(0);ASSERT_TRUE(std::filesystem::exists(path));
    const auto progress=w.tutorialProgress;EXPECT_EQ(progress.at("first-device").at("step_index"),1);
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_stop"}}).value("ok",false));
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id","first-device"},{"resume",true}}).value("ok",false));
    EXPECT_EQ(w.tutorial.Report().at("step_index"),1);
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_stop"}}).value("ok",false));
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id","first-device"},{"mode","verify"}}).value("ok",false));
    while(w.tutorial.Automatic() || w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.tutorial.Report().at("status"),"passed");EXPECT_EQ(w.tutorialProgress,progress);
    std::filesystem::remove(path,ec);
}
TEST(ScapsLabTutorial,NewProjectUsesJobPathAndResetsProjectState) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    w.path="previous.scapslab";w.project.name="Previous";w.projectDirty=true;
    const auto started=w.Dispatch({{"operation","project_new"}});ASSERT_TRUE(started.value("ok",false));EXPECT_NE(w.active,0);
    while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.value("ok",false))<<w.response.dump();EXPECT_TRUE(w.path.empty());EXPECT_FALSE(w.dirty);
    EXPECT_EQ(w.project.name,"Untitled project");EXPECT_EQ(w.project.devices.size(),1);EXPECT_TRUE(w.Current().history.empty());
}
TEST(ScapsLabTutorial,OffscreenHighlightedActualControlScreenshot) {
    Threading::SetMainThread();ASSERT_TRUE(glfwInit());
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto *window=glfwCreateWindow(1200,740,"Tutorial offscreen",nullptr,nullptr);ASSERT_NE(window,nullptr);glfwMakeContextCurrent(window);
    ASSERT_NE(gladLoadGL(glfwGetProcAddress),0);
    { Testing::UiScreenshotContext ui({.nativeWindow=window});auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,740);io.ConfigDebugHighlightIdConflicts=true;

    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id","first-device"}}).value("ok",false));
    ASSERT_TRUE(w.commands->SetMenu(CommandID{"scapslab.device_add"},"File/New","Ctrl+N"));
    ASSERT_TRUE(w.commands->SetMenu(CommandID{"scapslab.run"},"Simulate/Calculate I-V"));
    Panel panel(&w,15);
    for(int frame=0;frame<3;++frame) {
        ImGui_ImplOpenGL3_NewFrame();io.DeltaTime=1.f/60;ImGui::NewFrame();
        if(ImGui::BeginMainMenuBar()){RenderCommandMenu(*w.commands,"Simulate");ImGui::EndMainMenuBar();}
        ImGui::SetNextWindowPos(ImVec2(25,45));ImGui::SetNextWindowSize(ImVec2(650,650));
        ImGui::Begin("ScapsLab / Devices");panel.RenderContent();ImGui::End();
        EXPECT_FALSE(w.tutorialOverlay.Draw(w.tutorial));EXPECT_TRUE(w.tutorialOverlay.Highlighted());
        glViewport(0,0,1200,740);
        ui.Render();glFinish();
        EXPECT_EQ(ImGui::GetPlatformIO().Viewports.Size,1);
        EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);
    }
    auto registry=CreateRef<CommandRegistry>();ApplicationDetail::RegisterScreenshotCommand(registry,w.jobs);
    const auto path=std::filesystem::path(SCAPSLAB_TUTORIAL_ARTIFACTS)/"tutorial-highlight.png";
    std::filesystem::create_directories(path.parent_path());CommandContext context;context.Set("path",Path{path});
    const auto captured=registry->Execute(CommandID{"app.screenshot"},context);EXPECT_TRUE(captured);EXPECT_GT(std::filesystem::file_size(path),1000);
    } glfwDestroyWindow(window);glfwTerminate();
}


// Exercise the real history renderer without a visible window or persisted UI settings.
TEST(ScapsLabHistory,OffscreenCompactTable) {
    Threading::SetMainThread();ASSERT_TRUE(glfwInit());
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto *window=glfwCreateWindow(1280,900,"History offscreen",nullptr,nullptr);ASSERT_NE(window,nullptr);glfwMakeContextCurrent(window);
    ASSERT_NE(gladLoadGL(glfwGetProcAddress),0);
    { Testing::UiScreenshotContext ui({.nativeWindow=window});auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,900);io.ConfigDebugHighlightIdConflicts=true;

    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.Current().name="Reference device";
    w.project.ui["history_retention"]=20;
    for(int i=0;i<20;++i) {
        auto document=w.Current().document;document["temperature_K"]=300.+i;
        StoreRun(w.Current().history,"iv",{{"provenance",{{"time_unix_ms",1795161600000LL+i*1000}}}},document,Json::object(),20,i>=15?"temperature-sweep":"");
        w.Current().history.back()["profiles_stored"]=false;
    }
    w.Current().history.front()["name"]="Reference";w.Current().history.front()["pinned"]=true;
    w.Current().history[1]["visible"]=false;
    w.Current().selectedRun=w.Current().history.front().at("run_id");
    auto second=CreateRef<DeviceState>();second->id="second";second->name="Second device";second->document=w.Current().document;
    second->history=Json::array({w.Current().history.back()});w.project.devices.push_back(second);
    const auto before=w.Workspace();
    std::set<ImGuiID> hovered;
    for(int frame=0;frame<244;++frame) {
        const float columns[]={45,90,140,220};
        io.AddMousePosEvent(frame<240?columns[(frame/3)%4]:1250.f,frame<240?float(125+(frame/12)*25):870.f);
        ImGui_ImplOpenGL3_NewFrame();io.DeltaTime=1.f/60;ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10,10));ImGui::SetNextWindowSize(ImVec2(1260,880));
        ImGui::Begin("Run history");const float top=ImGui::GetCursorPosY();DrawHistory(w,true);
        EXPECT_LT(ImGui::GetCursorPosY()-top,820.f)<<"Twenty runs and device/sweep groups should fit in one panel";
        EXPECT_FLOAT_EQ(ImGui::GetScrollMaxY(),0.f);
        ImGui::End();
        glViewport(0,0,1280,900);
        ui.Render();glFinish();
        EXPECT_EQ(ImGui::GetPlatformIO().Viewports.Size,1);
        if(GImGui->HoveredId)hovered.insert(GImGui->HoveredId);
        EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);
    }
    EXPECT_GT(hovered.size(),20u)<<"Exercise actual row controls while detecting ID conflicts";
    EXPECT_EQ(w.Workspace(),before)<<"Rendering must not change persisted history or UI state";
    auto registry=CreateRef<CommandRegistry>();ApplicationDetail::RegisterScreenshotCommand(registry,w.jobs);
    const auto path=std::filesystem::path(SCAPSLAB_TUTORIAL_ARTIFACTS)/"slhist2-table.png";
    std::filesystem::create_directories(path.parent_path());CommandContext context;context.Set("path",Path{path});
    EXPECT_TRUE(registry->Execute(CommandID{"app.screenshot"},context));
    } glfwDestroyWindow(window);glfwTerminate();
}

TEST(ScapsLabHistory,SharedParameterLabelsTrimZerosKeepUnitsAndRename) {
    EXPECT_EQ(ParameterLabel("temperature_K","K",301.,300.),"T 300 \u2192 301 K");
    EXPECT_EQ(ParameterLabel("voltage_V","V",.125),"voltage 0.125 V");
    EXPECT_EQ(ParameterLabel("mu_n_m2_V_s","m^2/V/s",.01),"mu_n 0.01 m^2/V/s");
    EXPECT_EQ(ParameterLabel("series_ohm_m2","Ohm m^2",1.25),"series 1.25 Ohm m^2");
    EXPECT_EQ(ParameterLabel("temperature_K","K",300.25),"T 300.25 K");
    Json run={{"name",""},{"label","legacy label"},{"diff",Json::array({{{"parameter_id","temperature_K"},{"unit","K"},{"before",300.},{"after",301.}}})}};
    EXPECT_EQ(RunLabel(run),"T 300 \u2192 301 K");
    run["name"]="Reference";EXPECT_EQ(RunLabel(run),"Reference");
    run["name"]="";run["diff"]=Json::array({"legacy change"});EXPECT_EQ(RunLabel(run),"legacy label");
    run["diff"]=Json::array({{{"parameter_id","temperature_K"}}});EXPECT_EQ(RunLabel(run),"legacy label");
}
TEST(ScapsLabTutorial,CoachAvoidsTargetRowAndArrowAvoidsButtonAtViewportEdges) {
    Threading::SetMainThread();ASSERT_TRUE(glfwInit());glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto *window=glfwCreateWindow(1200,740,"Coach placement",nullptr,nullptr);ASSERT_NE(window,nullptr);glfwMakeContextCurrent(window);
    ASSERT_NE(gladLoadGL(glfwGetProcAddress),0);
    { Testing::UiScreenshotContext ui({.nativeWindow=window});auto &io=ImGui::GetIO();io.DisplaySize=ImVec2(1200,740);io.ConfigDebugHighlightIdConflicts=true;
    for(const auto pos:{ImVec2(10,10),ImVec2(950,10),ImVec2(10,570),ImVec2(950,570),ImVec2(300,10)}) {
        Tutorial tutorial;TutorialOverlay overlay;auto definition=Definition();const bool panelTarget=pos.x==300;
        if(panelTarget)definition["steps"][0]["target"].erase("item");
        tutorial.Start(definition,"learn",{{"value",[]{return Json(0);}}});
        for(int frame=0;frame<3;++frame) {
            ImGui_ImplOpenGL3_NewFrame();io.DeltaTime=1.f/60;ImGui::NewFrame();
            ImGui::SetNextWindowPos(pos);const ImVec2 targetSize(panelTarget?600.f:240.f,panelTarget?720.f:160.f);ImGui::SetNextWindowSize(targetSize);
            ImGui::Begin("Target");if(panelTarget)overlay.Window("test");ImGui::Button("Button");const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();overlay.Item("test","button");
            ImGui::SameLine();ImGui::Button("Neighbour");ImGui::End();
            EXPECT_FALSE(overlay.Draw(tutorial));EXPECT_TRUE(overlay.Highlighted());
            const auto *coach=ImGui::FindWindowByName(HostedEditorWindow("Tutorial coach###sst-tutorial").ImGuiName().c_str());ASSERT_NE(coach,nullptr);
            const auto c=coach->Pos,s=coach->Size;
            SCOPED_TRACE(testing::Message()<<"target "<<pos.x<<","<<pos.y<<" frame "<<frame);
            EXPECT_GE(s.y,100.f);
            if(panelTarget) EXPECT_TRUE(c.x+s.x<=pos.x || c.x>=pos.x+targetSize.x);
            else EXPECT_TRUE(c.y+s.y<=min.y || c.y>=max.y || c.x+s.x<=pos.x || c.x>=pos.x+targetSize.x);
            EXPECT_GE(c.x,0);EXPECT_GE(c.y,0);EXPECT_LE(c.x+s.x,1200);EXPECT_LE(c.y+s.y,740);
            EXPECT_FALSE(coach->Flags & ImGuiWindowFlags_NoDocking);
            const auto *draw=ImGui::GetForegroundDrawList();
            const int arrowVertices=(draw->Flags & ImDrawListFlags_AntiAliasedFill)?6:3;
            ASSERT_GE(draw->VtxBuffer.Size,arrowVertices);
            for(int i=draw->VtxBuffer.Size-arrowVertices;i<draw->VtxBuffer.Size;++i) {
                const auto point=draw->VtxBuffer[i].pos;
                EXPECT_FALSE(point.x>min.x && point.x<max.x && point.y>min.y && point.y<max.y)
                    <<"arrow "<<point.x<<","<<point.y<<" button "<<min.x<<","<<min.y<<" to "<<max.x<<","<<max.y;
            }
            ui.Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);
        }
    }
    }glfwDestroyWindow(window);glfwTerminate();
}

// Every step of every lesson: the target resolves to a visible widget, the coach does not cover it and the arrow tip stays outside it.
class ScapsLabTutorialUi:public testing::TestWithParam<const char *> {};
TEST_P(ScapsLabTutorialUi,EveryStepTargetIsVisibleAndArrowTipClearsIt) {
    static const char *names[]={"Project","Layers","Contacts","Solver","Run","Results","C-V","Mott-Schottky","C-f","G-omega-f","I-V","Defects","Interfaces","Rs-Rsh","Structure","Devices","Batch","Recorder","Batch overlay","Illumination","Absorption","Grading","Generation-Recombination"};
    const std::string lesson=GetParam();
    const char *shotRoot=std::getenv("SCAPSLAB_TUTORIAL_SHOTS");
    Threading::SetMainThread();ASSERT_TRUE(glfwInit());
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto *window=glfwCreateWindow(1400,1100,"Tutorial targets",nullptr,nullptr);ASSERT_NE(window,nullptr);glfwMakeContextCurrent(window);
    ASSERT_NE(gladLoadGL(glfwGetProcAddress),0);
    { Testing::UiScreenshotContext ui({.nativeWindow=window});auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1400,1100);
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    SetCommandMenuHook([&](const std::string &root,bool open){w.MenuHook(root,open);});
    ASSERT_TRUE(w.commands->SetMenu(CommandID{"scapslab.device_add"},"File/New","Ctrl+N"));
    ASSERT_TRUE(w.commands->SetMenu(CommandID{"scapslab.run"},"Simulate/Calculate I-V"));
    ASSERT_TRUE(w.commands->SetMenu(CommandID{"scapslab.undo"},"Edit/Undo","Ctrl+Z"));
    std::map<std::string,Ref<Panel>> panels;
    auto registry=CreateRef<CommandRegistry>();ApplicationDetail::RegisterScreenshotCommand(registry,w.jobs);
    ASSERT_TRUE(w.Dispatch({{"operation","tutorial_start"},{"tutorial_id",lesson}}).value("ok",false));
    for(int guard=0;w.tutorial.Active() && guard<200;++guard) {
        const auto step=*w.tutorial.Step();const auto stepId=step.at("id").get<std::string>();
        const auto windowId=step.at("target").at("window").get<std::string>();
        std::string view=windowId=="scapslab.Menu"?"Project":windowId.substr(std::string("scapslab.").size());
        int index=-1;for(int i=0;i<23;++i)if(view==names[i])index=i;ASSERT_GE(index,0)<<windowId;
        auto &panel=panels[view];if(!panel){panel=CreateRef<Panel>(&w,index);panel->SelectGradingParameter("Na_m3");}
        for(int frame=0;frame<4;++frame) {
            ImGui_ImplOpenGL3_NewFrame();io.DeltaTime=1.f/60;ImGui::NewFrame();
            if(ImGui::BeginMainMenuBar()){RenderCommandMenu(*w.commands,"Simulate");ImGui::EndMainMenuBar();}
            ImGui::SetNextWindowPos(ImVec2(20,40));ImGui::SetNextWindowSize(ImVec2(880,1040));
            ImGui::Begin((std::string("ScapsLab / ")+view).c_str());panel->RenderContent();ImGui::End();
            (void)w.tutorialOverlay.Draw(w.tutorial);
            glViewport(0,0,1400,1100);ui.Render();glFinish();
        }
        const auto &g=w.tutorialOverlay.LastGeometry();
        EXPECT_TRUE(w.tutorialOverlay.Highlighted())<<lesson<<"/"<<stepId<<": target "<<step.at("target").dump()<<" is not a visible widget";
        if(w.tutorialOverlay.Highlighted()) {
            EXPECT_GT(g.targetMax.x-g.targetMin.x,1.f);EXPECT_GT(g.targetMax.y-g.targetMin.y,1.f);
            EXPECT_GE(g.targetMin.x,0.f);EXPECT_GE(g.targetMin.y,0.f);EXPECT_LE(g.targetMax.x,1400.f);EXPECT_LE(g.targetMax.y,1100.f);
            const bool overlap=g.coachMin.x<g.targetMax.x && g.coachMax.x>g.targetMin.x && g.coachMin.y<g.targetMax.y && g.coachMax.y>g.targetMin.y;
            EXPECT_FALSE(overlap)<<lesson<<"/"<<stepId<<": coach window covers the target";
            EXPECT_TRUE(g.arrow)<<lesson<<"/"<<stepId<<": no arrow drawn";
            const bool tipInside=g.tip.x>=g.targetMin.x && g.tip.x<=g.targetMax.x && g.tip.y>=g.targetMin.y && g.tip.y<=g.targetMax.y;
            EXPECT_FALSE(tipInside)<<lesson<<"/"<<stepId<<": arrow tip covers the target";
        }
        if(shotRoot) {
            const auto path=std::filesystem::path(shotRoot)/lesson/(stepId+".png");std::filesystem::create_directories(path.parent_path());
            CommandContext context;context.Set("path",Path{path});EXPECT_TRUE(registry->Execute(CommandID{"app.screenshot"},context));
        }
        if(w.tutorial.AlreadyDone()) w.tutorial.Skip();
        else {
        auto command=step.at("command");
        if(command.value("run_id",std::string{})=="$selected_run")command["run_id"]=w.Current().selectedRun;
        ASSERT_TRUE(w.Dispatch(command).value("ok",false))<<w.response.dump();}
        int ticks=0;
        while(w.tutorial.Active() && (w.Busy() || w.tutorial.Step()->at("id").get<std::string>()==stepId)) {
            ASSERT_LT(++ticks,120000)<<lesson<<"/"<<stepId<<" never completed";
            w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    EXPECT_EQ(w.tutorial.Report().at("status"),"passed")<<w.tutorial.Report().dump();
    SetCommandMenuHook({});
    } glfwDestroyWindow(window);glfwTerminate();
}
INSTANTIATE_TEST_SUITE_P(AllLessons,ScapsLabTutorialUi,testing::ValuesIn(kLessons));
