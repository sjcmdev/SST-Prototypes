#pragma once

#include <array>
#include <map>
#include <optional>
#include <functional>
#include "Core/Commands/CommandRegistry.hpp"
#include "Presentation/Panels/PanelRegistry.hpp"
#include "Presentation/Panels/EditorWindow.hpp"

namespace SST
{
    class KeymapResolver;
    struct ImGuiLayout;
    struct WindowSnapshot
    {
        struct Item { Entry entry; WindowState state; };
        std::vector<Item> items;
        bool locked = false;
        WindowId active = 0;
    };

    struct WindowLayout
    {
        struct Item { Entry entry; WindowState state; std::any view; };
        std::vector<Item> items;
        bool locked = false;
    };

    class WindowWorkspace : public std::enable_shared_from_this<WindowWorkspace>
    {
    public:
        explicit WindowWorkspace(PanelRegistry &panels) : m_Panels(panels) {}
        Result<void> Bind(Ref<CommandRegistry> commands, Ref<KeymapResolver> keymap);
        Result<void> Execute(WindowRequest request);
        Result<void> Apply(const WindowRequest &request, Unique<ICommand> *contentCommand = nullptr);
        [[nodiscard]] WindowSnapshot Capture() const;
        void Restore(const WindowSnapshot &snapshot);
        [[nodiscard]] WindowLayout CaptureLayout() const;
        Result<void> LoadLayout(const WindowLayout &layout);
        [[nodiscard]] std::string SaveLayout(const WindowLayout &layout) const;
        [[nodiscard]] Result<WindowLayout> ParseLayout(const std::string &json) const;
        [[nodiscard]] std::string Save() const;
        Result<void> Load(const std::string &json);
        [[nodiscard]] std::vector<Ref<EditorWindow>> Windows() const;
        [[nodiscard]] Ref<EditorWindow> Find(WindowId id) const;
        void RegisterFactory(std::string type, std::function<Ref<EditorWindow>()> factory)
        { m_Factories[std::move(type)] = std::move(factory); }
        [[nodiscard]] bool IsLocked() const { return m_Locked; }
        void SetActive(WindowId id) { if (!m_FocusRequested) m_Active = id; }
        void RenderMenu();
        void RenderOverlays();
        void RenderContextMenu(EditorWindow &window);
        void InstallSettingsHandler();
        void RemoveSettingsHandler();
        void FreezeSettings();
        void SetAvailableArea(float x, float y, float width, float height);
        [[nodiscard]] const std::string &LastError() const { return m_Error; }

    private:
        Result<void> WorkspaceOperation(const WindowRequest &request);
        void ResetWindowStates();
        std::string SettingsPayload() const;
        PanelRegistry &m_Panels;
        std::map<std::string, std::function<Ref<EditorWindow>()>> m_Factories;
        WeakRef<CommandRegistry> m_Commands;
        std::size_t m_CommandObserver = 0;
        bool m_Locked = false;
        WindowId m_Active = 0;
        float m_X = 0, m_Y = 30, m_Width = 1280, m_Height = 690;
        std::map<std::string, std::pair<WindowLayout, Ref<const ImGuiLayout>>> m_Workspaces;
        std::optional<WindowRequest> m_PendingClose;
        WindowId m_RenameId = 0;
        WindowId m_RenameOpenId = 0;
        bool m_FocusRequested = false;
        std::array<char, 256> m_ShortName{}, m_LongName{}, m_WorkspaceName{};
        std::string m_Error, m_PendingSettings;
        std::string m_FrozenSettings;
        bool m_CapturingIni = false;
    };
}
