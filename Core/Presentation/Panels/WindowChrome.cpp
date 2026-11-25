#include "Core/dspch.hpp"
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Presentation/Panels/HostedEditorWindow.hpp"
#include <imgui.h>
#include "IconsFontAwesome6.h"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

namespace SST
{
    // Mirrors the native ImGui settings objects, without serializing a workspace switch.
    struct ImGuiLayout {
        struct Window { std::string name; ImGuiWindowSettings settings; };
        struct Table { ImGuiTableSettings settings; std::vector<ImGuiTableColumnSettings> columns; };
        struct Dock {
            ImGuiID id, parent, selected, parentWindow;
            ImGuiDockNodeFlags flags;
            ImGuiAxis split;
            ImVec2ih pos, size, sizeRef;
        };
        std::vector<Window> windows;
        std::vector<Table> tables;
        std::vector<Dock> docks;
        ImGuiConfigFlags flags = 0;
        bool tableHandler = false;
    };
    namespace {
        Ref<ImGuiLayout> CaptureImGuiLayout() {
            auto &g = *GImGui;
            // ImGui normally synchronizes these native objects just before writing INI.
            for (auto *w : g.Windows) if (!(w->Flags & ImGuiWindowFlags_NoSavedSettings)) {
                auto *s = ImGui::FindWindowSettingsByWindow(w);
                if (!s) { s = ImGui::CreateNewWindowSettings(w->Name); w->SettingsOffset = g.SettingsWindows.offset_from_ptr(s); }
                s->Pos = ImVec2ih(ImVec2(w->Pos.x - w->ViewportPos.x, w->Pos.y - w->ViewportPos.y)); s->Size = ImVec2ih(w->SizeFull);
                s->ViewportId = w->ViewportId; s->ViewportPos = ImVec2ih(w->ViewportPos);
                s->DockId = w->DockId; s->ClassId = w->WindowClass.ClassId; s->DockOrder = w->DockOrder;
                s->Collapsed = w->Collapsed; s->IsChild = w->RootWindow != w; s->WantDelete = false;
            }
            auto layout = CreateRef<ImGuiLayout>(); layout->flags = g.IO.ConfigFlags;
            layout->tableHandler = ImGui::FindSettingsHandler("Table") != nullptr;
            for (auto *s = g.SettingsWindows.begin(); s; s = g.SettingsWindows.next_chunk(s))
                if (!s->WantDelete) layout->windows.push_back({s->GetName(), *s});
            for (auto *s = g.SettingsTables.begin(); s; s = g.SettingsTables.next_chunk(s))
                if (s->ID) layout->tables.push_back({*s, {s->GetColumnSettings(), s->GetColumnSettings() + s->ColumnsCount}});
            auto visit = [&](auto &&self, ImGuiDockNode *node) -> void {
                layout->docks.push_back({node->ID, node->ParentNode ? node->ParentNode->ID : 0, node->SelectedTabId,
                    node->IsDockSpace() && node->HostWindow && node->HostWindow->ParentWindow ? node->HostWindow->ParentWindow->ID : 0,
                    node->LocalFlags & ImGuiDockNodeFlags_SavedFlagsMask_, node->IsSplitNode() ? node->SplitAxis : ImGuiAxis_None,
                    ImVec2ih(node->Pos), ImVec2ih(node->Size), ImVec2ih(node->SizeRef)});
                for (auto *child : node->ChildNodes) if (child) self(self, child);
            };
            for (const auto &pair : g.DockContext.Nodes.Data)
                if (auto *node = static_cast<ImGuiDockNode *>(pair.val_p); node && node->IsRootNode()) visit(visit, node);
            return layout;
        }
        void RestoreImGuiLayout(const ImGuiLayout &layout) {
            auto &g = *GImGui;
            ImGui::DockContextClearNodes(&g, 0, true);
            for (const auto &w : layout.windows) {
                auto *s = ImGui::FindWindowSettingsByID(w.settings.ID);
                if (!s) s = ImGui::CreateNewWindowSettings(w.name.c_str());
                *s = w.settings; s->WantApply = true;
            }
            if (layout.tableHandler && !ImGui::FindSettingsHandler("Table")) ImGui::TableSettingsAddSettingsHandler();
            for (const auto &t : layout.tables) {
                auto *s = ImGui::TableSettingsFindByID(t.settings.ID);
                if (s && s->ColumnsCountMax < t.settings.ColumnsCount) { s->ID = 0; s = nullptr; }
                if (!s) s = ImGui::TableSettingsCreate(t.settings.ID, t.settings.ColumnsCount);
                const auto capacity = s->ColumnsCountMax;
                *s = t.settings; s->ColumnsCountMax = capacity; s->WantApply = true;
                std::copy(t.columns.begin(), t.columns.end(), s->GetColumnSettings());
            }
            for (const auto &d : layout.docks) {
                ImGui::DockBuilderAddNode(d.id);
                ImGui::DockBuilderGetNode(d.id)->SetLocalFlags(d.flags);
            }
            for (const auto &d : layout.docks) {
                auto *node = ImGui::DockBuilderGetNode(d.id);
                node->ParentNode = d.parent ? ImGui::DockBuilderGetNode(d.parent) : nullptr;
                if (node->ParentNode) node->ParentNode->ChildNodes[node->ParentNode->ChildNodes[0] ? 1 : 0] = node;
                node->Pos = ImVec2(d.pos.x, d.pos.y); node->Size = ImVec2(d.size.x, d.size.y); node->SizeRef = ImVec2(d.sizeRef.x, d.sizeRef.y);
                node->AuthorityForPos = node->AuthorityForSize = node->AuthorityForViewport = ImGuiDataAuthority_DockNode;
                node->SelectedTabId = d.selected; node->SplitAxis = d.split;
                auto *root = ImGui::DockNodeGetRootNode(node);
                char host[32]; ImFormatString(host, sizeof(host), "##DockNode_%02X", root->ID);
                node->HostWindow = ImGui::FindWindowByName(host);
            }
            if (auto *handler = ImGui::FindSettingsHandler("Window")) handler->ApplyAllFn(&g, handler);
            if (auto *handler = ImGui::FindSettingsHandler("Table")) handler->ApplyAllFn(&g, handler);
            for (const auto &d : layout.docks) if (!d.parent) ImGui::DockBuilderFinish(d.id);
        }
        struct IniContext {
            ImGuiContext *previous = ImGui::GetCurrentContext(), *temporary;
            IniContext(ImGuiConfigFlags flags) {
                temporary = ImGui::CreateContext(previous ? previous->IO.Fonts : nullptr);
                ImGui::SetCurrentContext(temporary); temporary->IO.IniFilename = nullptr; temporary->IO.ConfigFlags = flags;
                temporary->SettingsLoaded = true;
            }
            ~IniContext() { ImGui::DestroyContext(temporary); ImGui::SetCurrentContext(previous); }
        };
        std::string EncodeImGuiLayout(const ImGuiLayout &layout) {
            IniContext context(layout.flags);
            RestoreImGuiLayout(layout);
            std::string ini = ImGui::SaveIniSettingsToMemory();
            // A temporary IO context has no live DockSpace host window.
            for (const auto &dock : layout.docks) if (!dock.parent && dock.parentWindow) {
                char id[32], parent[32];
                ImFormatString(id, sizeof(id), " ID=0x%08X", dock.id);
                ImFormatString(parent, sizeof(parent), " Window=0x%08X", dock.parentWindow);
                if (auto pos = ini.find(id); pos != std::string::npos) ini.insert(pos + std::strlen(id), parent);
            }
            return ini;
        }
        Ref<const ImGuiLayout> DecodeImGuiLayout(const std::string &ini) {
            IniContext context(ImGui::GetIO().ConfigFlags);
            if (!ImGui::FindSettingsHandler("Table")) ImGui::TableSettingsAddSettingsHandler();
            ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
            auto layout = CaptureImGuiLayout();
            // ImGui consumes this file field when rebuilding nodes but does not retain it on a node.
            for (auto &dock : layout->docks) if (!dock.parent) {
                char id[32]; ImFormatString(id, sizeof(id), " ID=0x%08X Window=", dock.id);
                if (auto pos = ini.find(id); pos != std::string::npos)
                    std::sscanf(ini.c_str() + pos + std::strlen(id), "0x%X", &dock.parentWindow);
            }
            return layout;
        }
    }

    namespace
    {
        bool chromeButton(ImGuiWindow *window, const char *label, const ImRect &rect, bool enabled, const char *tooltip)
        {
            bool hovered = false, held = false;
            const auto id = window->GetID(label);
            ImGui::PushClipRect(rect.Min, rect.Max, false);
            const bool pressed = ImGui::ItemAdd(rect, id) && enabled && ImGui::ButtonBehavior(rect, id, &hovered, &held);
            auto *draw = window->DrawList;
            draw->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(hovered ?
                (held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered) : ImGuiCol_TitleBgActive));
            const auto size = ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(rect.Min.x + (rect.GetWidth() - size.x) / 2, rect.Min.y + (rect.GetHeight() - size.y) / 2),
                ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
            ImGui::PopClipRect();
            if (hovered) ImGui::SetTooltip("%s", tooltip);
            return pressed;
        }
    }

    void EditorWindow::Request(WindowRequest request)
    {
        request.id = m_Id;
        if (auto workspace = m_Workspace.lock()) (void)workspace->Execute(std::move(request));
    }
    void EditorWindow::Render()
    {
        auto workspace = m_Workspace.lock();
        if (m_Capabilities.auxiliary && ImGui::GetCurrentContext())
        {
            ImGui::ClearWindowSettings(ImGuiName().c_str());
            ImGui::ClearWindowSettings(GetTitle().c_str());
        }
        if (!IsVisible() || m_State.mode == WindowMode::Minimized) { OnContentSkipped(); return; }
        auto *viewport = ImGui::GetMainViewport();
        const bool maximized = m_State.mode == WindowMode::Maximized;
        const bool locked = workspace && workspace->IsLocked();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | WindowFlags();
        if (!(flags & ImGuiWindowFlags_NoScrollbar)) flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
        if (m_Capabilities.auxiliary) flags |= ImGuiWindowFlags_NoSavedSettings;
        if (m_State.pinned || locked || maximized) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        if (maximized || ((m_State.pinned || locked) && m_State.dockId == 0)) flags |= ImGuiWindowFlags_NoDocking;
        if (!m_Capabilities.dockable) flags |= ImGuiWindowFlags_NoDocking;
        if (IsDirty()) flags |= ImGuiWindowFlags_UnsavedDocument;
        ImGuiWindowClass windowClass;
        if (locked || m_State.pinned)
            windowClass.DockNodeFlagsOverrideSet = int(ImGuiDockNodeFlags_NoDocking) | int(ImGuiDockNodeFlags_NoResize) | int(ImGuiDockNodeFlags_NoUndocking);
        ImGui::SetNextWindowClass(&windowClass);
        if (maximized)
        {
            // Keep the dock node and its split ratios while this view temporarily floats.
            if (auto *native = ImGui::FindWindowByName(ImGuiName().c_str()); native && native->DockNode)
                ImGui::DockContextProcessUndockWindow(GImGui, native, false);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, std::max(1.0f, viewport->WorkSize.y - ImGui::GetFrameHeightWithSpacing())));
        }
        else if (m_ApplyGeometry)
        {
            ImGui::SetNextWindowDockID(m_State.dockId, ImGuiCond_Always);
            if (m_State.dockId == 0)
            {
                ImGui::SetNextWindowPos(ImVec2(m_State.x, m_State.y));
                ImGui::SetNextWindowSize(ImVec2(m_State.width, m_State.height));
            }
        }
        else if (!(GImGui->NextWindowData.HasFlags & ImGuiNextWindowDataFlags_HasSize))
            ImGui::SetNextWindowSize(ImVec2(m_State.width, m_State.height), ImGuiCond_FirstUseEver);
        m_ApplyGeometry = false;
        const auto name = ImGuiName();
        if (!m_Capabilities.auxiliary && !ImGui::FindWindowByName(name.c_str()) && !ImGui::FindWindowSettingsByID(ImHashStr(name.c_str())))
            if (const auto *legacy = ImGui::FindWindowSettingsByID(ImHashStr(GetTitle().c_str())))
            {
                const auto saved = *legacy; // Creating settings may reallocate ImGui's settings storage.
                auto *settings = ImGui::CreateNewWindowSettings(name.c_str());
                const auto id = settings->ID;
                *settings = saved; settings->ID = id; settings->WantApply = true;
            }
        const bool content = ImGui::Begin(name.c_str(), nullptr, flags);
        auto *window = ImGui::GetCurrentWindow();
        if (workspace && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) workspace->SetActive(m_Id);
        if (!maximized && !m_State.pinned && !m_ApplyGeometry)
        {
            m_State.x = window->Pos.x; m_State.y = window->Pos.y;
            m_State.width = window->SizeFull.x; m_State.height = window->SizeFull.y;
            m_State.dockId = window->DockId;
        }
        const ImRect title = window->DockNode ? window->DC.DockTabItemRect : window->TitleBarRect();
        const bool dockTabHovered = window->DockNode && window->DockNode->HostWindow &&
            GImGui->HoveredWindow == window->DockNode->HostWindow &&
            ImGui::IsWindowContentHoverable(window->DockNode->HostWindow) &&
            (GImGui->ActiveId == 0 || GImGui->ActiveId == window->TabId);
        const bool titleHovered = title.Contains(ImGui::GetMousePos()) &&
            (dockTabHovered || ImGui::GetHoveredID() == window->TabId || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows));
        // Overlapping tabs otherwise require a hover frame before a fast move-and-click.
        if (dockTabHovered && titleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            if (auto *bar = window->DockNode->TabBar)
                if (auto *tab = ImGui::TabBarFindTabByID(bar, window->TabId)) {
                    ImGui::TabBarQueueFocus(bar, tab);
                    ImGui::FocusWindow(window);
                }
        // Action buttons must not also trigger the title bar's double-click gesture.
        const bool titleGestureHovered = titleHovered && (window->DockNode ||
            ImGui::GetMousePos().x < title.Max.x - 4 * title.GetHeight());
        if (titleGestureHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && m_Capabilities.maximizable)
            Request({WindowAction::Maximize});
        if (window->DockNode && titleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && m_Capabilities.closable)
            Request({WindowAction::Close});

        if (workspace)
        {
            const auto lastItem = GImGui->LastItemData;
            const bool skipped = window->SkipItems;
            window->SkipItems = false; // Dock tabs remain actionable when their content is inactive.
            ImGui::SetLastItemData(window->TabId ? window->TabId : window->ID, ImGuiItemFlags_None,
                titleHovered ? ImGuiItemStatusFlags_HoveredRect | ImGuiItemStatusFlags_HoveredWindow : 0, title);
            if (ImGui::BeginPopupContextItem("##sst-window-context"))
            {
                workspace->RenderContextMenu(*this);
                RenderContextMenu();
                ImGui::EndPopup();
            }
            window->SkipItems = skipped;
            GImGui->LastItemData = lastItem;
        }
        if (content)
        {
            if (window->DockNode && ImGui::DockNodeBeginAmendTabBar(window->DockNode))
            {
                ImGui::PushID("sst-window-actions");
                auto *tabBar = window->DockNode->TabBar;
                float trailingRight = 0.0f;
                for (const auto &tab : tabBar->Tabs)
                    if (tab.Flags & ImGuiTabItemFlags_Trailing)
                        trailingRight = std::max(trailingRight, tab.Offset + tab.Width);
                // ImGui packs trailing tabs beside the titles; keep window actions at the right edge.
                const float offset = tabBar->BarRect.GetWidth() - trailingRight;
                for (auto &tab : tabBar->Tabs)
                    if (tab.Flags & ImGuiTabItemFlags_Trailing) tab.Offset += offset;
                auto button = [&](const char *icon, const char *tooltip, WindowAction action, bool enabled) {
                    if (!enabled) return;
                    if (ImGui::TabItemButton(icon, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) Request({action});
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
                };
                button(ICON_FA_WINDOW_MINIMIZE, "Minimize", WindowAction::Minimize, m_Capabilities.minimizable);
                button(maximized ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE, maximized ? "Restore" : "Maximize",
                    WindowAction::Maximize, m_Capabilities.maximizable && !locked && !m_State.pinned);
                button(ICON_FA_THUMBTACK, m_State.pinned ? "Unpin" : "Pin", WindowAction::Pin, m_Capabilities.pinnable && !locked);
                button(ICON_FA_XMARK, "Close", WindowAction::Close, m_Capabilities.closable);
                ImGui::PopID();
                ImGui::DockNodeEndAmendTabBar();
            }
            else if (!window->DockNode)
            {
                auto bar = window->TitleBarRect();
                const float size = bar.GetHeight();
                float right = bar.Max.x;
                for (auto [label, action, enabled, tooltip] : {
                    std::tuple{ICON_FA_XMARK, WindowAction::Close, m_Capabilities.closable, "Close"},
                    std::tuple{ICON_FA_THUMBTACK, WindowAction::Pin, m_Capabilities.pinnable && !locked, m_State.pinned ? "Unpin" : "Pin"},
                    std::tuple{maximized ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE, WindowAction::Maximize, m_Capabilities.maximizable && !locked && !m_State.pinned, maximized ? "Restore" : "Maximize"},
                    std::tuple{ICON_FA_WINDOW_MINIMIZE, WindowAction::Minimize, m_Capabilities.minimizable, "Minimize"}})
                {
                    const ImRect rect(ImVec2(right - size, bar.Min.y), ImVec2(right, bar.Max.y));
                    if (chromeButton(window, label, rect, enabled, tooltip)) Request({action});
                    right -= size;
                }
            }
            RenderContent();
        }
        else OnContentSkipped();
        ImGui::End();
        if (maximized) window->DockId = m_State.dockId; // Preserve the native INI reference as well as Core state.
    }

    void WindowWorkspace::RenderContextMenu(EditorWindow &window)
    {
        const auto id = window.GetId();
        const auto state = window.GetState();
        const auto &cap = window.Capabilities();
        auto item = [&](const char *label, WindowAction action, bool enabled = true, bool selected = false) {
            if (ImGui::MenuItem(label, nullptr, selected, enabled)) (void)Execute({action, id});
        };
        ImGui::TextDisabled("%s", state.longName.c_str());
        ImGui::Separator();
        item("Minimize", WindowAction::Minimize, cap.minimizable);
        item(state.mode == WindowMode::Maximized ? "Restore" : "Maximize", WindowAction::Maximize, cap.maximizable && !m_Locked && !state.pinned);
        item("Pin", WindowAction::Pin, cap.pinnable && !m_Locked, state.pinned);
        if (ImGui::MenuItem("Rename...", nullptr, false, cap.renamable))
        {
            m_RenameId = id;
            std::snprintf(m_ShortName.data(), m_ShortName.size(), "%s", state.shortName.c_str());
            std::snprintf(m_LongName.data(), m_LongName.size(), "%s", state.longName.c_str());
        }
        item("Duplicate", WindowAction::Duplicate, cap.duplicable);
        item("Float", WindowAction::Float, cap.dockable && !m_Locked && !state.pinned);
        if (ImGui::BeginMenu("Dock to...", cap.dockable && !m_Locked && !state.pinned))
        {
            const auto *viewport = ImGui::GetMainViewport();
            if (ImGui::MenuItem("Application")) { WindowRequest r{WindowAction::Dock, id}; r.dockId = viewport->ID; (void)Execute(r); }
            for (auto &target : Windows()) if (target->GetId() != id && target->IsVisible())
            {
                auto *targetWindow = ImGui::FindWindowByName(target->ImGuiName().c_str());
                if (!targetWindow || !targetWindow->DockNode) continue;
                ImGui::PushID(static_cast<int>(target->GetId()));
                if (ImGui::MenuItem(target->GetTitle().c_str()))
                { WindowRequest r{WindowAction::Dock, id}; r.dockId = targetWindow->DockId; (void)Execute(r); }
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        item("Show in Project Explorer", WindowAction::Reveal, cap.projectItem);
        ImGui::Separator();
        item("Close", WindowAction::Close, cap.closable);
        item("Close others", WindowAction::CloseOthers);
        item("Close all of this type", WindowAction::CloseType);
    }

    void WindowWorkspace::RenderMenu()
    {
        if (!ImGui::BeginMenu("Window")) return;
        for (auto &window : Windows())
        {
            if (window->GetTitle().empty()) continue;
            if (auto hosted = std::dynamic_pointer_cast<HostedEditorWindow>(window); hosted && !hosted->claimed) continue;
            ImGui::PushID(static_cast<int>(window->GetId()));
            auto state = window->GetState();
            const auto label = state.shortName + (window->IsDirty() ? " *" : "") +
                (state.mode == WindowMode::Minimized ? " (minimized)" : "");
            if (ImGui::MenuItem(label.c_str(), nullptr, window->IsVisible())) (void)Execute({WindowAction::Activate, window->GetId()});
            ImGui::PopID();
        }
        ImGui::Separator();
        for (auto [label, action] : {std::pair{"Cascade", WindowAction::Cascade},
            std::pair{"Tile horizontally", WindowAction::TileHorizontal}, std::pair{"Tile vertically", WindowAction::TileVertical},
            std::pair{"Show minimized", WindowAction::ShowMinimized}})
            if (ImGui::MenuItem(label)) (void)Execute({action});
        if (ImGui::MenuItem("Reset layout")) (void)Execute({WindowAction::ResetLayout});
        if (ImGui::MenuItem("Lock layout", nullptr, m_Locked)) (void)Execute({WindowAction::LockLayout});
        ImGui::Separator();
        ImGui::TextUnformatted("Workspace name"); ImGui::SameLine();
        ImGui::InputText("##workspace-name", m_WorkspaceName.data(), m_WorkspaceName.size());
        if (ImGui::MenuItem("Save workspace", nullptr, false, m_WorkspaceName[0] != '\0'))
        { WindowRequest r{WindowAction::SaveWorkspace}; r.shortName = m_WorkspaceName.data(); (void)Execute(r); }
        for (const auto &[name, data] : m_Workspaces)
            if (ImGui::MenuItem(name.c_str())) { WindowRequest r{WindowAction::LoadWorkspace}; r.shortName = name; (void)Execute(r); }
        ImGui::EndMenu();
    }

    void WindowWorkspace::RenderOverlays()
    {
        const auto windows = Windows();
        const auto *viewport = ImGui::GetMainViewport();
        SetAvailableArea(viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x,
            viewport->WorkSize.y - ImGui::GetFrameHeightWithSpacing());
        for (auto &window : windows)
        {
            window->m_Workspace = weak_from_this();
            if (auto *gui = ImGui::FindWindowByName(window->ImGuiName().c_str()))
            {
                if (window->GetState().pinned && window->IsVisible() && !gui->DockNode && GImGui->OpenPopupStack.empty())
                    ImGui::BringWindowToDisplayFront(gui);
                if (window->GetId() == m_Active && m_FocusRequested)
                { ImGui::FocusWindow(gui); m_FocusRequested = false; }
            }
        }
        // shortcut: one-row minimized strip; add scrolling when apps routinely minimize many views.
        if (std::any_of(windows.begin(), windows.end(), [](const auto &w) { return w->IsVisible() && w->GetState().mode == WindowMode::Minimized; }))
        {
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - ImGui::GetFrameHeightWithSpacing()));
            ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, ImGui::GetFrameHeightWithSpacing()));
            ImGui::Begin("Minimized windows###sst-minimized", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
            for (auto &window : windows) if (window->IsVisible() && window->GetState().mode == WindowMode::Minimized)
            {
                ImGui::PushID(static_cast<int>(window->GetId()));
                if (ImGui::SmallButton(window->GetTitle().c_str())) (void)Execute({WindowAction::Activate, window->GetId()});
                ImGui::PopID(); ImGui::SameLine();
            }
            ImGui::End();
        }
        if (m_RenameId) { ImGui::OpenPopup("Rename window"); m_RenameOpenId = std::exchange(m_RenameId, 0); }
        if (ImGui::BeginPopupModal("Rename window", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Short name"); ImGui::SameLine(); ImGui::InputText("##short-name", m_ShortName.data(), m_ShortName.size());
            ImGui::TextUnformatted("Long name"); ImGui::SameLine(); ImGui::InputText("##long-name", m_LongName.data(), m_LongName.size());
            if (ImGui::Button("Apply"))
            {
                WindowRequest r{WindowAction::Rename, m_RenameOpenId}; r.shortName = m_ShortName.data(); r.longName = m_LongName.data();
                if (Execute(r)) ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            if (!m_Error.empty()) ImGui::TextWrapped("%s", m_Error.c_str());
            ImGui::EndPopup();
        }
        if (m_PendingClose) ImGui::OpenPopup("Unsaved window data");
        if (ImGui::BeginPopupModal("Unsaved window data", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("These windows contain unsaved data. Close them?");
            for (auto &window : windows) if (window->IsDirty()) ImGui::BulletText("%s", window->GetTitle().c_str());
            ImGui::TextUnformatted("Closing hides the view. Save document data through the application.");
            if (ImGui::Button("Close windows") && m_PendingClose)
            { auto r = *m_PendingClose; m_PendingClose.reset(); r.confirmed = true; (void)Execute(r); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) { m_PendingClose.reset(); m_Error.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

    Result<void> WindowWorkspace::WorkspaceOperation(const WindowRequest &request)
    {
        if (!ImGui::GetCurrentContext()) return StructuredError{ErrorCategory::Validation, Severity::Error,
            "Workspace operations require ImGui.", {}, {}, "WindowWorkspace"};
        if (request.action == WindowAction::SaveWorkspace)
        {
            if (request.shortName.empty() || request.shortName.size() >= 256) return StructuredError{ErrorCategory::Validation,
                Severity::Error, "Invalid workspace name.", {}, {}, "WindowWorkspace"};
            m_Workspaces[request.shortName] = {CaptureLayout(), CaptureImGuiLayout()}; return {};
        }
        auto it = m_Workspaces.find(request.shortName);
        if (it == m_Workspaces.end()) return StructuredError{ErrorCategory::Validation, Severity::Error,
            "Workspace does not exist.", {}, {}, "WindowWorkspace"};
        auto result = LoadLayout(it->second.first);
        if (result) RestoreImGuiLayout(*it->second.second);
        return result;
    }
    std::string WindowWorkspace::SettingsPayload() const
    {
        if (!m_FrozenSettings.empty()) return m_FrozenSettings;
        nlohmann::json named = nlohmann::json::object();
        for (const auto &[name, saved] : m_Workspaces) named[name] = {SaveLayout(saved.first), EncodeImGuiLayout(*saved.second)};
        return nlohmann::json{{"state", Save()}, {"workspaces", std::move(named)}}.dump();
    }
    void WindowWorkspace::FreezeSettings() { m_FrozenSettings = SettingsPayload(); }
    void WindowWorkspace::InstallSettingsHandler()
    {
        if (!ImGui::GetCurrentContext() || ImGui::FindSettingsHandler("SSTWindows")) return;
        ImGuiSettingsHandler handler;
        handler.TypeName = "SSTWindows"; handler.TypeHash = ImHashStr(handler.TypeName); handler.UserData = this;
        handler.ClearAllFn = [](ImGuiContext *, ImGuiSettingsHandler *h) {
            auto &workspace = *static_cast<WindowWorkspace *>(h->UserData);
            workspace.ResetWindowStates();
        };
        handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *h, const char *name) -> void * {
            return std::strcmp(name, "Workspace") == 0 ? h->UserData : nullptr;
        };
        handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *entry, const char *line) {
            if (std::strncmp(line, "Data=", 5) == 0) static_cast<WindowWorkspace *>(entry)->m_PendingSettings = line + 5;
        };
        handler.ApplyAllFn = [](ImGuiContext *, ImGuiSettingsHandler *h) {
            auto &workspace = *static_cast<WindowWorkspace *>(h->UserData);
            if (workspace.m_PendingSettings.empty()) return;
            try
            {
                const auto data = nlohmann::json::parse(std::exchange(workspace.m_PendingSettings, {}));
                decltype(workspace.m_Workspaces) named;
                for (const auto &[name, saved] : data.at("workspaces").items()) {
                    auto layout = workspace.ParseLayout(saved.at(0).get<std::string>());
                    if (!layout) throw std::runtime_error(layout.Error().userMessage);
                    named[name] = {std::move(*layout), DecodeImGuiLayout(saved.at(1).get<std::string>())};
                }
                const auto result = workspace.Load(data.at("state").get<std::string>());
                if (result) workspace.m_Workspaces = std::move(named);
                else workspace.m_Error = result.Error().userMessage;
            }
            catch (const std::exception &e) { workspace.m_Error = e.what(); }
        };
        handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *h, ImGuiTextBuffer *out) {
            auto &workspace = *static_cast<WindowWorkspace *>(h->UserData);
            if (!workspace.m_CapturingIni) out->appendf("[SSTWindows][Workspace]\nData=%s\n\n", workspace.SettingsPayload().c_str());
        };
        ImGui::AddSettingsHandler(&handler);
        // Core Input owns window switching; avoid a second ImGui Ctrl+Tab handler.
        GImGui->ConfigNavWindowingKeyNext = GImGui->ConfigNavWindowingKeyPrev = 0;
        // A startup layout may have arrived before app windows were registered.
        const std::string ini = GImGui->SettingsIniData.c_str();
        if (!ini.empty()) ImGui::LoadIniSettingsFromMemory(ini.c_str());
    }
    void WindowWorkspace::RestoreDefaultLayoutIfNeeded(std::string_view typePrefix)
    {
        if (m_DefaultLayoutChecked || !m_DefaultLayout || !ImGui::GetCurrentContext()) return;
        bool found = false, hasLayout = false;
        for (const auto& window : Windows()) {
            if (window->Capabilities().auxiliary || !window->GetType().starts_with(typePrefix)) continue;
            found = true;
            const auto* saved = ImGui::FindWindowSettingsByID(ImHashStr(window->ImGuiName().c_str()));
            hasLayout |= saved && !saved->WantDelete;
        }
        if (!found) return; // Hosts may register their windows after the first frame.
        m_DefaultLayoutChecked = true;
        // Floating, hidden and maximized windows are valid saved user layouts.
        if (!hasLayout) (void)Execute({WindowAction::ResetLayout});
    }
    void WindowWorkspace::RemoveSettingsHandler()
    {
        if (ImGui::GetCurrentContext()) ImGui::RemoveSettingsHandler("SSTWindows");
    }
}
