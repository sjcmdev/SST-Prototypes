#include <gtest/gtest.h>
#include "Panel.hpp"
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
TEST(ScapsLab,ProjectTitleAndTabHaveNoConflictingIds) {
    ImGui::CreateContext();ImPlot::CreateContext();
    auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,800);
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;io.ConfigDebugHighlightIdConflicts=true;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
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
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    w.Current().document["layers"][0]["defects"]=Json::array();w.Current().document["voltage"]={{"start_V",0.0},{"stop_V",0.05},{"step_V",0.05}};
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
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().document["layers"][0]["defects"].size(),1u);
    const auto path=std::filesystem::temp_directory_path()/"scapslab-ui-defect.yaml";WriteProject(w.Current().document,path.string());
    Workflow yaml(CreateRef<JobSystem>(nullptr,1));yaml.Current().document=ReadProject(path.string());
    EXPECT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());EXPECT_TRUE(yaml.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    while(w.Busy() || yaml.Busy()) {w.OnUpdate(0);yaml.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
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
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    auto second=w.Current().document["layers"][0];second["scapslab_id"]="test-layer";w.Current().document["layers"].push_back(second);
    w.Current().document["layers"][0]["defects"]=Json::array({NewDefect()});w.Current().document["interface_defects"]=Json::array({NewDefect(true)});
    for(int view:{1,2,3,4,11,12,13,14,15,16,17,18,19,20,21}) {
        Panel panel(&w,view);
        auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(340,1500));ImGui::Begin("Editor IDs");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1)<<view;};
        frame();frame();for(float y=35;y<900;y+=9) for(float x=160;x<330;x+=36) {io.AddMousePosEvent(x,y);frame();frame();}
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}

TEST(ScapsLab,StructureShowsEveryEditorGradingLaw) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,800);
    io.Fonts->AddFontDefault();io.Fonts->Build();Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);Panel panel(&w,14);
    int expected=0;
    for(const auto *law:{"linear","uniform","parabolic","logarithmic","exponential"}) {
        w.Current().document["layers"][0]["grading"]={{"Eg_eV",{{"law",law},{"left",1.12},{"right",1.15},{"bowing",.5}}}};
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
    io.Fonts->AddFontDefault();io.Fonts->Build();Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    const auto original=w.Current().document;Panel panel(&w,14);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1000,700));ImGui::Begin("Structure drag");panel.RenderContent();ImGui::End();ImGui::Render();};
    frame();frame();io.AddMousePosEvent(988,120);frame();frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(968,120);frame();io.AddMouseButtonEvent(0,false);frame();
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_LT(w.Current().document["layers"][0]["thickness_m"].get<double>(),original["layers"][0]["thickness_m"].get<double>());
    EXPECT_TRUE(w.Dispatch({{"operation","undo"}}).at("ok").get<bool>());
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().document,original);ImPlot::DestroyContext();ImGui::DestroyContext();
}

Json RunBatchViaUi(const Json &document,const Json &spec) {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,1100);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=document;w.Current().batchSpec=spec;Panel panel(&w,16);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1100,1050));ImGui::Begin("Batch parity");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
    frame();frame();const auto button=ImGui::FindWindowByName("Batch parity")->GetID("Run batch");bool found=false;
    for(float y=40;y<1000 && !found;y+=4) {io.AddMousePosEvent(40,y);frame();frame();if(GImGui->HoveredId==button) {found=true;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}}
    EXPECT_TRUE(found);while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response.dump();
    auto result=w.Current().batchResult;ImPlot::DestroyContext();ImGui::DestroyContext();return result;
}

Json RunLightViaUi() {
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1200,1100);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string());Panel panel(&w,19);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1100,1050));ImGui::Begin("Light parity");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
    frame();frame();
    // Select the actual I-V tab, then its Calculate button.
    auto click=[&](const char *label,float x,float first,float last) {
        const auto id=ImHashStr(label,0,ImHashStr("Light I-V",0,ImGui::FindWindowByName("Light parity")->GetID("Optical views")));bool found=false;
        for(float y=first;y<last && !found;y+=2) {io.AddMousePosEvent(x,y);frame();frame();if(GImGui->HoveredId==id) {found=true;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}}
        return found;
    };
    // Tab IDs are scoped by the tab bar; position is obtained from the rendered item.
    const auto tabId=ImHashStr("Light I-V",0,ImGui::FindWindowByName("Light parity")->GetID("Optical views"));
    bool tab=false;
    for(float y=45;y<150 && !tab;y+=2) for(float x=100;x<250 && !tab;x+=3) {io.AddMousePosEvent(x,y);frame();frame();if(GImGui->HoveredId==tabId) {tab=true;io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();}}
    EXPECT_TRUE(tab);EXPECT_TRUE(click("Calculate light I-V",75,60,220));
    while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response.dump();
    frame();frame();
    bool pvPlot=false;
    for(int i=0;i<GImPlot->Plots.GetBufSize();++i) if(auto *plot=GImPlot->Plots.GetByIndex(i);plot && plot->HasTitle() && std::string(plot->GetTitle())=="Light I-V") {
        pvPlot=true;const double jsc=w.Current().result.at("photovoltaic").at("Jsc_A_m2").get<double>()*.1;
        EXPECT_NEAR(plot->Axes[ImAxis_Y1].Range.Min,-1.2*jsc,1e-10);EXPECT_NEAR(plot->Axes[ImAxis_Y1].Range.Max,jsc,1e-10);
        EXPECT_STREQ(plot->GetAxisLabel(plot->Axes[ImAxis_Y1]),"J [mA/cm^2]");
        for(const auto *label:{"Voc","Jsc","MPP"}) {
            bool found=false;for(int item=0;item<plot->Items.GetLegendCount();++item)found|=std::string(plot->Items.GetLegendLabel(item))==label;
            EXPECT_TRUE(found)<<label;
        }
    }
    EXPECT_TRUE(pvPlot);
    Json result={{"document",w.Current().document},{"result",w.Current().result}};
    ImPlot::DestroyContext();ImGui::DestroyContext();return result;
}

TEST(ScapsLab,GenerationRecombinationPanelHasNoConflictingIds) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string());
    ASSERT_TRUE(w.Dispatch({{"operation","band"},{"voltage_V",0.}}).at("ok").get<bool>());
    while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.at("ok").get<bool>())<<w.response.dump();w.Current().result=w.Current().bandResult;
    ImGui::CreateContext();ImPlot::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1500,1600);io.ConfigDebugHighlightIdConflicts=true;io.Fonts->AddFontDefault();io.Fonts->Build();
    Panel panel(&w,22);
    auto frame=[&] {io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);ImGui::Begin("G/R UI");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_LE(GImGui->HoveredIdPreviousFrameItemCount,1);};
    frame();frame();for(float y=10;y<1500;y+=14) {io.AddMousePosEvent(450,y);frame();frame();}
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
