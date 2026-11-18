#include <gtest/gtest.h>
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Vendor/ImGui/imgui.h"
#include "Vendor/ImGui/imgui_internal.h"
#include <nlohmann/json.hpp>

namespace SST
{
    class ChromeWindow final : public EditorWindow
    {
    public:
        ChromeWindow() : EditorWindow("chrome", "Chrome", true, {.duplicable = true}) {}
        WindowDuplicate Duplicate() const override { return {CreateRef<ChromeWindow>(*this), {}}; }
        void RenderContent() override { ImGui::TextUnformatted("Content"); }
    };
    class WindowChromeTest : public testing::Test
    {
    protected:
        PanelRegistry panels;
        Ref<WindowWorkspace> workspace = CreateRef<WindowWorkspace>(panels);
        Ref<CommandRegistry> commands = CreateRef<CommandRegistry>();
        Ref<UndoStack> undo = CreateRef<UndoStack>();
        WindowId a = 0, b = 0;
        void SetUp() override
        {
            ImGui::CreateContext();
            auto &io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = ImVec2(1280, 800);
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.Fonts->AddFontDefault(); io.Fonts->Build();
            commands->SetUndoStack(undo); ASSERT_TRUE(workspace->Bind(commands, {}));
            a = panels.Add<ChromeWindow>(); b = panels.Add<ChromeWindow>();
            workspace->InstallSettingsHandler();
        }
        void TearDown() override { workspace->RemoveSettingsHandler(); ImGui::DestroyContext(); }
        void frame()
        {
            ImGui::GetIO().DeltaTime = 1.0f / 60;
            ImGui::NewFrame();
            ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);
            const auto entries = panels.Entries();
            for (const auto &entry : entries) entry.panel->Render();
            workspace->RenderOverlays(); ImGui::Render();
        }
        ImGuiWindow *gui(WindowId id) { return ImGui::FindWindowByName(workspace->Find(id)->ImGuiName().c_str()); }
        void dockBoth()
        {
            frame();
            const auto dockId = ImGui::GetMainViewport()->ID;
            for (auto id : {a, b}) { WindowRequest request{WindowAction::Dock, id}; request.dockId = dockId; ASSERT_TRUE(workspace->Execute(request)); }
            frame(); frame();
        }
    };
    TEST_F(WindowChromeTest, RendersFloatingMaximizedPinnedAndRestoredGeometry)
    {
        frame(); frame(); const auto original = workspace->Find(a)->GetState();
        ASSERT_TRUE(workspace->Execute({WindowAction::Maximize, a})); frame();
        EXPECT_EQ(gui(a)->Viewport->ID, ImGui::GetMainViewport()->ID); EXPECT_FLOAT_EQ(gui(a)->SizeFull.x, 1280);
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, a})); frame();
        EXPECT_FLOAT_EQ(gui(a)->SizeFull.x, original.width);
        ASSERT_TRUE(workspace->Execute({WindowAction::Pin, a})); frame();
        EXPECT_TRUE(gui(a)->Flags & ImGuiWindowFlags_NoMove); EXPECT_TRUE(gui(a)->Flags & ImGuiWindowFlags_NoResize);
        ASSERT_TRUE(workspace->Execute({WindowAction::Minimize, a})); frame();
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, a})); frame();
        EXPECT_TRUE(workspace->Find(a)->IsVisible());
    }
    TEST_F(WindowChromeTest, DockedTabChromeAndMiddleClickCloseInactiveTab)
    {
        dockBoth(); ASSERT_NE(gui(a)->DockNode, nullptr); ASSERT_EQ(gui(a)->DockNode, gui(b)->DockNode);
        const auto *node = gui(a)->DockNode; ASSERT_NE(node->TabBar, nullptr);
        EXPECT_GT(node->TabBar->Tabs.Size, 2);
        const auto inactive = gui(a)->DockTabIsVisible ? b : a;
        const auto rect = gui(inactive)->DC.DockTabItemRect;
        ImGui::GetIO().AddMousePosEvent(rect.GetCenter().x, rect.GetCenter().y); frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle, true); frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Middle, false); frame();
        EXPECT_FALSE(workspace->Find(inactive)->IsVisible());
    }
    TEST_F(WindowChromeTest, ContextMenuOnInactiveDockTab)
    {
        dockBoth();
        const auto inactive = gui(a)->DockTabIsVisible ? b : a;
        const auto rect = gui(inactive)->DC.DockTabItemRect;
        ImGui::GetIO().AddMousePosEvent(rect.GetCenter().x, rect.GetCenter().y); frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true); frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false); frame();
        EXPECT_GT(GImGui->OpenPopupStack.Size, 0);
    }
    TEST_F(WindowChromeTest, LayoutSettingsIncludeWindowStateAndNamedWorkspacesWithoutRecursiveGrowth)
    {
        frame(); ASSERT_TRUE(workspace->Execute({WindowAction::Minimize, a}));
        WindowRequest save{WindowAction::SaveWorkspace}; save.shortName = "Analysis";
        ASSERT_TRUE(workspace->Execute(save));
        const std::string first = ImGui::SaveIniSettingsToMemory();
        EXPECT_NE(first.find("[SSTWindows][Workspace]"), std::string::npos);
        ASSERT_TRUE(workspace->Execute(save));
        const std::string second = ImGui::SaveIniSettingsToMemory(); EXPECT_LT(second.size(), first.size() + 1000);
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, a}));
        WindowRequest load{WindowAction::LoadWorkspace}; load.shortName = "Analysis"; ASSERT_TRUE(workspace->Execute(load));
        EXPECT_EQ(workspace->Find(a)->GetState().mode, WindowMode::Minimized) << workspace->LastError();
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, a}));
        ImGui::LoadIniSettingsFromMemory(second.c_str());
        EXPECT_EQ(workspace->Find(a)->GetState().mode, WindowMode::Minimized) << workspace->LastError();
        frame();
    }
    TEST_F(WindowChromeTest, FreezeRetainsStateAfterEditorClearsPanels)
    {
        frame(); const auto before = workspace->Save(); workspace->FreezeSettings(); panels.Clear();
        const std::string ini = ImGui::SaveIniSettingsToMemory();
        EXPECT_NE(ini.find("sst-window-"), std::string::npos);
        EXPECT_NE(ini.find("\\\"type\\\":\\\"chrome\\\""), std::string::npos);
    }
    TEST_F(WindowChromeTest, CoreCycleFocusSurvivesThePreviouslyFocusedWindowRenderingFirst)
    {
        frame(); frame(); ASSERT_TRUE(workspace->Execute({WindowAction::Activate, a})); frame();
        ASSERT_TRUE(commands->Execute(CommandID{"window.next"})); frame(); frame();
        EXPECT_EQ(workspace->Capture().active, b); EXPECT_EQ(GImGui->NavWindow, gui(b));
    }

    TEST_F(WindowChromeTest, LayoutLockKeepsExistingDockingAndPreventsUndock)
    {
        dockBoth(); ASSERT_NE(gui(a)->DockNode, nullptr);
        const auto dock = gui(a)->DockId;
        ASSERT_TRUE(workspace->Execute({WindowAction::LockLayout})); frame(); frame();
        EXPECT_EQ(gui(a)->DockId, dock); ASSERT_NE(gui(a)->DockNode, nullptr);
        EXPECT_TRUE(gui(a)->DockNode->MergedFlags & ImGuiDockNodeFlags_NoUndocking);
        ASSERT_TRUE(workspace->Execute({WindowAction::LockLayout})); frame(); frame();
        EXPECT_EQ(gui(a)->DockId, dock);
        EXPECT_FALSE(gui(a)->DockNode->MergedFlags & ImGuiDockNodeFlags_NoUndocking);
    }

    TEST_F(WindowChromeTest, MigratedPanelRetainsLegacyIniPlacement)
    {
        ImGui::LoadIniSettingsFromMemory("[Window][Chrome]\nPos=123,89\nSize=410,300\nCollapsed=0\n\n");
        frame(); frame();
        EXPECT_FLOAT_EQ(gui(a)->Pos.x, 123); EXPECT_FLOAT_EQ(gui(a)->Pos.y, 89);
        EXPECT_FLOAT_EQ(gui(a)->SizeFull.x, 410); EXPECT_FLOAT_EQ(gui(a)->SizeFull.y, 300);
    }

}
