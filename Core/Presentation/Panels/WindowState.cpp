#include "Core/dspch.hpp"
#include "Presentation/Panels/WindowWorkspace.hpp"
#include "Presentation/Panels/HostedEditorWindow.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "IconsFontAwesome6.h"
#include <nlohmann/json.hpp>
#include <imgui_internal.h>
#include <cmath>
#include <limits>

namespace SST
{
    namespace
    {
        Result<void> invalid(const std::string &message)
        {
            return StructuredError{ErrorCategory::Validation, Severity::Error, message,
                message, "Check the window capabilities or layout data.", "WindowWorkspace", "window.invalid"};
        }
        bool validName(const std::string &name)
        {
            return !name.empty() && name.size() < 256 && name.find('#') == std::string::npos &&
                std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; });
        }
        nlohmann::json encode(const WindowState &s)
        {
            return {{"short", s.shortName}, {"long", s.longName}, {"mode", int(s.mode)},
                {"restore", int(s.restoreMode)}, {"visible", s.visible}, {"pinned", s.pinned},
                {"x", s.x}, {"y", s.y}, {"width", s.width}, {"height", s.height}, {"dock", s.dockId}};
        }
        WindowState decode(const nlohmann::json &j)
        {
            WindowState s;
            s.shortName = j.at("short").get<std::string>(); s.longName = j.at("long").get<std::string>();
            if (!j.at("mode").is_number_integer() || !j.at("restore").is_number_integer() || !j.at("dock").is_number_integer())
                throw std::runtime_error("Window modes and dock IDs must be integers");
            const auto mode = j.at("mode").get<std::int64_t>(), restore = j.at("restore").get<std::int64_t>();
            if (mode < 0 || mode > 2 || restore < 0 || restore > 2 || restore == 1)
                throw std::runtime_error("Invalid window mode");
            s.mode = WindowMode(mode); s.restoreMode = WindowMode(restore);
            s.visible = j.at("visible").get<bool>(); s.pinned = j.at("pinned").get<bool>();
            s.x = j.at("x").get<float>(); s.y = j.at("y").get<float>();
            s.width = j.at("width").get<float>(); s.height = j.at("height").get<float>();
            const auto dock = j.at("dock").get<std::int64_t>();
            if (dock < 0 || dock > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Invalid dock ID");
            s.dockId = std::uint32_t(dock);
            if (!validName(s.shortName) || !validName(s.longName) ||
                !std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.width) || !std::isfinite(s.height) ||
                std::abs(s.x) > 100000 || std::abs(s.y) > 100000 || s.width < 1 || s.height < 1 ||
                s.width > 100000 || s.height > 100000) throw std::runtime_error("Invalid window state");
            return s;
        }
        class WindowCommand final : public ICommand
        {
        public:
            WindowCommand(WeakRef<WindowWorkspace> workspace, WindowRequest request)
                : m_Workspace(std::move(workspace)), m_Request(std::move(request)) {}
            Result<void> Execute(CommandContext &) override
            {
                auto workspace = m_Workspace.lock();
                if (!workspace) return invalid("EditorWindow workspace is no longer available.");
                m_Before = workspace->Capture();
                auto result = workspace->Apply(m_Request, &m_ContentCommand);
                if (result) m_After = workspace->Capture();
                return result;
            }
            Result<void> Undo(CommandContext &context) override
            {
                if (m_Workspace.expired()) return invalid("Window workspace is no longer available.");
                if (m_ContentCommand) { auto result = m_ContentCommand->Undo(context); if (!result) return result; }
                return restore(m_Before);
            }
            Result<void> Redo(CommandContext &context) override
            {
                if (m_Workspace.expired()) return invalid("Window workspace is no longer available.");
                if (m_ContentCommand) { auto result = m_ContentCommand->Redo(context); if (!result) return result; }
                return restore(m_After);
            }
            std::string Description() const override { return "EditorWindow action"; }
            bool IsUndoable() const noexcept override
            {
                return m_Request.action != WindowAction::Reveal && m_Request.action != WindowAction::SaveWorkspace &&
                    m_Request.action != WindowAction::LoadWorkspace && m_Request.action != WindowAction::ResetLayout && m_Request.action != WindowAction::Next &&
                    m_Request.action != WindowAction::Previous && m_Request.action != WindowAction::Activate;
            }
        private:
            Result<void> restore(const WindowSnapshot &snapshot)
            {
                auto workspace = m_Workspace.lock();
                if (!workspace) return invalid("EditorWindow workspace is no longer available.");
                workspace->Restore(snapshot); return {};
            }
            WeakRef<WindowWorkspace> m_Workspace;
            WindowRequest m_Request;
            WindowSnapshot m_Before, m_After;
            Unique<ICommand> m_ContentCommand;
        };
    }

    EditorWindow::EditorWindow(std::string type, std::string shortName, bool visible, WindowCapabilities capabilities)
        : IPanel(shortName, visible && !capabilities.auxiliary), m_Type(std::move(type)), m_Capabilities(capabilities)
    {
        m_State.shortName = m_State.longName = std::move(shortName);
        m_State.visible = IsVisible();
    }
    EditorWindow::EditorWindow(const EditorWindow &other)
        : IPanel(other.GetTitle(), other.IsVisible()), m_Type(other.m_Type),
          m_Capabilities(other.m_Capabilities), m_State(other.GetState()) {}
    WindowState EditorWindow::GetState() const
    {
        auto state = m_State;
        state.shortName = GetTitle(); state.visible = IsVisible();
        return state;
    }
    void EditorWindow::ApplyState(const WindowState &state)
    {
        m_State = state; SetTitle(state.shortName); SetVisible(state.visible); m_ApplyGeometry = true;
    }
    std::string EditorWindow::ImGuiName() const
    {
        return std::string(ICON_FA_WINDOW_MAXIMIZE " ") + GetTitle() + (IsDirty() ? " *" : "") + "###sst-window-" + std::to_string(m_Id);
    }

    std::vector<Ref<EditorWindow>> WindowWorkspace::Windows() const
    {
        std::vector<Ref<EditorWindow>> windows;
        for (const auto &entry : m_Panels.Entries())
            if (auto window = std::dynamic_pointer_cast<EditorWindow>(entry.panel)) windows.push_back(std::move(window));
        return windows;
    }
    Ref<EditorWindow> WindowWorkspace::Find(WindowId id) const
    {
        return std::dynamic_pointer_cast<EditorWindow>(m_Panels.Get(id).lock());
    }
    WindowSnapshot WindowWorkspace::Capture() const
    {
        WindowSnapshot snapshot;
        snapshot.locked = m_Locked; snapshot.active = m_Active;
        for (const auto &entry : m_Panels.Entries())
            if (auto window = std::dynamic_pointer_cast<EditorWindow>(entry.panel)) snapshot.items.push_back({entry, window->GetState()});
        return snapshot;
    }
    void WindowWorkspace::Restore(const WindowSnapshot &snapshot)
    {
        for (const auto &window : Windows()) (void)m_Panels.Remove(window->GetId());
        for (const auto &item : snapshot.items)
        {
            (void)m_Panels.AddExisting(item.entry.panel, item.entry.id);
            auto window = std::static_pointer_cast<EditorWindow>(item.entry.panel);
            window->ApplyState(item.state); window->m_Workspace = weak_from_this();
        }
        m_Locked = snapshot.locked; m_Active = snapshot.active; m_FocusRequested = m_Active != 0;
        if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
    }
    Result<void> WindowWorkspace::Bind(Ref<CommandRegistry> commands, Ref<KeymapResolver> keymap)
    {
        if (auto previous = m_Commands.lock(); previous && m_CommandObserver) previous->RemoveObserver(m_CommandObserver);
        m_Commands = commands;
        auto weak = weak_from_this();
        m_CommandObserver = commands->AddObserver([weak](const CommandExecutionEvent &event) {
            auto workspace = weak.lock();
            if (!workspace || event.state != CommandExecutionState::Succeeded) return;
            for (auto &window : workspace->Windows())
            {
                const auto &cap = window->Capabilities();
                if (!cap.auxiliary) continue;
                if (cap.openCommand == event.id.value) (void)workspace->Apply({WindowAction::Activate, window->GetId()});
                if (cap.closeCommand == event.id.value) (void)workspace->Apply({WindowAction::Close, window->GetId()});
            }
        });
        for (const auto &[id, name, action] : {
            std::tuple{"window.action", "EditorWindow action", WindowAction::Activate},
            std::tuple{"window.next", "Next window", WindowAction::Next},
            std::tuple{"window.previous", "Previous window", WindowAction::Previous},
            std::tuple{"app.layout.reset", "Reset layout", WindowAction::ResetLayout}})
        {
            auto result = commands->Register({CommandID{id}, name, "EditorWindow", {}, {},
                    action == WindowAction::Activate ? CommandFlags::HiddenFromPalette : CommandFlags::None},
                [weak, action](CommandContext &context) {
                    auto request = context.TryGet<WindowRequest>("window.request");
                    return CreateUnique<WindowCommand>(weak, request ? *request : WindowRequest{action});
                });
            if (!result) return result;
        }
        if (keymap)
        {
            auto result = keymap->RegisterBinding({"window.next", {KeyCode::Tab, KeyModifiers::Ctrl}, CommandID{"window.next"}});
            if (!result) return result;
            return keymap->RegisterBinding({"window.previous", {KeyCode::Tab, KeyModifiers::Ctrl | KeyModifiers::Shift}, CommandID{"window.previous"}});
        }
        return {};
    }
    Result<void> WindowWorkspace::Execute(WindowRequest request)
    {
        auto commands = m_Commands.lock();
        if (!commands) return invalid("EditorWindow commands are unavailable.");
        CommandContext context; context.Set("window.request", request);
        auto result = commands->Execute(CommandID{request.action == WindowAction::ResetLayout ? "app.layout.reset" : "window.action"}, std::move(context));
        if (!result) { m_Error = result.Error().userMessage; return result.Error(); }
        m_Error.clear();
        if (ImGui::GetCurrentContext()) ImGui::MarkIniSettingsDirty();
        return {};
    }
    void WindowWorkspace::SetAvailableArea(float x, float y, float width, float height)
    {
        m_X = x; m_Y = y; m_Width = std::max(1.0f, width); m_Height = std::max(1.0f, height);
    }
    void WindowWorkspace::ResetWindowStates()
    {
        m_PendingSettings.clear(); m_FrozenSettings.clear();
        m_Locked = false; m_Active = 0; m_FocusRequested = false;
        for (auto &window : Windows())
        {
            auto state = window->m_DefaultState;
            if (!state.dockId)
            {
                state.width = std::clamp(state.width, 1.f, m_Width); state.height = std::clamp(state.height, 1.f, m_Height);
                state.x = std::clamp(state.x, m_X, m_X + m_Width - state.width);
                state.y = std::clamp(state.y, m_Y, m_Y + m_Height - state.height);
            }
            window->ApplyState(state);
        }
    }
    Result<void> WindowWorkspace::Apply(const WindowRequest &request, Unique<ICommand> *contentCommand)
    {
        const auto action = request.action;
        if (action == WindowAction::ResetLayout)
        {
            ResetWindowStates();
            if (ImGui::GetCurrentContext()) { ImGui::ClearIniSettings(); ImGui::MarkIniSettingsDirty(); }
            return {};
        }
        auto windows = Windows();
        auto window = Find(request.id ? request.id : m_Active);
        if (action == WindowAction::LockLayout) { m_Locked = !m_Locked; return {}; }
        if (action == WindowAction::ShowMinimized)
        {
            for (auto &w : windows) if (w->IsVisible() && w->GetState().mode == WindowMode::Minimized)
            { auto s = w->GetState(); s.mode = s.restoreMode; s.restoreMode = WindowMode::Normal; s.visible = true; w->ApplyState(s); }
            return {};
        }
        if (action == WindowAction::Next || action == WindowAction::Previous)
        {
            std::erase_if(windows, [](const auto &w) { return !w->IsVisible(); });
            if (windows.empty()) return {};
            auto it = std::find_if(windows.begin(), windows.end(), [this](const auto &w) { return w->GetId() == m_Active; });
            auto index = it == windows.end() ? 0 : std::size_t(it - windows.begin());
            index = (index + (action == WindowAction::Next ? 1 : windows.size() - 1)) % windows.size();
            window = windows[index];
        }
        if (action == WindowAction::Cascade || action == WindowAction::TileHorizontal || action == WindowAction::TileVertical)
        {
            if (m_Locked) return invalid("Layout is locked.");
            std::erase_if(windows, [](const auto &w) { return !w->IsVisible() || w->GetState().pinned || w->GetState().mode == WindowMode::Minimized; });
            for (std::size_t i = 0; i < windows.size(); ++i)
            {
                auto s = windows[i]->GetState(); s.dockId = 0; s.mode = WindowMode::Normal;
                const auto count = float(windows.size()), index = float(i);
                s.x = m_X; s.y = m_Y; s.width = m_Width; s.height = m_Height;
                if (action == WindowAction::TileHorizontal) { s.y += index * m_Height / count; s.height /= count; }
                else if (action == WindowAction::TileVertical) { s.x += index * m_Width / count; s.width /= count; }
                else { s.x += index * 24; s.y += index * 24; s.width *= .65f; s.height *= .65f; }
                windows[i]->ApplyState(s);
            }
            return {};
        }
        if (action == WindowAction::SaveWorkspace || action == WindowAction::LoadWorkspace)
            return WorkspaceOperation(request);
        if (!window) return invalid("EditorWindow does not exist.");
        const auto &cap = window->Capabilities();
        auto s = window->GetState();
        if (action == WindowAction::Close || action == WindowAction::CloseOthers || action == WindowAction::CloseType)
        {
            std::vector<Ref<EditorWindow>> closing;
            for (auto &w : windows)
                if (w->IsVisible() && ((action == WindowAction::Close && w == window) ||
                    (action == WindowAction::CloseOthers && w != window) ||
                    (action == WindowAction::CloseType && w->GetType() == window->GetType())))
                {
                    if (!w->Capabilities().closable) { if (action == WindowAction::Close) return invalid("EditorWindow cannot be closed."); else continue; }
                    closing.push_back(w);
                }
            if (!request.confirmed && std::any_of(closing.begin(), closing.end(), [](const auto &w) { return w->IsDirty(); }))
            { m_PendingClose = request; return invalid("Unsaved data: confirm closing these windows."); }
            for (auto &w : closing) w->SetVisible(false);
            if (std::any_of(closing.begin(), closing.end(), [this](const auto &w) { return w->GetId() == m_Active; }))
            { m_Active = 0; m_FocusRequested = false; }
            return {};
        }
        if (action == WindowAction::Rename)
        {
            if (!cap.renamable || !validName(request.shortName) || !validName(request.longName)) return invalid("Invalid window names.");
            s.shortName = request.shortName; s.longName = request.longName;
        }
        else if (action == WindowAction::Minimize)
        { if (!cap.minimizable) return invalid("EditorWindow cannot be minimized."); if (s.mode != WindowMode::Minimized) s.restoreMode = s.mode; s.mode = WindowMode::Minimized; }
        else if (action == WindowAction::Maximize)
        { if (!cap.maximizable || m_Locked || s.pinned) return invalid("EditorWindow cannot be maximized."); s.mode = s.mode == WindowMode::Maximized ? WindowMode::Normal : WindowMode::Maximized; s.restoreMode = WindowMode::Normal; }
        else if (action == WindowAction::Restore)
        { s.mode = s.mode == WindowMode::Minimized ? s.restoreMode : WindowMode::Normal; s.restoreMode = WindowMode::Normal; s.visible = true; }
        else if (action == WindowAction::Pin)
        { if (!cap.pinnable || m_Locked) return invalid("EditorWindow cannot be pinned."); s.pinned = !s.pinned; }
        else if (action == WindowAction::Float || action == WindowAction::Dock)
        { if (!cap.dockable || m_Locked || s.pinned) return invalid("EditorWindow docking is locked."); s.dockId = action == WindowAction::Float ? 0 : request.dockId; s.mode = WindowMode::Normal; }
        else if (action == WindowAction::Duplicate)
        {
            if (!cap.duplicable) return invalid("EditorWindow cannot be duplicated.");
            auto factory = window->Duplicate();
            auto duplicate = factory.window;
            if (!duplicate || duplicate.get() == window.get() || duplicate->GetId() != 0 || duplicate->GetType() != window->GetType())
                return invalid("EditorWindow factory did not produce a new instance of this type.");
            if (factory.contentCommand)
            {
                if (!contentCommand || !factory.contentCommand->IsUndoable()) return invalid("Engine duplication must supply an undoable command.");
                CommandContext context;
                auto result = factory.contentCommand->Execute(context);
                if (!result) return result;
                *contentCommand = std::move(factory.contentCommand);
            }
            auto copy = s;
            if (copy.shortName.size() + 5 < 256) copy.shortName += " copy";
            if (copy.longName.size() + 5 < 256) copy.longName += " copy";
            copy.dockId = 0;
            copy.mode = WindowMode::Normal; copy.pinned = false; copy.visible = true; copy.x += 24; copy.y += 24;
            duplicate->ApplyState(copy); duplicate->m_Workspace = weak_from_this();
            m_Active = m_Panels.AddExisting(std::move(duplicate)); m_FocusRequested = true; return {};
        }
        else if (action == WindowAction::Reveal)
        { if (!cap.projectItem) return invalid("EditorWindow has no project item."); window->RevealInProject(); return {}; }
        else if (action == WindowAction::Activate || action == WindowAction::Next || action == WindowAction::Previous)
        { s.visible = true; if (s.mode == WindowMode::Minimized) { s.mode = s.restoreMode; s.restoreMode = WindowMode::Normal; } m_Active = window->GetId(); m_FocusRequested = true; }
        if (action != WindowAction::Minimize) { m_Active = window->GetId(); m_FocusRequested = true; }
        else if (m_Active == window->GetId()) { m_Active = 0; m_FocusRequested = false; }
        window->ApplyState(s); return {};
    }

    std::string WindowWorkspace::Save() const
    {
        nlohmann::json result = {{"version", 1}, {"locked", m_Locked}, {"windows", nlohmann::json::array()}};
        for (const auto &w : Windows()) if (!w->Capabilities().auxiliary) result["windows"].push_back({{"id", w->GetId()}, {"type", w->GetType()},
            {"state", encode(w->GetState())}, {"view", w->SaveViewState()}});
        return result.dump();
    }
    Result<void> WindowWorkspace::Load(const std::string &json)
    {
        try
        {
            const auto document = nlohmann::json::parse(json);
            if (document.at("version") != 1 || !document.at("windows").is_array()) return invalid("Unsupported window layout.");
            const bool locked = document.at("locked").get<bool>();
            WindowSnapshot staged; staged.locked = locked;
            std::set<WindowId> ids;
            struct View { Ref<EditorWindow> window; std::string previous, next; };
            std::vector<View> views;
            for (const auto &item : document.at("windows"))
            {
                if (!item.at("id").is_number_integer()) return invalid("Window IDs must be integers.");
                const auto signedId = item.at("id").get<std::int64_t>();
                if (signedId <= 0 || signedId >= std::numeric_limits<std::int64_t>::max() || !ids.insert(WindowId(signedId)).second)
                    return invalid("Duplicate or invalid window ID in layout.");
                const auto id = WindowId(signedId);
                const auto type = item.at("type").get<std::string>();
                auto window = Find(id);
                if (window && window->GetType() != type) return invalid("EditorWindow type does not match layout.");
                if (!window)
                {
                    if (m_Panels.Contains(id)) return invalid("Layout ID conflicts with a legacy panel.");
                    if (type.starts_with("hosted.")) window = CreateRef<HostedEditorWindow>(type.substr(7));
                    if (auto factory = m_Factories.find(type); !window && factory != m_Factories.end()) window = factory->second();
                    for (auto &candidate : Windows()) if (!window && candidate->GetType() == type && candidate->Capabilities().duplicable)
                    {
                        auto factory = candidate->Duplicate();
                        if (factory.contentCommand) return invalid("Restore engine-backed windows from the project before loading its layout.");
                        window = factory.window; break;
                    }
                    if (!window || window->GetId() != 0 || window->GetType() != type) return invalid("No factory for saved window type: " + type);
                }
                auto state = decode(item.at("state"));
                const auto &cap = window->Capabilities();
                if (cap.auxiliary) state = window->m_DefaultState;
                if ((!cap.pinnable && state.pinned) || (!cap.minimizable && state.mode == WindowMode::Minimized) ||
                    (!cap.maximizable && state.mode == WindowMode::Maximized) || (!cap.closable && !state.visible) ||
                    (!cap.dockable && state.dockId)) return invalid("Saved state exceeds window capabilities.");
                staged.items.push_back({{id, window}, state});
                views.push_back({window, window->SaveViewState(), item.at("view").get<std::string>()});
            }
            // Retained windows must still be able to import legacy INI geometry on their first render.
            std::vector<std::pair<Ref<EditorWindow>, bool>> retainedGeometry;
            for (auto &w : Windows()) if (!ids.contains(w->GetId()))
            {
                staged.items.push_back({{w->GetId(), w}, w->GetState()});
                retainedGeometry.emplace_back(w, w->m_ApplyGeometry);
            }
            for (std::size_t i = 0; i < views.size(); ++i)
            {
                bool accepted = false;
                try { accepted = views[i].window->LoadViewState(views[i].next); }
                catch (const std::exception &) { /* Roll back earlier views below as well. */ }
                if (!accepted)
                {
                    for (std::size_t j = 0; j <= i; ++j) (void)views[j].window->LoadViewState(views[j].previous);
                    return invalid("Window rejected saved view state.");
                }
            }
            Restore(staged);
            for (auto &[window, applyGeometry] : retainedGeometry) window->m_ApplyGeometry = applyGeometry;
            return {};
        }
        catch (const std::exception &error) { return invalid(std::string("Invalid window layout: ") + error.what()); }
    }
}
