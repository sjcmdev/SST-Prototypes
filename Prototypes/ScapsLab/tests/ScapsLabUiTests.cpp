#include "DocumentFixture.hpp"
#include <gtest/gtest.h>
#include "Panel.hpp"
#include "Core/App/Application.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include "Core/Presentation/Panels/PanelRegistry.hpp"
#include "Core/Presentation/Panels/WindowWorkspace.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>
#include <thread>
#include <bit>
#include <scaps_core/api.hpp>
#include <scaps_core/device_project_yaml.hpp>
using namespace SST;
using namespace SST::ScapsLab;
TEST(TypedBoundary, PanelsReadTypedResultsWithoutJsonViews) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,1000);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    {
        Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.autoRecalculate=false;w.autoRecalculateBand=false;w.viewIdleSeconds=5;
        w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));EditFixture(w.Current(),[&](Json& document){document["mesh"]["points_per_layer"]=21;});EditFixture(w.Current(),[&](Json& document){document["voltage"]={{"start_V",0.},{"stop_V",.01},{"step_V",.01}};});
        const auto execute=[&](Json request){EXPECT_TRUE(w.Dispatch(request).value("ok",false));while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response;};
        execute({{"operation","run"}});execute({{"operation","band"},{"voltage_V",0.}});
        execute({{"operation","cv"},{"temperature_K",300.},{"start_V",0.},{"stop_V",.01},{"step_V",.01},{"frequency_Hz",1e3}});
        execute({{"operation","cf"},{"temperature_K",300.},{"voltage_V",0.},{"start_Hz",100.},{"stop_Hz",1000.},{"points",3}});
        execute({{"operation","qe"},{"start_nm",500.},{"stop_nm",700.},{"points",3}});
        auto spec=BatchDefaults();spec["axes"][0]["count"]=2;execute({{"operation","run_batch"},{"spec",spec}});
        for(int view:{2,4,5,6,7,8,9,10,11,12,17,18,19,20,22}) {
            Panel panel(&w,view);for(int frame=0;frame<2;++frame){io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(1100,900));ImGui::Begin("Typed boundary panels");panel.RenderContent();ImGui::End();ImGui::Render();}
        }
        EXPECT_TRUE(w.ViewsReady());EXPECT_EQ(w.warmJob,0u);
        EXPECT_FALSE(w.Current().Iv()->HasView());EXPECT_FALSE(w.Current().Band()->HasView());EXPECT_FALSE(w.Current().batchResult.typed()->HasView());
        for(const auto& [kind,result]:w.Current().acResults.all())EXPECT_FALSE(result->HasView());
        for(const auto& [id,result]:*w.Current().runs)EXPECT_FALSE(result->HasView());
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
TEST(ScapsLab,ProjectTitleAndTabHaveNoConflictingIds) {
    ImGui::CreateContext();ImPlot::CreateContext();
    auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,800);
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;io.ConfigDebugHighlightIdConflicts=true;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    PanelRegistry registry;auto panel=CreateRef<Panel>(&w,0);registry.AddExisting(panel,5);
    auto workspace=CreateRef<WindowWorkspace>(registry);auto commands=CreateRef<CommandRegistry>();
    ASSERT_TRUE(workspace->Bind(commands,{}));workspace->InstallSettingsHandler();
    int hoveredSamples=0;
    auto frame=[&] {
        io.DeltaTime=1.f/60;ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);
        panel->Render();workspace->RenderOverlays();ImGui::Render();
        if(GImGui->HoveredId) ++hoveredSamples;
        EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1)<<"Hovered ID "<<GImGui->HoveredIdPreviousFrame<<" at "<<io.MousePos.x<<","<<io.MousePos.y;
    };
    frame();frame();
    // Cover both the custom floating title and the amended dock tab, including Close.
    for(bool docked:{false,true}) {
        if(docked) {auto state=panel->GetState();state.dockId=ImGui::GetMainViewport()->ID;panel->ApplyState(state);frame();frame();}
        auto *window=ImGui::FindWindowByName(panel->ImGuiName().c_str());
        const auto title=docked?window->DC.DockTabItemRect:window->TitleBarRect();
        const float right=docked?window->DockNode->Rect().Max.x:title.Max.x;
        for(float x=title.Min.x+2;x<right;x+=6) {
            io.AddMousePosEvent(x,title.GetCenter().y);frame();frame();frame();
        }
    }
    EXPECT_GT(hoveredSamples,5)<<"The regression must exercise actual hovered title/tab controls";
    workspace->RemoveSettingsHandler();ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,UiAddsDefectAndMatchesYamlBitwise) {
    ImGui::CreateContext();ImPlot::CreateContext();
    auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,1000);io.ConfigDebugHighlightIdConflicts=true;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    EditFixture(w.Current(),[&](Json& document){document["layers"][0]["defects"]=Json::array();});EditFixture(w.Current(),[&](Json& document){document["voltage"]={{"start_V",0.0},{"stop_V",0.05},{"step_V",0.05}};});
    Panel panel(&w,11);bool found=false;ImVec2 button;
    auto frame=[&] {
        io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(600,950));
        ImGui::Begin("UI parity");panel.RenderContent();ImGui::End();ImGui::Render();
        EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);
    };
    frame();frame();const auto id=ImGui::FindWindowByName("UI parity")->GetID("Add defect");
    for(float y=30;y<190 && !found;y+=4) {
        io.AddMousePosEvent(35,y);frame();frame();
        if(GImGui->HoveredId==id) {button=ImVec2(35,y);found=true;}
    }
    EXPECT_TRUE(found);
    if(found) {io.AddMousePosEvent(button.x,button.y);io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}
    while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().Document()["layers"][0]["defects"].size(),1u);
    const auto path=std::filesystem::temp_directory_path()/"scapslab-ui-defect.yaml";WriteProject(w.Current().Document(),path.string());
    Workflow yaml(CreateRef<JobSystem>(nullptr,1));yaml.Current().SetDocument(ReadProject(path.string()));
    EXPECT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());EXPECT_TRUE(yaml.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    while(w.Pending() || yaml.Busy()) {w.OnUpdate(0);yaml.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().result.at("points"),yaml.Current().result.at("points"));EXPECT_EQ(w.Current().result.at("mesh_x_m"),yaml.Current().result.at("mesh_x_m"));
    for(std::size_t i=0;i<w.Current().result.at("points").size();++i) {
        const auto &a=w.Current().result.at("points")[i],&b=yaml.Current().result.at("points")[i];
        if(a.at("J_A_m2").is_number()) EXPECT_EQ(std::bit_cast<std::uint64_t>(a.at("J_A_m2").get<double>()),std::bit_cast<std::uint64_t>(b.at("J_A_m2").get<double>()));
        for(const auto *key:{"Ec_eV","Ev_eV","n_m3","p_m3"}) for(std::size_t j=0;j<a.at(key).size();++j)
            EXPECT_EQ(std::bit_cast<std::uint64_t>(a.at(key)[j].get<double>()),std::bit_cast<std::uint64_t>(b.at(key)[j].get<double>()));
    }
    const auto project=scaps_core::load_device_project_yaml(path);const auto device=scaps_core::make_device_from_project(project);
    const auto pc=scaps_core::physical_constants(project.constants_mode);auto volts=scaps_core::make_voltage_list(project.voltage);for(auto &v:volts)v=-v;
    const auto mesh=scaps_core::prepare_mesh(device,project.mesh,volts,{project.temperature_K},project.solver,pc);
    const auto native=scaps_core::solve_iv(device,mesh,volts,project.temperature_K,project.solver,pc);
    for(std::size_t i=0;i<native.size();++i) {
        ASSERT_EQ(native[i].status,scaps_core::SolverStatus::Converged);
        const double current=native[i].state.Jn_Am2.front()+native[i].state.Jp_Am2.front();
        EXPECT_EQ(std::bit_cast<std::uint64_t>(w.Current().result.at("points")[i].at("J_A_m2").get<double>()),std::bit_cast<std::uint64_t>(current));
    }
    std::filesystem::remove(path);ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,AllEditorControlsHaveUniqueIdsAtNarrowWidth) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,1600);
    io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    auto second=w.Current().Document()["layers"][0];second["scapslab_id"]="test-layer";EditFixture(w.Current(),[&](Json& document){document["layers"].push_back(second);});
    EditFixture(w.Current(),[&](Json& document){document["layers"][0]["defects"]=Json::array({NewDefect()});});EditFixture(w.Current(),[&](Json& document){document["interface_defects"]=Json::array({NewDefect(true)});});
    for(int view:{1,2,3,4,11,12,13,14,15,16,17,18,19,20,21}) {
        Panel panel(&w,view);
        auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(340,1500));ImGui::Begin("Editor IDs");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1)<<view;};
        frame();frame();for(float y=35;y<900;y+=9) for(float x=160;x<330;x+=36) {io.AddMousePosEvent(x,y);frame();frame();}
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,StructureShowsEveryEditorGradingLaw) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,800);
    io.Fonts->AddFontDefault();io.Fonts->Build();Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));Panel panel(&w,14);
    int expected=0;
    for(const auto *law:{"linear","uniform","parabolic","logarithmic","exponential"}) {
        EditFixture(w.Current(),[&](Json& document){document["layers"][0]["grading"]={{"Eg_eV",{{"law",law},{"left",1.12},{"right",1.15},{"bowing",.5}}}};});
        int green=0;
        for(int frame=0;frame<2;++frame) {
            io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1000,700));ImGui::Begin("Grading laws");panel.RenderContent();
            green=0;for(const auto &v:ImGui::GetWindowDrawList()->VtxBuffer) if(v.col==IM_COL32(140,235,170,255)) ++green;
            ImGui::End();ImGui::Render();
        }
        if(!expected) expected=green;
        EXPECT_GT(green,100)<<law;EXPECT_EQ(green,expected)<<law;
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,StructureThicknessDragIsUndoable) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,800);
    io.Fonts->AddFontDefault();io.Fonts->Build();Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    const auto original=w.Current().Document();Panel panel(&w,14);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1000,700));ImGui::Begin("Structure drag");panel.RenderContent();ImGui::End();ImGui::Render();};
    frame();frame();io.AddMousePosEvent(988,120);frame();frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(968,120);frame();io.AddMouseButtonEvent(0,false);frame();
    while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_LT(w.Current().Document()["layers"][0]["thickness_m"].get<double>(),original["layers"][0]["thickness_m"].get<double>());
    EXPECT_TRUE(w.Dispatch({{"operation","undo"}}).at("ok").get<bool>());
    while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().Document(),original);ImPlot::DestroyContext();ImGui::DestroyContext();
}

Json RunBatchViaUi(const Json &document,const Json &spec) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,1100);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(document);w.Current().SetBatchSpec(spec);Panel panel(&w,16);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1100,1050));ImGui::Begin("Batch parity");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
    frame();frame();const auto button=ImGui::FindWindowByName("Batch parity")->GetID("Run batch");bool found=false;
    for(float y=40;y<1000 && !found;y+=4) {io.AddMousePosEvent(40,y);frame();frame();if(GImGui->HoveredId==button) {found=true;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}}
    EXPECT_TRUE(found);while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response.dump();
    Json result=*w.Current().batchResult;ImPlot::DestroyContext();ImGui::DestroyContext();return result;
}

Json RunLightViaUi() {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,1100);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string()));Panel panel(&w,19),run(&w,4);
    auto runFrame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1100,1050));ImGui::Begin("Run parity");run.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
    runFrame();runFrame();const auto shotId=ImGui::FindWindowByName("Run parity")->GetID("Single shot");bool clicked=false;
    for(float y=30;y<1000 && !clicked;y+=2) {io.AddMousePosEvent(75,y);runFrame();runFrame();if(GImGui->HoveredId==shotId){clicked=true;io.AddMouseButtonEvent(0,true);runFrame();io.AddMouseButtonEvent(0,false);runFrame();}}
    EXPECT_TRUE(clicked)<<"Single shot in Run must execute the included I-V Action";
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response;
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1100,1050));ImGui::Begin("Light parity");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
    frame();frame();
    // Tab IDs are scoped by the tab bar; position is obtained from the rendered item.
    const auto tabId=ImHashStr("Light I-V",0,ImGui::FindWindowByName("Light parity")->GetID("Optical views"));
    bool tab=false;
    for(float y=45;y<150 && !tab;y+=2) for(float x=100;x<250 && !tab;x+=3) {io.AddMousePosEvent(x,y);frame();frame();if(GImGui->HoveredId==tabId) {tab=true;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}}
    EXPECT_TRUE(tab);
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response.dump();
    frame();frame();
    bool pvPlot=false;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i) if(auto *plot=GImPlot->Plots.GetByIndex(i);plot && plot->HasTitle() && std::string(plot->GetTitle())=="Light I-V") {
        pvPlot=true;const double jsc=w.Current().result.at("photovoltaic").at("Jsc_A_m2").get<double>()*.1;
        const double padding=2.2*jsc*w.plotSession.settings.margin*.01;
        EXPECT_NEAR(plot->Axes[ImAxis_Y1].Range.Min,-1.2*jsc-padding,1e-10);EXPECT_NEAR(plot->Axes[ImAxis_Y1].Range.Max,jsc+padding,1e-10);
        EXPECT_STREQ(plot->GetAxisLabel(plot->Axes[ImAxis_Y1]),"J [mA/cm^2]");
        for(const auto *label:{"Voc","Jsc","MPP"}) {
            bool found=false;for(int item=0;item<plot->Items.GetLegendCount();++item)found|=std::string(plot->Items.GetLegendLabel(item))==label;
            EXPECT_TRUE(found)<<label;
        }
    }
    EXPECT_TRUE(pvPlot);
    Json result={{"document",w.Current().Document()},{"result",*w.Current().result}};
    ImPlot::DestroyContext();ImGui::DestroyContext();return result;
}

TEST(ScapsLab,GenerationRecombinationPanelHasNoConflictingIds) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string()));
    ASSERT_TRUE(w.Dispatch({{"operation","band"},{"voltage_V",0.}}).at("ok").get<bool>());
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.at("ok").get<bool>())<<w.response.dump();w.Current().result=w.Current().bandResult;
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1500,1600);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Panel panel(&w,22);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);ImGui::Begin("G/R UI");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
    frame();frame();for(float y=10;y<1500;y+=14) {io.AddMousePosEvent(450,y);frame();frame();}
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,HistoryControlsAndNumericRowsHaveNoConflictingIds) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1600,1200);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    ASSERT_TRUE(w.Dispatch({{"operation","band"},{"voltage_V",0.}}).value("ok",false));
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.value("ok",false));
    Panel history(&w,15),parameters(&w,1);
    for(int view=0;view<2;++view) for(int frame=0;frame<30;++frame) {
        io.AddMousePosEvent(200,float(40+frame*24));io.DeltaTime=1.f/60;ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1200,1100));
        ImGui::Begin("History regression");if(view==0)history.RenderContent();else parameters.RenderContent();ImGui::End();ImGui::Render();
        EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,CurrentIvRemainsVisibleAfterAnotherTechniqueEvictsItsHistory) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1200,900);io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    EditFixture(w.Current(),[&](Json& document){document["voltage"]={{"start_V",0.},{"stop_V",.1},{"step_V",.05}};});w.project.ui["history_retention"]=1;
    for(const auto *operation:{"run","band"}) {
        ASSERT_TRUE(w.Dispatch({{"operation",operation},{"voltage_V",0.}}).value("ok",false));
        while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ASSERT_TRUE(w.response.value("ok",false));
    }
    ASSERT_EQ(w.Current().history.size(),1);ASSERT_EQ(w.Current().history.front().at("kind"),"band");
    Panel panel(&w,10);
    for(int frame=0;frame<3;++frame) {
        io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowSize(ImVec2(1100,800));
        ImGui::Begin("Current curve regression");panel.RenderContent();
        const auto *plot=ImPlot::GetPlot("I-V compare");EXPECT_NE(plot,nullptr);if(plot)EXPECT_GT(plot->Items.GetItemCount(),0);
        ImGui::End();ImGui::Render();
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,CompactHistoryRowsEditAndCollapseWithoutChangingTheSchema) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1200,1000);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    auto finish=[&] {while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response.dump();};
    ASSERT_TRUE(w.Dispatch({{"operation","band"},{"voltage_V",0.}}).value("ok",false));finish();
    const auto seed=w.Current().history.front();w.project.ui["history_retention"]=20;
    for(int i=1;i<20;++i) {
        auto run=seed;run["run_id"]="compact-"+std::to_string(i);run["label"]="temperature_K 300 -> "+std::to_string(300+i);
        run["group_id"]=i>=10?"sweep-1":"";run["profiles_stored"]=false;w.Current().history.mut().push_back(run);{auto runs=CreateRef<RunMap>(*w.Current().runs);(*runs)[run["run_id"].get<std::string>()]=w.Current().RunResult(seed.at("run_id"));w.Current().runs=std::move(runs);}
    }
    const auto deviceId=w.Current().id,runId=seed.at("run_id").get<std::string>();
    auto frame=[&] {
        w.OnUpdate(0);
        io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Compact history");DrawHistory(w);ImGui::End();ImGui::Render();
        EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);
    };
    const auto before=w.Workspace();frame();frame();EXPECT_EQ(w.Workspace(),before);
    auto *window=ImGui::FindWindowByName("Compact history");
    const auto scope=ImHashStr(deviceId.c_str(),0,window->GetID("device-history"));
    const auto tableId=ImHashStr("runs",0,ImHashStr("device",0,scope));
    const auto runScope=ImHashStr(runId.c_str(),0,tableId);
    auto table=[&] {return GImGui->Tables.GetByKey(tableId);};
    ASSERT_NE(table(),nullptr);EXPECT_EQ(table()->ColumnsCount,7);EXPECT_EQ(table()->CurrentRow,21);
    EXPECT_LT(table()->OuterRect.GetHeight(),700.f)<<"Twenty runs must remain one row each";
    auto locate=[&](ImGuiID id,int column) {
        const float x=table()->Columns[column].WorkMinX+10;
        for(float y=table()->OuterRect.Min.y+20;y<table()->OuterRect.Max.y;y+=3) {
            io.AddMousePosEvent(x,y);frame();frame();if(GImGui->HoveredId==id)return true;
        }
        return false;
    };
    auto click=[&] {io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();};
    ASSERT_TRUE(locate(ImHashStr("##visible",0,runScope),1));click();finish();EXPECT_FALSE(w.Current().history[0].at("visible"));
    ASSERT_TRUE(locate(ImHashStr("##pin",0,runScope),2));click();finish();EXPECT_TRUE(w.Current().history[0].at("pinned"));
    ASSERT_TRUE(locate(ImHashStr("##swatch",0,runScope),0));click();frame();EXPECT_TRUE(ImGui::IsPopupOpen(ImHashStr("Colour",0,runScope),ImGuiPopupFlags_None));
    io.AddMousePosEvent(1100,30);click();frame();frame();
    EXPECT_FALSE(ImGui::IsPopupOpen(ImHashStr("Colour",0,runScope),ImGuiPopupFlags_None));
    ASSERT_TRUE(locate(ImHashStr("###label",0,runScope),3));
    for(int i=0;i<25;++i)frame();
    click();finish();click();frame();frame();
    EXPECT_EQ(GImGui->InputTextState.ID,ImHashStr("##rename",0,runScope));
    io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();
    io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddInputCharactersUTF8("Reference");frame();
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();finish();
    EXPECT_EQ(w.Current().history[0].at("name"),"Reference");
    const auto edited=w.Workspace();
    ASSERT_TRUE(locate(ImHashStr("sweep",0,ImHashStr("sweep-1",0,tableId)),3));click();frame();
    EXPECT_EQ(table()->CurrentRow,11);EXPECT_EQ(w.Workspace(),edited);
    bool deviceHeader=false;
    for(float y=window->WorkRect.Min.y;y<table()->OuterRect.Min.y;y+=2) {
        io.AddMousePosEvent(window->WorkRect.Min.x+20,y);frame();frame();
        if(GImGui->HoveredId==ImHashStr("device",0,scope)){deviceHeader=true;break;}
    }
    ASSERT_TRUE(deviceHeader);click();frame();
    EXPECT_LT(table()->LastFrameActive,ImGui::GetFrameCount());EXPECT_EQ(w.Workspace(),edited);
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,MainLoopPreservesProjectAndReportsLayerExceptions) {
    Threading::SetMainThread();
    class Broken final:public Layer {
        int &calls;bool render;
    public:Broken(int &count,bool draw):Layer(draw?"Broken profile panel":"Broken update"),calls(count),render(draw){}
        void OnUpdate(float) override {if(!render){++calls;throw std::runtime_error("regression update failure");}}
        void OnImGuiRender() override {if(render){++calls;ImGui::Begin("Interrupted profile");throw std::runtime_error("regression profile failure");}}
    };
    class Probe final:public Layer {
        bool &seen;
    public:explicit Probe(bool &value):Layer("Error dialog probe"),seen(value){}
        void OnImGuiRender() override {if(const auto *popup=ImGui::FindWindowByName("Fatal Error##blocking"))seen|=popup->Active && !popup->Hidden;}
    };
    bool dialogSeen=false;
    const auto file=std::filesystem::path(SCAPSLAB_TUTORIAL_ARTIFACTS)/"recovered-error.png";std::filesystem::create_directories(file.parent_path());
    std::string exe="ScapsLabRecoveryTest",shot="--screenshot",target=file.string(),frames="--screenshot-after-frames",count="4",exit="--exit-after-screenshot";
    char *argv[]={exe.data(),shot.data(),target.data(),frames.data(),count.data(),exit.data()};
    const auto previousDirectory=std::filesystem::current_path();
    struct RestoreDirectory {std::filesystem::path path;~RestoreDirectory(){std::filesystem::current_path(path);}} restore{previousDirectory};
    std::filesystem::current_path(std::filesystem::path(SCAPSLAB_HEADLESS).parent_path().parent_path()/"ScapsLab");
    int updates=0,renders=0;Workflow project(CreateRef<JobSystem>(nullptr,1));project.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));project.projectDirty=true;const auto before=project.Workspace();
    auto app=Application::Create(6,argv,[&](Application &application){application.PushLayer(CreateUnique<Broken>(updates,false));application.PushLayer(CreateUnique<Broken>(renders,true));application.PushLayer(CreateUnique<Probe>(dialogSeen));});
    EXPECT_EQ(app.Run(),0);EXPECT_EQ(updates,1);EXPECT_EQ(renders,1);EXPECT_TRUE(dialogSeen);EXPECT_EQ(project.Workspace(),before);EXPECT_TRUE(project.projectDirty);EXPECT_TRUE(std::filesystem::exists(file));
}

TEST(ScapsLab,ProfilePreviewRightClickOpensExportMenu) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1100,1000);io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    EditFixture(w.Current(),[&](Json& document){document["layers"][0]["grading"]["Eg_eV"]={{"law","linear"},{"left",1.1},{"right",1.2}};});
    Panel panel(&w,21);
    auto frame=[&]{io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);ImGui::Begin("Profile export test");panel.RenderContent();ImGui::End();ImGui::Render();};
    frame();frame();ImVec2 at;bool found=false;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i)if(auto *plot=GImPlot->Plots.GetByIndex(i);plot && plot->HasTitle() && std::string(plot->GetTitle())=="Profile preview"){at=plot->PlotRect.GetCenter();found=true;}
    EXPECT_TRUE(found);
    if(found){io.AddMousePosEvent(at.x,at.y);frame();frame();io.AddMouseButtonEvent(1,true);frame();io.AddMouseButtonEvent(1,false);frame();
        const auto expected=ImHashStr("Export profile",0,ImGui::FindWindowByName("Profile export test")->GetID("Eg_eV"));
        bool opened=false;for(const auto &popup:GImGui->OpenPopupStack)opened|=popup.PopupId==expected;EXPECT_TRUE(opened)<<"Right-click must expose the product's CSV/.grd export menu";
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

// ---- slslide: live slider drag, fine mode, log sliders, Esc revert, latest-wins previews ----
TEST(ScapsLab,SliderFineModeScalesRelativeMotionWithoutJumps) {
    const SliderMap m{0,10,false};SliderDrag d;d.Begin(5);const float width=1000;
    EXPECT_EQ(SliderScale(false,false,false),1.);EXPECT_EQ(SliderScale(true,false,false),.1);EXPECT_EQ(SliderScale(true,true,false),.01);EXPECT_EQ(SliderScale(false,false,true),.1);
    EXPECT_EQ(d.Update(m,6.5,5,100,width,1.),6.5)<<"native drag untouched until a modifier is used";
    // Shift pressed: the cursor-absolute native value (9) is ignored; no jump from the last value (6.5).
    EXPECT_EQ(d.Update(m,9.,6.5,300,width,.1),6.5);
    EXPECT_EQ(d.Update(m,9.,6.5,300,width,.1),6.5);
    const double fine=d.Update(m,0,6.5,400,width,.1);
    EXPECT_NEAR(fine,6.5+.1,1e-9)<<"100 px at 10x finer = 1/10 of the normal 1.0 per 100 px";
    // Shift released at the same cursor position: continue from the current value (no jump), then full speed, relative.
    EXPECT_EQ(d.Update(m,2.,fine,400,width,1.),fine);
    EXPECT_NEAR(d.Update(m,2.,fine,500,width,1.),fine+100/width*10,1e-9);
    SliderDrag e;e.Begin(5);(void)e.Update(m,5,5,0,width,.01);EXPECT_NEAR(e.Update(m,5,5,100,width,.01),5+.01,1e-9)<<"Ctrl+Shift: 100x slower";
    SliderDrag n;n.Begin(5);(void)n.Update(m,5,5,0,width,1.);(void)n.Update(m,5,5,0,width,.1);EXPECT_NEAR(n.Update(m,5,5,100,width,.1),5.1,1e-9)<<"Shift: 10x slower";
}
TEST(ScapsLab,LogSliderRoundTripsValueToPositionToValue) {
    const SliderMap m{1e-30,1e21,true};
    for(const double v:{1e-30,3.3e-20,1.,2.5e5,1e14,9.99e20,1e21}) EXPECT_NEAR(m.FromT(m.ToT(v))/v,1.,1e-12)<<v;
    EXPECT_NEAR(m.ToT(1e-30),0,1e-15);EXPECT_NEAR(m.ToT(1e21),1,1e-15);EXPECT_NEAR(m.ToT(1.),30./51.,1e-12);
    EXPECT_NEAR(m.Step(1e14,1)/1e14,std::pow(10.,.01),1e-12)<<"arrow step = 0.01 decade";EXPECT_NEAR(m.Step(1e14,.1)/1e14,std::pow(10.,.001),1e-12);
    const SliderMap lin{0,10,false};EXPECT_NEAR(lin.Step(5,1),5.1,1e-12);EXPECT_NEAR(lin.Step(5,.1),5.01,1e-12);EXPECT_EQ(lin.Step(9.99,5),10.);
}
TEST(ScapsLab,EscapeDuringSliderDragRestoresTheStartValue) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,900);io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));const auto original=w.Current().Document();Panel panel(&w,4);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(900,800));ImGui::Begin("Esc drag");panel.RenderContent();ImGui::End();ImGui::Render();};
    frame();frame();const auto id=ImHashStr("##value",0,ImHashStr("Temperature [K]",0,ImGui::FindWindowByName("Esc drag")->GetID("Temperature [K]")));
    ImVec2 at;bool found=false;for(float y=30;y<500 && !found;y+=2) {io.AddMousePosEvent(450,y);frame();frame();if(GImGui->HoveredId==id) {found=true;at=ImVec2(450,y);}}
    ASSERT_TRUE(found);
    io.AddMouseButtonEvent(0,true);frame();io.AddMousePosEvent(at.x-150,at.y);frame();frame();io.AddMousePosEvent(at.x-250,at.y);frame();
    EXPECT_TRUE(w.dragSeen)<<"a held slider announces itself to the workflow";
    EXPECT_NE((w.EditDraft()?DraftDocument(*w.EditDraft()):w.Current().Document())["temperature_K"].get<double>(),original["temperature_K"].get<double>());
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();io.AddMouseButtonEvent(0,false);frame();
    while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().Document()["temperature_K"],original["temperature_K"]);EXPECT_EQ(w.Current().Document(),original);
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
namespace {
Json TinyDevice() {auto d=ReadProject(SCAPSLAB_PROJECT);d["voltage"]={{"start_V",0.0},{"stop_V",0.8},{"step_V",0.1}};return d;}
void Settle(Workflow &w,int maxMs=60000) {for(int i=0;i<maxMs && (w.Pending() || w.Current().needsRecalculate);++i) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
}
TEST(ScapsLab,LiveDragLatestWinsStartsFewJobsAndEndsWithTheExactFinalRun) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(TinyDevice());w.autoRecalculate=true;
    ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());Settle(w);
    const auto baseline=w.Current().history.size();ASSERT_EQ(baseline,1u);
    w.dragging=true;const auto t0=std::chrono::steady_clock::now();
    for(int i=0;i<30;++i) {
        auto doc=w.Current().Document();doc["temperature_K"]=300.+i;
        EXPECT_TRUE(w.Dispatch({{"operation","edit"},{"document",doc},{"gesture_id","drag"}}).at("ok").get<bool>());
        for(int k=0;k<5;++k) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
    EXPECT_LE(w.previewsStarted,unsigned(ms/33)+2)<<"<=30 per second, never queued; ms="<<ms;
    EXPECT_EQ(w.Current().history.size(),baseline)<<"previews never enter history";
    auto last=w.EditDraft()?DraftDocument(*w.EditDraft()):w.Current().Document();last["temperature_K"]=329.;
    w.dragging=false;ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"document",last},{"gesture_id","drag"}}).at("ok").get<bool>());
    Settle(w);
    EXPECT_EQ(w.Current().result.at("project_snapshot").at("temperature_K").get<double>(),329.)<<"exact run of the final value";
    EXPECT_TRUE(w.Current().previewV.empty());EXPECT_EQ(w.Current().history.size(),baseline)<<"auto exact only updates Live";
    Workflow ref(CreateRef<JobSystem>(nullptr,1));ref.Current().SetDocument(w.Current().Document());ASSERT_TRUE(ref.Dispatch({{"operation","run"}}).at("ok").get<bool>());Settle(ref);
    ASSERT_EQ(w.Current().result.at("points").size(),ref.Current().result.at("points").size());
    for(std::size_t i=0;i<ref.Current().result.at("points").size();++i) EXPECT_EQ(w.Current().result.at("points")[i].at("J_A_m2"),ref.Current().result.at("points")[i].at("J_A_m2"));
}
TEST(ScapsLab,CoarsePreviewIsNeverSavedStoredOrUsedForReadouts) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(TinyDevice());w.autoRecalculate=true;
    ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());Settle(w);
    const Json resultBefore=*w.Current().result,historyBefore=*w.Current().history;
    w.lastFullMs=1000;w.dragging=true;auto doc=w.Current().Document();doc["temperature_K"]=310.;
    ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"document",doc},{"gesture_id","drag"}}).at("ok").get<bool>());
    for(int i=0;i<20000 && w.Current().previewV.empty();++i) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    auto &d=w.Current();ASSERT_FALSE(d.previewV.empty());
    EXPECT_TRUE(d.previewCoarse);EXPECT_LT(d.previewV.size(),resultBefore.at("points").size());EXPECT_EQ(d.status,"preview");EXPECT_NE(w.status.find("Preview"),std::string::npos);
    EXPECT_EQ(*d.result,resultBefore);EXPECT_EQ(*d.history,historyBefore);
    const auto saved=w.Workspace(true).dump();EXPECT_EQ(saved.find("previewV"),std::string::npos);EXPECT_EQ(saved.find("\"preview\":true"),std::string::npos);
    w.dragging=false;Settle(w);EXPECT_TRUE(d.previewV.empty());EXPECT_EQ(d.status,"solved");
}

TEST(ScapsLabLive, GestureKeepsPreviewBiasesAndMeshWhenSolveTimingChanges) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDocument(TinyDevice());w.autoRecalculate=true;w.autoRecalculateBand=false;
    ASSERT_TRUE(w.Dispatch({{"operation","run"}}).value("ok",false));Settle(w);ASSERT_TRUE(w.Current().warmIv);
    const auto grid=w.Current().warmIv->x_m;w.lastFullMs=1000;w.dragging=w.interacting=true;
    std::vector<double> biases;
    for(int tick=0;tick<3;++tick){
        auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=301+tick;ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,"fixed-grid")).value("ok",false));
        w.lastLive={};w.OnUpdate(0);ASSERT_TRUE(w.ActiveOp());EXPECT_TRUE(w.ActiveOp()->automatic);EXPECT_EQ(w.ActiveOp()->request.value("stride",0),4);ASSERT_TRUE(w.ActiveOp()->action);const auto samples=ActionSamples(*w.ActiveOp()->action,w.Current().draft->project);
        if(tick==0)biases=samples;else EXPECT_EQ(samples,biases);ASSERT_TRUE(w.ActiveOp()->liveMeshInput);EXPECT_EQ(*w.ActiveOp()->liveMeshInput,grid);
        while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}w.lastFullMs=10;
    }
    w.dragging=w.interacting=false;w.OnUpdate(0);EXPECT_FALSE(w.Current().liveGesture);Settle(w);
    w.dragging=true;auto geometry=CreateRef<ProjectDraft>(*w.Current().draft);geometry->project.layers.front().thickness_m*=1.1;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(geometry,"geometry-drag")).value("ok",false));w.lastLive={};w.OnUpdate(0);ASSERT_TRUE(w.ActiveOp());EXPECT_FALSE(w.ActiveOp()->liveMeshInput);
    w.dragging=false;Settle(w);
}
TEST(ScapsLabLive, FirstGestureUsesStoredSolveTimingBeforeFreezingSampling) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(TinyDevice());w.autoRecalculate=w.autoRecalculateBand=false;
    auto result=CreateRef<IvResult>();result->meta.prim={{"compute_time_ms",1000.}};w.Current().result.adopt(result);w.dragging=true;w.OnUpdate(0);
    EXPECT_EQ(w.Current().liveStride,4);EXPECT_FALSE(w.Current().liveMesh);w.lastFullMs=10;w.OnUpdate(0);EXPECT_EQ(w.Current().liveStride,4);
}

// ---- sllive1: mantissa x 10^exponent log widget, live band/profile refresh ----
TEST(ScapsLab,MantissaExponentWidgetMathRoundTripsAndClamps) {
    const SliderMap r{1,1e24,true};
    for(const double v:{1.,2.5,9.99,1e14,3.2e15,9.5e23,1e24}) {
        double m;int e;SplitMantissaExponent(v,m,e);
        EXPECT_GE(m,1.);EXPECT_LT(m,10.);EXPECT_NEAR(m*std::pow(10.,e)/v,1.,1e-12)<<v;
        EXPECT_NEAR(MantissaExponentValue(m,e,r)/v,1.,1e-12)<<v;
    }
    double m;int e;SplitMantissaExponent(1e-30,m,e);EXPECT_EQ(e,-30);EXPECT_NEAR(m,1.,1e-12);
    EXPECT_EQ(MantissaExponentValue(5,30,r),1e24)<<"clamped to hi";EXPECT_EQ(MantissaExponentValue(5,-3,r),1.)<<"clamped to lo";
    EXPECT_NEAR(MantissaExponentValue(3.2,15,r),3.2e15,1.);
}
TEST(ScapsLab,LayerLogParameterIsTwoSlidersAndCtrlClickTypesValueOrMantissa) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1400,2400);io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));
    EditFixture(w.Current(),[&](Json& document){document["layers"][0]["doping"]["Nd_m3"]=1e22;});EditFixture(w.Current(),[&](Json& document){document["layers"][0]["doping"]["Na_m3"]=0.;});Panel panel(&w,1);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1300,2300));ImGui::Begin("Log widget");panel.RenderContent();ImGui::End();ImGui::Render();};
    frame();frame();
    const auto row=ImGui::FindWindowByName("Log widget")->GetID("Donors [cm^-3]");
    const auto mantissaId=ImHashStr("##mantissa",0,ImHashStr("Donors [cm^-3]",0,row)),exponentId=ImHashStr("##exponent",0,ImHashStr("Donors [cm^-3]",0,row));
    auto hover=[&](ImGuiID id)->ImVec2 {for(float y=30;y<2300;y+=2) for(float x:{300.f,600.f,900.f,1200.f}) {io.AddMousePosEvent(x,y);frame();frame();if(GImGui->HoveredId==id) return ImVec2(x,y);}return ImVec2(-1,-1);};
    auto settle=[&]{while(w.Pending()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    auto type=[&](ImVec2 at,const char *text) {
        io.AddMousePosEvent(at.x,at.y);frame();io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
        EXPECT_TRUE(ImGui::TempInputIsActive(GImGui->ActiveId)||GImGui->ActiveId!=0)<<"Ctrl+click opens exact text entry";
        io.AddInputCharactersUTF8(text);frame();io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();frame();settle();frame();
    };
    const auto nd=[&]{return w.Current().Document()["layers"][0]["doping"]["Nd_m3"].get<double>();};
    const auto m=hover(mantissaId);ASSERT_GE(m.x,0)<<"mantissa slider exists with its own ID";
    const auto e=hover(exponentId);ASSERT_GE(e.x,0)<<"exponent slider exists with its own ID";
    EXPECT_EQ(m.y,e.y)<<"both sliders on one row";EXPECT_LT(m.x,e.x);
    type(m,"3.2e15");EXPECT_NEAR(nd()/3.2e21,1.,1e-9)<<"a full value typed into the mantissa is taken as the value";
    const auto m2=hover(mantissaId);ASSERT_GE(m2.x,0);type(m2,"5");EXPECT_NEAR(nd()/5e21,1.,1e-9)<<"a plain mantissa keeps the exponent";
    const auto e2=hover(exponentId);ASSERT_GE(e2.x,0);type(e2,"16");EXPECT_NEAR(nd()/5e22,1.,1e-9)<<"typed exponent keeps the mantissa";
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
TEST(ScapsLab,LiveBandRefreshesOnDraftChangeIsCoalescedAndKeepsHistoryClean) {
    auto doc=TinyDevice();
        {   // I-V auto off, band auto on: a discrete edit refreshes the band at the working bias, no history entry, no "running" noise
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(doc);w.autoRecalculate=false;w.autoRecalculateBand=true;
        ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());Settle(w);
        const auto history=w.Current().history.size();
        auto d=w.Current().Document();d["layers"][0]["doping"]["Nd_m3"]=d["layers"][0]["doping"]["Nd_m3"].get<double>()*10+1e21;
        ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"document",d}}).at("ok").get<bool>());
        for(int i=0;i<60000 && !w.Current().BandFresh();++i) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        EXPECT_TRUE(w.Current().BandFresh());EXPECT_EQ(w.Current().history.size(),history);EXPECT_GE(w.liveBandsStarted,1u);
        EXPECT_TRUE(w.Current().Stale())<<"I-V is outdated, the stale marker stays";
        EXPECT_NEAR(w.Current().bandResult.at("points").at(0).at("voltage_V").get<double>(),w.Current().biasVoltage_V,1e-12);
        const auto before=w.liveBandsStarted;for(int i=0;i<200;++i){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_EQ(w.liveBandsStarted,before)<<"no recompute while nothing changed";
    }
    {   // toggle off: no band jobs at all
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(doc);w.autoRecalculate=false;w.autoRecalculateBand=false;
        ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());Settle(w);
        auto d=w.Current().Document();d["layers"][0]["doping"]["Nd_m3"]=3e21;ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"document",d}}).at("ok").get<bool>());
        for(int i=0;i<400;++i) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        EXPECT_EQ(w.liveBandsStarted,0u);EXPECT_FALSE(w.Current().BandFresh());
    }
    {   // drag: <=10 band jobs/s, latest wins, alternates with the I-V preview, final band is for the final draft
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDocument(doc);w.autoRecalculate=true;w.autoRecalculateBand=true;
        ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());Settle(w);const auto history=w.Current().history.size();
        w.dragging=true;const auto t0=std::chrono::steady_clock::now();const double nd0=doc["layers"][0]["doping"]["Nd_m3"].get<double>();
        for(int i=0;i<40;++i) {
            auto d=w.Current().Document();d["layers"][0]["doping"]["Nd_m3"]=nd0*(1+.05*i);
            ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"document",d},{"gesture_id","drag"}}).at("ok").get<bool>());
            for(int k=0;k<5;++k) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
        EXPECT_GE(w.liveBandsStarted,1u);EXPECT_LE(w.liveBandsStarted,unsigned(ms/33)+2)<<"coalesced: <=30 per second; ms="<<ms;
        EXPECT_GE(w.previewsStarted,1u)<<"the I-V preview is not starved";EXPECT_EQ(w.Current().history.size(),history);
        auto last=w.EditDraft()?DraftDocument(*w.EditDraft()):w.Current().Document();last["layers"][0]["doping"]["Nd_m3"]=nd0*7;
        w.dragging=false;ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"document",last},{"gesture_id","drag"}}).at("ok").get<bool>());
        Settle(w);EXPECT_EQ(w.Current().result.at("project_snapshot").at("layers")[0]["doping"]["Nd_m3"].get<double>(),nd0*7);
    }
}

TEST(ScapsLabLive, ReferenceSurvivesReleaseAndTheNextParameterGesture) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDocument(TinyDevice());
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Settle(w);
    auto& d=w.Current();const auto reference=d.Iv();const auto history=d.history.size();const auto depth=d.undo.GetUndoDepth();
    w.autoRecalculate=true;w.autoRecalculateBand=true;
    for(int gesture=0;gesture<2;++gesture) {
        ASSERT_TRUE(w.Exec(Cmd::LiveEditing::Make(true)).value("ok",false));EXPECT_TRUE(w.dragging);
        for(int tick=0;tick<4;++tick) {
            auto draft=CreateRef<ProjectDraft>(w.EditDraft()?*w.EditDraft():*d.draft);draft->project.temperature_K=305+gesture*10+tick;
            ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(draft,"gesture-"+std::to_string(gesture))).value("ok",false));
            for(int k=0;k<8;++k){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        }
        ASSERT_TRUE(w.Dispatch({{"operation","live_editing"},{"active",false}}).value("ok",false));EXPECT_FALSE(w.dragging);
        Settle(w);ASSERT_FALSE(w.Pending());EXPECT_EQ(d.changeIv,reference);EXPECT_EQ(d.history.size(),history);
        EXPECT_EQ(d.undo.GetUndoDepth(),depth+gesture+1);EXPECT_EQ(d.resultRevision,d.revision);EXPECT_TRUE(d.previewV.empty());
    }
    const double final=d.draft->project.temperature_K;ASSERT_TRUE(w.Exec(Cmd::Undo{}).value("ok",false));
    EXPECT_LT(d.draft->project.temperature_K,final);EXPECT_EQ(d.changeIv,reference);
    w.autoRecalculate=w.autoRecalculateBand=false;
    while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_FALSE(d.changeIv);
}
TEST(ScapsLabLive, WarmBandMatchesColdAfterAParameterEdit) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDocument(TinyDevice());
    const auto run=[&]{w.StartOperation({{"operation","band"},{"live",true},{"voltage_V",.01}},w.Current().id);while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    run();ASSERT_TRUE(w.Current().warmBand);auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=301;w.Current().SetDraft(next);
    run();const auto warm=w.Current().Band();w.Current().warmBand.reset();run();const auto cold=w.Current().Band();
    ASSERT_EQ(warm->points.size(),1u);const auto& a=warm->points[0];const auto& b=cold->points[0];ASSERT_TRUE(a.valid);ASSERT_TRUE(b.valid);
    EXPECT_NEAR(a.J_A_m2,b.J_A_m2,std::max(1e-8,std::abs(b.J_A_m2)*1e-5));
    ASSERT_EQ(a.Ec_eV.size(),b.Ec_eV.size());for(std::size_t i=0;i<a.Ec_eV.size();++i)EXPECT_NEAR(a.Ec_eV.get()[i],b.Ec_eV.get()[i],1e-6);
}
TEST(ScapsLabLive, ContinuousEditsApplyWithoutWorkerJobsAndRefreshWhileHeld) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDocument(TinyDevice());
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make()).value("ok",false));Settle(w);const auto reference=w.Current().Iv();
    const auto depth=w.Current().undo.GetUndoDepth();w.autoRecalculateBand=true;w.dragging=w.interacting=true;
    int fresh=0;
    for(int tick=0;tick<60;++tick){
        auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=300+.02*(tick+1);
        ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,"60Hz-drag")).value("ok",false));
        EXPECT_EQ(w.Current().draft,next);EXPECT_NE(w.ActiveOp()?w.ActiveOp()->request.value("operation",std::string{}):"","edit");
        for(int k=0;k<16;++k){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        if(w.Current().BandFresh())++fresh;
    }
    EXPECT_GE(fresh,3);EXPECT_EQ(w.Current().changeIv,reference);EXPECT_EQ(w.Current().undo.GetUndoDepth(),depth+1);
    w.dragging=w.interacting=false;w.autoRecalculateBand=false;while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
TEST(ScapsLabLive, QeOnEditRunsDuringDragAndRefinesOnRelease) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().SetDocument(TinyDevice());
    auto plan=CreateRef<RunPlan>();plan->actions[4].include=true;plan->actions[4].onEdit=true;plan->actions[4].range=SweepRange{SweepSampling::Points,false,500,700,1,17};
    ASSERT_TRUE(w.Exec(Cmd::SingleShot::Make(plan)).value("ok",false));Settle(w);const auto reference=w.Current().acResults.get("qe");
    w.SetRunPlanTyped(plan);w.dragging=true;auto next=CreateRef<ProjectDraft>(*w.Current().draft);next->project.temperature_K=310;
    ASSERT_TRUE(w.Exec(Cmd::EditDocument::Make(next,"qe-drag")).value("ok",false));
    w.OnUpdate(0);ASSERT_TRUE(w.Current().liveMesh);ASSERT_TRUE(w.ActiveOp());EXPECT_EQ(w.ActiveOp()->liveMeshInput,w.Current().liveMesh);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(w.Current().acResults.get("qe")==reference && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_NE(w.Current().acResults.get("qe"),reference);EXPECT_EQ(w.Current().changeAc.at("qe"),reference);EXPECT_LE(w.Current().acResults.get("qe")->points.size(),9u);
    w.dragging=false;Settle(w);EXPECT_EQ(w.Current().acResults.get("qe")->points.size(),17u);EXPECT_EQ(w.Current().changeAc.at("qe"),reference);
}
TEST(DlcpUi,TypedPanelsKeepResultsWhileSettingsChange) {
    EXPECT_EQ(DisplayBounds("sweep.step:voltage_V").displayUnit,"V");
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1400,1100);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    {
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDocument(ReadProject(SCAPSLAB_PROJECT));w.autoRecalculate=false;w.autoRecalculateBand=false;
        auto result=CreateRef<AcResult>();result->kind="dlcp";result->dlcp_eps_r=11.7;result->snapshot=w.Current().draft;
        for(int i=0;i<3;++i) {
            auto row=CreateRef<DlcpProfilePoint>();auto& p=row->value;p.V_max_V=-1.4+i*.5;p.C0_F=1e-9+i*1e-10;p.C1_F_per_V=-1e-10;p.C2_F_per_V2=1e-11;p.x_m=(.8-i*.1)*1e-6;p.N_DL_m3=1e21;p.fit_relative_rms=1e-6;p.status=scaps_core::DlcpStatus::Converged;row->cv_x_m=p.x_m;row->N_CV_m3=1.05e21;
            for(double a:{.05,.1,.15,.2,.25}){scaps_core::DlcpSample s;s.amplitude_V=a;s.C_F=p.C0_F+a*(p.C1_F_per_V+a*p.C2_F_per_V2);p.samples.push_back(s);}result->dlcp.push_back(row);
            AcPoint cv;cv.temperature_K=300;cv.frequency_Hz=3e5;result->points.push_back(cv);
        }
        w.Current().acResults.set("dlcp",result);Panel panel(&w,23,true);std::vector<double> times;
        for(int i=0;i<120;++i) {
            const auto start=std::chrono::steady_clock::now();
            if(i>=10 && i<70){auto plan=CreateRef<RunPlan>(*w.Current().runPlan);plan->actions[5].frequency_Hz=3e5+i*1000;w.SetRunPlanTyped(plan,"one-drag");}
            io.DeltaTime=1.f/60;io.AddMousePosEvent(400+float(i%20)*20,120);ImGui::NewFrame();panel.Render();ImGui::Render();
            EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);EXPECT_EQ(w.Current().acResults.get("dlcp"),result);EXPECT_FALSE(result->HasView());EXPECT_FALSE(w.Pending());
            if(i>=10)times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        int logPlots=0;for(const auto& plot:GImPlot->Plots.Buf)if(plot.Axes[ImAxis_Y1].Scale==ImPlotScale_Log10){++logPlots;int labels=0;for(const auto& tick:plot.Axes[ImAxis_Y1].Ticker.Ticks)labels+=tick.ShowLabel;EXPECT_GE(labels,3);}
        EXPECT_EQ(logPlots,1);
        EXPECT_EQ(w.Current().undo.GetUndoDepth(),1u);std::sort(times.begin(),times.end());std::printf("DLCP settings + plot CPU frame: median %.3f ms, p95 %.3f ms, max %.3f ms\n",times[times.size()/2],times[times.size()*95/100],times.back());
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
