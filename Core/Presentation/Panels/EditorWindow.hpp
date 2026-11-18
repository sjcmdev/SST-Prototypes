#pragma once

#include <cstdint>
#include <string>
#include "Presentation/Panels/IPanel.hpp"
#include "Core/Commands/Command.hpp"

namespace SST
{
    using WindowId = std::uint64_t;
    class WindowWorkspace;
    class PanelRegistry;
    class EditorWindow;
    struct WindowDuplicate
    {
        Ref<EditorWindow> window;
        Unique<ICommand> contentCommand;
    };

    enum class WindowMode { Normal, Minimized, Maximized };
    struct WindowCapabilities
    {
        bool closable = true, renamable = true, duplicable = false;
        bool minimizable = true, maximizable = true, pinnable = true, dockable = true;
        bool projectItem = false;
    };
    struct WindowState
    {
        std::string shortName, longName;
        WindowMode mode = WindowMode::Normal;
        WindowMode restoreMode = WindowMode::Normal;
        bool visible = true, pinned = false;
        float x = 80, y = 80, width = 640, height = 420;
        std::uint32_t dockId = 0;
        bool operator==(const WindowState &) const = default;
    };
    enum class WindowAction
    {
        Close, Minimize, Maximize, Restore, Pin, Rename, Duplicate, Float, Dock,
        CloseOthers, CloseType, Reveal, Activate, Next, Previous,
        Cascade, TileHorizontal, TileVertical, ShowMinimized, LockLayout,
        SaveWorkspace, LoadWorkspace
    };
    struct WindowRequest
    {
        WindowAction action = WindowAction::Activate;
        WindowId id = 0;
        std::string shortName, longName;
        std::uint32_t dockId = 0;
        bool confirmed = false;
    };

    // Content-only base for new SST windows; IPanel remains the migration adapter.
    class EditorWindow : public IPanel
    {
    public:
        EditorWindow(std::string type, std::string shortName, bool visible = true,
               WindowCapabilities capabilities = {});
        EditorWindow(const EditorWindow &other);
        [[nodiscard]] WindowId GetId() const { return m_Id; }
        [[nodiscard]] const std::string &GetType() const { return m_Type; }
        [[nodiscard]] const WindowCapabilities &Capabilities() const { return m_Capabilities; }
        [[nodiscard]] WindowState GetState() const;
        void ApplyState(const WindowState &state);
        [[nodiscard]] virtual bool IsDirty() const { return false; }
        [[nodiscard]] virtual WindowDuplicate Duplicate() const { return {}; }
        [[nodiscard]] Ref<IPanel> Clone() const final { return {}; } // Use the undoable workspace factory.
        void Render() final;
        [[nodiscard]] std::string ImGuiName() const;

    protected:
        virtual void RenderContent() = 0;
        virtual void RenderContextMenu() {}
        virtual void RevealInProject() {}
        // Project serializers own content. This hook is for window-specific view state.
        [[nodiscard]] virtual std::string SaveViewState() const { return {}; }
        virtual bool LoadViewState(const std::string &state) { return state.empty(); }
        void Request(WindowRequest request);

    private:
        friend class PanelRegistry;
        friend class WindowWorkspace;
        WindowId m_Id = 0;
        std::string m_Type;
        WindowCapabilities m_Capabilities;
        WindowState m_State;
        WeakRef<WindowWorkspace> m_Workspace;
        bool m_ApplyGeometry = false;
    };
}
