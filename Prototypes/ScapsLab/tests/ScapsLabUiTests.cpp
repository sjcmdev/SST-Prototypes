#include <gtest/gtest.h>
#include "Panel.hpp"
#include "Core/Presentation/Panels/PanelRegistry.hpp"
#include "Core/Presentation/Panels/WindowWorkspace.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
using namespace SST;
using namespace SST::ScapsLab;
TEST(ScapsLab,ProjectTitleAndTabHaveNoConflictingIds) {
    ImGui::CreateContext();ImPlot::CreateContext();
    auto &io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize=ImVec2(1280,800);
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;io.ConfigDebugHighlightIdConflicts=true;
    io.Fonts->AddFontDefault();io.Fonts->Build();
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);
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
