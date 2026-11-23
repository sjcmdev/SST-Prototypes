#include <gtest/gtest.h>
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Vendor/ImGui/imgui.h"
#include "Vendor/ImGui/imgui_internal.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include "Vendor/ImGuiNotify/win32Example/fonts/IconsFontAwesome6.h"
#include "App/Window.hpp"
#include "App/Managers/ConfigManager.hpp"
#define GLFW_INCLUDE_NONE
#include "Vendor/GLFW/include/GLFW/glfw3.h"
#include <sst/ui/Widgets/LabelledControls.hpp>

namespace SST
{
    class ChromeWindow final : public EditorWindow
    {
    public:
        ChromeWindow() : EditorWindow("chrome", "Chrome", true, {.duplicable = true}) {}
        WindowDuplicate Duplicate() const override { return {CreateRef<ChromeWindow>(*this), {}}; }
        void RenderContent() override {
            if (ImGui::BeginChild("##content", {0, 0})) ImGui::TextUnformatted("Content");
            ImGui::EndChild();
        }
    };
    class ScrollWindow final : public EditorWindow
    {
    public:
        ScrollWindow(WindowCapabilities capabilities = {}) : EditorWindow("scroll", "Scroll", true, capabilities) {}
        float height = 100;
        void RenderContent() override { ImGui::Dummy({1, height}); }
    };
    class WindowChromeTest : public testing::Test
    {
    protected:
        PanelRegistry panels;
        Ref<WindowWorkspace> workspace = CreateRef<WindowWorkspace>(panels);
        Ref<CommandRegistry> commands = CreateRef<CommandRegistry>();
        Ref<UndoStack> undo = CreateRef<UndoStack>();
        WindowId a = 0, b = 0;
        bool restoreDefaultsEachFrame = false;
        int defaultLayoutCalls = 0;
        void SetUp() override
        {
            ImGui::CreateContext();
            auto &io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = ImVec2(1280, 800);
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigDebugHighlightIdConflicts = true;
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
            if (restoreDefaultsEachFrame) workspace->RestoreDefaultLayoutIfNeeded();
            const auto entries = panels.Entries();
            for (const auto &entry : entries) entry.panel->Render();
            workspace->RenderOverlays(); ImGui::Render();
            EXPECT_EQ(GImGui->DebugDrawIdConflictsId, 0u);
        }
        ImGuiWindow *gui(WindowId id) { return ImGui::FindWindowByName(workspace->Find(id)->ImGuiName().c_str()); }
        void clickChrome(WindowId id, int button)
        {
            const auto bar = gui(id)->TitleBarRect();
            ImGui::GetIO().AddMousePosEvent(bar.Max.x - (button + .5f) * bar.GetHeight(), bar.GetCenter().y); frame();
            ImGui::GetIO().AddMouseButtonEvent(0, true); frame(); frame(); frame();
            ImGui::GetIO().AddMouseButtonEvent(0, false); frame(); frame();
        }
        void defaultTabs()
        {
            workspace->SetDefaultLayout([this] {
                ++defaultLayoutCalls;
                const auto* viewport = ImGui::GetMainViewport();
                const auto root = viewport->ID;
                ImGui::DockBuilderRemoveNode(root);
                ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodePos(root, viewport->WorkPos);
                ImGui::DockBuilderSetNodeSize(root, viewport->WorkSize);
                for (const auto& window : workspace->Windows()) {
                    auto state = window->GetState(); state.dockId = root;
                    window->ApplyState(state);
                    ImGui::DockBuilderDockWindow(window->ImGuiName().c_str(), root);
                }
                ImGui::DockBuilderFinish(root);
            });
            restoreDefaultsEachFrame = true;
        }
        void dockBoth()
        {
            frame();
            const auto dockId = ImGui::GetMainViewport()->ID;
            for (auto id : {a, b}) { WindowRequest request{WindowAction::Dock, id}; request.dockId = dockId; ASSERT_TRUE(workspace->Execute(request)); }
            frame(); frame();
        }
    };
    TEST_F(WindowChromeTest, ClickingEveryDockTabSelectsAndRetainsItsContent)
    {
        const auto c = panels.Add<ChromeWindow>(), d = panels.Add<ChromeWindow>();
        defaultTabs();
        for (int i = 0; i < 5; ++i) frame();
        ASSERT_EQ(defaultLayoutCalls, 1);
        workspace->Find(a)->SetTitle("Raw Plot"); workspace->Find(b)->SetTitle("Raw Plot 1");
        workspace->Find(c)->SetTitle("MC Scatter"); workspace->Find(d)->SetTitle("MC Curves");
        for (float scale : {1.0f, 1.5f}) {
            ImGui::GetStyle().FontScaleMain = scale; frame(); frame();
            for (auto id : {a, b, c, d}) {
                const auto pos = gui(id)->DC.DockTabItemRect.GetCenter();
                ImGui::GetIO().AddMousePosEvent(pos.x, pos.y); frame();
                ImGui::GetIO().AddMouseButtonEvent(0, true); frame();
                ImGui::GetIO().AddMouseButtonEvent(0, false); frame();
                for (int i = 0; i < 30; ++i) {
                    frame();
                    EXPECT_TRUE(gui(id)->DockTabIsVisible) << id << " scale=" << scale;
                    EXPECT_EQ(gui(id)->DockNode->SelectedTabId, gui(id)->TabId);
                }
            }
        }
        EXPECT_EQ(defaultLayoutCalls, 1);
    }

    TEST_F(WindowChromeTest, RestoreButtonAfterMaximize)
    {
        workspace->Find(b)->SetVisible(false); frame(); frame();
        const auto original = workspace->Find(a)->GetState();
        ASSERT_TRUE(workspace->Execute({WindowAction::Maximize, a})); frame(); frame();
        clickChrome(a, 2); EXPECT_EQ(workspace->Find(a)->GetState().mode, WindowMode::Normal);
        EXPECT_FLOAT_EQ(gui(a)->SizeFull.x, original.width);
    }
    TEST_F(WindowChromeTest, UnpinButton)
    {
        workspace->Find(b)->SetVisible(false); frame(); frame();
        ASSERT_TRUE(workspace->Execute({WindowAction::Pin, a})); frame(); frame();
        clickChrome(a, 1); EXPECT_FALSE(workspace->Find(a)->GetState().pinned);
    }
    TEST_F(WindowChromeTest, CloseButtonWithChildContentKeepsSiblingOpen)
    {
        auto state = workspace->Find(b)->GetState(); state.x = 950; state.width = 250; workspace->Find(b)->ApplyState(state);
        frame(); frame(); clickChrome(a, 0);
        EXPECT_FALSE(workspace->Find(a)->IsVisible()); EXPECT_TRUE(workspace->Find(b)->IsVisible());
    }
    TEST_F(WindowChromeTest, MinimizeButtonStillRestoresFromTheStrip)
    {
        workspace->Find(b)->SetVisible(false); frame(); frame(); clickChrome(a, 3);
        ASSERT_EQ(workspace->Find(a)->GetState().mode, WindowMode::Minimized);
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, a})); frame();
        EXPECT_TRUE(workspace->Find(a)->IsVisible()); EXPECT_EQ(workspace->Find(a)->GetState().mode, WindowMode::Normal);
    }
    TEST_F(WindowChromeTest, PinKeepsDockAndSiblingVisible)
    {
        frame();
        const auto root = ImGui::GetMainViewport()->ID;
        ImGuiID top = 0, bottom = 0;
        ImGui::DockBuilderSplitNode(root, ImGuiDir_Up, .5f, &top, &bottom);
        ImGui::DockBuilderDockWindow(workspace->Find(a)->ImGuiName().c_str(), top);
        ImGui::DockBuilderDockWindow(workspace->Find(b)->ImGuiName().c_str(), bottom);
        ImGui::DockBuilderFinish(root); frame(); frame();
        const auto dock = gui(a)->DockId;
        const auto sibling = gui(b)->Rect();
        ASSERT_TRUE(workspace->Execute({WindowAction::Pin, a})); frame(); frame();
        EXPECT_EQ(gui(a)->DockId, dock);
        EXPECT_FALSE(gui(a)->Rect().Overlaps(sibling));
        EXPECT_TRUE(gui(b)->DockTabIsVisible);
    }
    TEST_F(WindowChromeTest, MaximizeRestoreKeepsSplitDockLayout)
    {
        frame(); const auto root = ImGui::GetMainViewport()->ID;
        ImGuiID top = 0, bottom = 0;
        ImGui::DockBuilderSplitNode(root, ImGuiDir_Up, .5f, &top, &bottom);
        ImGui::DockBuilderDockWindow(workspace->Find(a)->ImGuiName().c_str(), top);
        ImGui::DockBuilderDockWindow(workspace->Find(b)->ImGuiName().c_str(), bottom);
        ImGui::DockBuilderFinish(root); frame(); frame();
        const auto dock = gui(a)->DockId; const auto sibling = gui(b)->Rect();
        ASSERT_TRUE(workspace->Execute({WindowAction::Maximize, a})); frame(); frame();
        const std::string maximizedIni = ImGui::SaveIniSettingsToMemory();
        workspace->RemoveSettingsHandler(); ImGui::DestroyContext(); ImGui::CreateContext();
        auto &io = ImGui::GetIO(); io.IniFilename = nullptr; io.DisplaySize = ImVec2(1280, 800);
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; io.ConfigDebugHighlightIdConflicts = true;
        io.Fonts->AddFontDefault(); io.Fonts->Build(); workspace->InstallSettingsHandler();
        ImGui::LoadIniSettingsFromMemory(maximizedIni.c_str()); frame(); frame();
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, a})); frame(); frame();
        EXPECT_EQ(gui(a)->DockId, dock);
        EXPECT_FLOAT_EQ(gui(b)->Pos.y, sibling.Min.y); EXPECT_FLOAT_EQ(gui(b)->Size.y, sibling.GetHeight());
    }
    TEST_F(WindowChromeTest, PopupAbovePinnedWindow)
    {
        workspace->Find(b)->SetVisible(false); frame(); frame();
        ASSERT_TRUE(workspace->Execute({WindowAction::Pin, a})); frame();
        const auto pos = gui(a)->TitleBarRect().GetCenter();
        ImGui::GetIO().AddMousePosEvent(pos.x, pos.y); frame();

        ImGui::GetIO().AddMouseButtonEvent(1, true); frame();
        ImGui::GetIO().AddMouseButtonEvent(1, false); frame(); frame();
        ASSERT_GT(GImGui->OpenPopupStack.Size, 0);
        const auto popup = GImGui->OpenPopupStack.back().Window; ASSERT_NE(popup, nullptr);
        EXPECT_LT(GImGui->Windows.find_index(gui(a)), GImGui->Windows.find_index(popup));
    }
    TEST_F(WindowChromeTest, DockedCloseWithChildContentDoesNotCloseSibling)
    {
        dockBoth(); frame(); frame(); // Newly added action tabs need their first layout before hit testing.
        const auto active = gui(a)->DockTabIsVisible ? a : b;
        const auto sibling = active == a ? b : a;
        auto* bar = gui(active)->DockNode->TabBar;
        ImGuiTabItem close;
        for (auto &tab : bar->Tabs)
            if ((tab.Flags & ImGuiTabItemFlags_Trailing) && std::strcmp(ImGui::TabBarGetTabName(bar, &tab), ICON_FA_XMARK) == 0) close = tab;
        ASSERT_NE(close.ID, 0u);
        const ImVec2 pos(bar->BarRect.Min.x + close.Offset + close.Width / 2, bar->BarRect.GetCenter().y);
        ImGui::GetIO().AddMousePosEvent(pos.x, pos.y); frame();
        const auto hovered = GImGui->HoveredId;
        ImGui::GetIO().AddMouseButtonEvent(0, true); frame(); frame(); frame();
        ImGui::GetIO().AddMouseButtonEvent(0, false); frame(); frame();
        EXPECT_FALSE(workspace->Find(active)->IsVisible()) << "close=" << close.ID << " hovered=" << hovered << " width=" << close.Width;
        EXPECT_TRUE(workspace->Find(sibling)->IsVisible());
    }
    TEST_F(WindowChromeTest, ContentWidthDoesNotJumpWhenScrollbarBecomesNecessary)
    {
        workspace->Find(a)->SetVisible(false); workspace->Find(b)->SetVisible(false);
        const auto id = panels.Add<ScrollWindow>(); frame(); frame();
        const float width = gui(id)->InnerRect.GetWidth();
        std::static_pointer_cast<ScrollWindow>(workspace->Find(id))->height = 2000;
        frame(); frame(); EXPECT_FLOAT_EQ(gui(id)->InnerRect.GetWidth(), width);
    }
    TEST_F(WindowChromeTest, AuxiliaryDoesNotRetainLegacyNativeIniSettings)
    {
        const auto id = panels.Add<ScrollWindow>(WindowCapabilities{.auxiliary = true});
        const auto name = workspace->Find(id)->ImGuiName();
        const auto legacy = "[Window][Scroll]\nPos=20,30\nSize=410,300\nCollapsed=0\n\n[Window][" + name + "]\nPos=123,89\nSize=410,300\nCollapsed=0\n\n";
        ImGui::LoadIniSettingsFromMemory(legacy.c_str()); frame();
        const auto suffix = "sst-window-" + std::to_string(id);
        EXPECT_EQ(std::string(ImGui::SaveIniSettingsToMemory()).find(suffix), std::string::npos);
        ASSERT_TRUE(workspace->Execute({WindowAction::Activate, id})); frame(); frame();
        EXPECT_TRUE(gui(id)->Flags & ImGuiWindowFlags_NoSavedSettings);
        const std::string saved = ImGui::SaveIniSettingsToMemory();
        EXPECT_EQ(saved.find(suffix), std::string::npos);
        EXPECT_EQ(saved.find("[Window][Scroll]"), std::string::npos);
    }
    TEST_F(WindowChromeTest, ResetClearsDockPinMaximizeAndRestoresRegisteredDefaults)
    {
        const auto initial = workspace->Find(a)->GetState();
        frame(); ASSERT_TRUE(workspace->Execute({WindowAction::Maximize, a})); frame();
        ASSERT_TRUE(workspace->Execute({WindowAction::Pin, b}));
        ASSERT_TRUE(commands->Execute(CommandID{"app.layout.reset"}));
        EXPECT_EQ(workspace->Find(a)->GetState(), initial);
        EXPECT_FALSE(workspace->Find(b)->GetState().pinned);
        frame(); EXPECT_EQ(gui(a)->DockId, 0u);
    }
    TEST_F(WindowChromeTest, StartupDefaultsRunOnceAndExplicitResetDocksAllDefaults)
    {
        int rebuilt = 0;
        workspace->SetDefaultLayout([&] {
            ++rebuilt; const auto* viewport = ImGui::GetMainViewport(); ImGuiID center = viewport->ID;
            ImGui::DockBuilderRemoveNode(center); ImGui::DockBuilderAddNode(center, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(center, viewport->WorkPos); ImGui::DockBuilderSetNodeSize(center, viewport->WorkSize);
            const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .25f, nullptr, &center);
            for (auto id : {a,b}) {
                auto window = workspace->Find(id); auto state = window->GetState();
                state.mode = state.restoreMode = WindowMode::Normal; state.pinned = false; state.visible = true;
                state.dockId = id == a ? left : center; window->ApplyState(state);
                ImGui::DockBuilderDockWindow(window->ImGuiName().c_str(), state.dockId);
            }
            ImGui::DockBuilderFinish(viewport->ID);
        });
        frame();
        ImGui::NewFrame(); workspace->RestoreDefaultLayoutIfNeeded(); ImGui::Render();
        frame(); frame(); EXPECT_EQ(rebuilt,1);
        for(auto id : {a,b}) {ASSERT_NE(gui(id)->DockNode,nullptr); EXPECT_EQ(ImGui::DockNodeGetRootNode(gui(id)->DockNode)->ID,ImGui::GetMainViewport()->ID);}
        ImGui::NewFrame(); workspace->RestoreDefaultLayoutIfNeeded(); ImGui::Render(); EXPECT_EQ(rebuilt,1);
        ASSERT_TRUE(workspace->Execute({WindowAction::Maximize,a})); frame(); 
        ImGui::NewFrame(); workspace->RestoreDefaultLayoutIfNeeded(); ImGui::Render(); frame(); frame(); EXPECT_EQ(rebuilt,1);
        EXPECT_EQ(workspace->Find(a)->GetState().mode, WindowMode::Maximized);
        ASSERT_TRUE(workspace->Execute({WindowAction::Close,b})); 
        ImGui::NewFrame(); ASSERT_TRUE(commands->Execute(CommandID{"app.layout.reset"})); ImGui::Render(); frame(); frame();
        EXPECT_EQ(rebuilt,2);
        for(auto id : {a,b}) {EXPECT_TRUE(workspace->Find(id)->IsVisible()); ASSERT_NE(gui(id)->DockNode,nullptr); EXPECT_EQ(workspace->Find(id)->GetState().mode,WindowMode::Normal);}
    }
    TEST_F(WindowChromeTest, DefaultChecksPreserveLoadedFloatingAndMaximizedWindows)
    {
        const auto name = workspace->Find(a)->ImGuiName();
        const std::string ini = "[Window][" + name + "]\nPos=97,83\nSize=411,307\nCollapsed=0\n\n";
        ImGui::LoadIniSettingsFromMemory(ini.c_str());
        auto state = workspace->Find(b)->GetState(); state.mode = WindowMode::Maximized;
        workspace->Find(b)->ApplyState(state);
        defaultTabs();
        for (int i = 0; i < 15; ++i) frame();
        EXPECT_EQ(defaultLayoutCalls, 0);
        EXPECT_EQ(gui(a)->DockId, 0u);
        EXPECT_FLOAT_EQ(gui(a)->Pos.x, 97); EXPECT_FLOAT_EQ(gui(a)->SizeFull.x, 411);
        EXPECT_EQ(workspace->Find(b)->GetState().mode, WindowMode::Maximized);
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, b}));
        for (int i = 0; i < 10; ++i) frame();
        EXPECT_EQ(defaultLayoutCalls, 0);
        EXPECT_EQ(workspace->Find(b)->GetState().mode, WindowMode::Normal);
    }
    TEST_F(WindowChromeTest, DragOutAndBackAndResetSurviveRepeatedDefaultChecks)
    {
        defaultTabs(); for (int i = 0; i < 5; ++i) frame();
        const auto root = gui(a)->DockId;
        const auto start = gui(a)->DC.DockTabItemRect.GetCenter();
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(start.x, start.y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMousePosEvent(start.x + 80, start.y + 200); frame(); frame();
        io.AddMousePosEvent(300, 300); frame(); frame();
        io.AddMouseButtonEvent(0, false); frame();
        for (int i = 0; i < 10; ++i) frame();
        ASSERT_EQ(gui(a)->DockId, 0u);
        EXPECT_EQ(defaultLayoutCalls, 1);
        const auto title = gui(a)->TitleBarRect().GetCenter();
        io.AddMousePosEvent(title.x, title.y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        ASSERT_NE(gui(b)->DockNode, nullptr);
        const auto target = gui(b)->DockNode->Rect().GetCenter();
        io.AddMousePosEvent(target.x, target.y); frame(); frame(); frame();
        io.AddMouseButtonEvent(0, false); frame();
        for (int i = 0; i < 10; ++i) frame();
        EXPECT_EQ(gui(a)->DockId, root);
        EXPECT_EQ(defaultLayoutCalls, 1);
        ASSERT_TRUE(workspace->Execute({WindowAction::Maximize, a}));
        for (int i = 0; i < 10; ++i) frame();
        EXPECT_EQ(workspace->Find(a)->GetState().mode, WindowMode::Maximized);
        ASSERT_TRUE(workspace->Execute({WindowAction::Restore, a}));
        for (int i = 0; i < 10; ++i) frame();
        EXPECT_EQ(gui(a)->DockId, root);
        ImGui::NewFrame();
        ASSERT_TRUE(commands->Execute(CommandID{"app.layout.reset"}));
        ImGui::Render();
        for (int i = 0; i < 15; ++i) frame();
        EXPECT_EQ(defaultLayoutCalls, 2);
        EXPECT_EQ(gui(a)->DockId, root); EXPECT_EQ(gui(b)->DockId, root);
    }
    TEST_F(WindowChromeTest, ScientificControlUsesOneRowAndPreservesUntouchedValues)
    {
        ImGui::NewFrame(); ImGui::SetNextWindowSize({900, 300}); ImGui::Begin("Controls");
        const float y = ImGui::GetCursorPosY();
        double value = -1.234567891e-299;
        EXPECT_FALSE(sst::ui::widgets::LogValue("Density [cm^-3]", &value));
        EXPECT_DOUBLE_EQ(value, -1.234567891e-299);
        EXPECT_LE(ImGui::GetCursorPosY() - y, ImGui::GetFrameHeightWithSpacing() + 2 * ImGui::GetStyle().CellPadding.y);
        ImGui::End(); ImGui::Render();
    }
    TEST(NativeWindow, InitialPlacementAndMaximizeRespectMonitorWorkArea)
    {
        ASSERT_TRUE(glfwInit());
        struct Cleanup { ~Cleanup() { glfwTerminate(); } } cleanup;
        Window window; ASSERT_TRUE(window.Create(640, 480, "Work-area regression", {}));
        WindowConfig config; config.width = 10000; config.height = 10000; config.x = 0; config.y = 0;
        auto fits = [&](bool framed) {
            int x, y, width, height, left = 0, top = 0, right = 0, bottom = 0, count;
            glfwGetWindowPos(window.GetNativeHandle(), &x, &y); glfwGetWindowSize(window.GetNativeHandle(), &width, &height);
            if (framed) glfwGetWindowFrameSize(window.GetNativeHandle(), &left, &top, &right, &bottom);
            auto monitors = glfwGetMonitors(&count);
            for (int i = 0; i < count; ++i) {
                int mx, my, mw, mh; glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
                if (x - left >= mx && y - top >= my && x + width + right <= mx + mw && y + height + bottom <= my + mh) return true;
            }
            return false;
        };
        window.ApplyConfig(config, true); glfwPollEvents(); EXPECT_TRUE(fits(true));
        config.maximized = true; window.ApplyConfig(config, true); glfwPollEvents(); EXPECT_TRUE(fits(false));
    }
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
    TEST_F(WindowChromeTest, ViewOnlyCommandsAndUndoRequestAutosave)
    {
        frame(); GImGui->SettingsDirtyTimer = 0;
        ASSERT_TRUE(workspace->Execute({WindowAction::Pin, a})); EXPECT_GT(GImGui->SettingsDirtyTimer, 0);
        GImGui->SettingsDirtyTimer = 0; ASSERT_TRUE(undo->Undo()); EXPECT_GT(GImGui->SettingsDirtyTimer, 0);
        GImGui->SettingsDirtyTimer = 0;
        ASSERT_TRUE(workspace->Execute({WindowAction::Close, a})); EXPECT_GT(GImGui->SettingsDirtyTimer, 0);
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
