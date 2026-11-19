#include <gtest/gtest.h>
#include "Presentation/CapLabSchematicEditor.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

TEST(CapLabSchematicDisplay, RenderingPreservesSIAndSurvivesExternalReplacement) {
    const auto oldGui=ImGui::GetCurrentContext();
    auto* gui=ImGui::CreateContext();
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1600,1200}; io.DeltaTime=1.f/60;
    io.ConfigDebugHighlightIdConflicts=true;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    ccui::CircuitDocument document;
    for (int k=0;k<9;++k) document.add(static_cast<ccui::Kind>(k),{140.f*k,0});
    document.elements[7].value=-1.5;
    document.elements[3].secondary=.85;
    const auto original=document.elements;
    ccui::CapLabSchematicEditor editor;
    ImVec2 voltagePosition{};
    float unselectedPropertiesEnd=0;
    for (int frame=0;frame<5;++frame) {
        if (frame>=3) io.AddMousePosEvent(400,300);
        else io.AddMousePosEvent(frame ? voltagePosition.x : -100,frame ? voltagePosition.y : -100);
        if (frame==1) io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);
        if (frame==2) io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);
        if (frame==3) {
            document.elements.clear(); document.nodes.clear();
            io.AddKeyEvent(ImGuiKey_R,true);
        }
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1500,1000});
        ImGui::Begin("Schematic test");
        ImGui::SetNextItemOpen(true,ImGuiCond_Always);
        EXPECT_FALSE(editor.properties(document));
        if (frame==0) unselectedPropertiesEnd=ImGui::GetCursorScreenPos().y;
        if (frame==2) EXPECT_GT(ImGui::GetCursorScreenPos().y,unselectedPropertiesEnd+20);
        ImGui::BeginChild("Canvas",{0,500});
        EXPECT_FALSE(editor.draw(document));
        if (frame==0) {
            const auto lo=ImGui::GetItemRectMin(), hi=ImGui::GetItemRectMax();
            const float zoom=std::clamp(std::min((hi.x-lo.x-30)/1260,(hi.y-lo.y-30)/90),.75f,4.f);
            voltagePosition={(lo.x+hi.x)*.5f+(980-570)*zoom,(lo.y+hi.y)*.5f-5*zoom};
            EXPECT_GT(voltagePosition.x,lo.x); EXPECT_LT(voltagePosition.x,hi.x);
        }
        ImGui::EndChild();
        EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
        ImGui::End(); ImGui::Render();
        if (frame<3) {
            ASSERT_EQ(document.elements.size(),original.size());
            for (size_t i=0;i<original.size();++i) {
                EXPECT_DOUBLE_EQ(document.elements[i].value,original[i].value);
                EXPECT_DOUBLE_EQ(document.elements[i].secondary,original[i].secondary);
                EXPECT_DOUBLE_EQ(document.elements[i].cj0,original[i].cj0);
                EXPECT_DOUBLE_EQ(document.elements[i].vbi,original[i].vbi);
                EXPECT_DOUBLE_EQ(document.elements[i].tau,original[i].tau);
                EXPECT_EQ(document.elements[i].rotation,original[i].rotation);
            }
        } else EXPECT_TRUE(document.elements.empty());
    }
    ImGui::DestroyContext(gui); ImGui::SetCurrentContext(oldGui);
}
