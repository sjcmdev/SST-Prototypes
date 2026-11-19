#include "Core/dspch.hpp"
#include "Presentation/Panels/WindowWorkspace.hpp"
#include <imgui.h>
#include "IconsFontAwesome6.h"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

namespace SST
{
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
        const bool titleHovered = title.Contains(ImGui::GetMousePos()) &&
            (ImGui::GetHoveredID() == window->TabId || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows));
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
        ImGui::InputText("Workspace name", m_WorkspaceName.data(), m_WorkspaceName.size());
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
            ImGui::InputText("Short name", m_ShortName.data(), m_ShortName.size());
            ImGui::InputText("Long name", m_LongName.data(), m_LongName.size());
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
            m_CapturingIni = true;
            const std::string ini = ImGui::SaveIniSettingsToMemory();
            m_CapturingIni = false;
            m_Workspaces[request.shortName] = {Save(), ini}; return {};
        }
        auto it = m_Workspaces.find(request.shortName);
        if (it == m_Workspaces.end()) return StructuredError{ErrorCategory::Validation, Severity::Error,
            "Workspace does not exist.", {}, {}, "WindowWorkspace"};
        auto result = Load(it->second.first);
        if (result) ImGui::LoadIniSettingsFromMemory(it->second.second.c_str());
        return result;
    }
    std::string WindowWorkspace::SettingsPayload() const
    {
        if (!m_FrozenSettings.empty()) return m_FrozenSettings;
        return nlohmann::json{{"state", Save()}, {"workspaces", m_Workspaces}}.dump();
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
                auto named = data.at("workspaces").get<decltype(workspace.m_Workspaces)>();
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
    void WindowWorkspace::RemoveSettingsHandler()
    {
        if (ImGui::GetCurrentContext()) ImGui::RemoveSettingsHandler("SSTWindows");
    }
}
