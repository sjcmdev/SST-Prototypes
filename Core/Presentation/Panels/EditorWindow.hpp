#pragma once

#include <cstdint>
#include <any>
#include <stdexcept>
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
        bool auxiliary = false;
        std::string openCommand, closeCommand;
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
        SaveWorkspace, LoadWorkspace, ResetLayout
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
        void Render() override;
        [[nodiscard]] virtual std::string ImGuiName() const;

    protected:
        virtual void RenderContent() = 0;
        virtual int WindowFlags() const { return 0; }
        virtual void OnContentSkipped() {}
        virtual void RenderContextMenu() {}
        virtual void RevealInProject() {}
        // Captured values are owned C++ view objects; text conversion belongs to layout IO.
        [[nodiscard]] virtual std::any CaptureViewState() const { return {}; }
        virtual bool RestoreViewState(const std::any &state) { return !state.has_value(); }
        [[nodiscard]] virtual std::string EncodeViewState(const std::any &) const { return {}; }
        [[nodiscard]] virtual std::any DecodeViewState(const std::string &state) const {
            if (!state.empty()) throw std::invalid_argument("Unexpected window view state");
            return {};
        }
        void Request(WindowRequest request);

    private:
        friend class PanelRegistry;
        friend class HostedWindows;
        friend class WindowWorkspace;
        WindowId m_Id = 0;
        std::string m_Type;
        WindowCapabilities m_Capabilities;
        WindowState m_State, m_DefaultState;
        WeakRef<WindowWorkspace> m_Workspace;
        bool m_ApplyGeometry = false;
    };
}
