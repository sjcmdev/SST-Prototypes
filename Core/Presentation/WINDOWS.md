# SST editor windows

`Panels/EditorWindow.hpp` is the base for new application windows. The native OS window remains `SST::Window`; `IPanel` supports gradual migration. `EditorLayer::RegisterPanel<T>()` owns windows and assigns monotonic IDs. Renaming preserves `###sst-window-ID`. Keep built-in registration order stable; recreate project-owned windows with saved IDs through `EditorLayer::RegisterWindow(window, savedId)` after registering built-in app windows, before restoring layout.

## Write a window

```cpp
class Results final : public SST::EditorWindow {
public:
    Results() : EditorWindow("app.results", "Results", true, {.duplicable = true}) {}
    SST::WindowDuplicate Duplicate() const override {
        return {SST::CreateRef<Results>(*this), {}};
    }
protected:
    void RenderContent() override { /* widgets; no window Begin/End */ }
};
// application.GetEditorLayer().RegisterPanel<Results>();
```

Auxiliary windows declare `{.auxiliary = true}`. They start hidden and are excluded from persisted workspaces. Optional `openCommand` / `closeCommand` IDs follow successful Core command execution (for example, preview / confirm import); failed confirmation keeps the window open. Hosted dialogs use `ImGuiWindowFlags_NoSavedSettings` for the same persistence policy. Other hosted views retain normal layout persistence.

The base supplies Begin/End, title buttons, dock-tab actions and context menu. Declare capabilities in the constructor. Override `RenderContextMenu()` to append app items, `RevealInProject()` to select the backing object, and `IsDirty()` to read document state. Closing hides the view and preserves data; dirty close/bulk close requires confirmation. Document saving belongs to the application.

Window and dock-tab labels start with the window icon, before the short title. Minimize, maximize/restore, pin and close stay at the right edge of the title/tab bar. The icon is display text; `###sst-window-ID` remains the stable ImGui identity. Dock actions share the dock node's ID scope so switching active tabs preserves their IDs.

## Commands and factories

Use `editor.GetWindows()->Execute(WindowRequest{WindowAction::Minimize, id})`. Chrome routes through Core Commands (`window.action`) and the shared UndoStack. Core Input registers `window.next` / `window.previous` on Ctrl+Tab / Ctrl+Shift+Tab; user keymaps can override them. `GetState()` exposes short/long names, mode/restore mode, visibility, pin, geometry and dock ID (0 = floating). `ApplyState()` is for initialization/restoration.

`Duplicate()` returns a new, unregistered view (ID 0). The base copy constructor resets identity/runtime binding. For engine-owned data, also return an **undoable `contentCommand`**: it executes before view registration and shares one Undo/Redo record with view removal/restoration. It may bind the new view to its new engine object. Execute/Undo/Redo must be transactional on failure; Redo restores the same object ID. `IPanel::Clone()` is disabled to prevent bypassing engine Undo.

## Persistence

`Save()` / `Load(json)` work without ImGui and belong in project IO; PrototypeTemplate demonstrates UI-thread capture before a background save and restoration after successful open. Applications own document content. `CaptureLayout()` / `LoadLayout()` retain typed window and view snapshots for workspace switching. `CaptureViewState()` / `RestoreViewState()` exchange owned C++ view objects (`std::any` supports heterogeneous window types); validate before mutation and accept your own captured state for rollback. `EncodeViewState()` / `DecodeViewState()` are used only by layout file IO. Named workspaces retain native ImGui window, table and docking settings; INI conversion occurs at the settings handler.

On Windows, each executable keeps its layout at `%LOCALAPPDATA%/Semiconductor Studio/<executable-stem>/imgui.ini`; an existing portable layout is copied on first use. Screenshot runs retain their portable layout path, so existing sandboxed captures do not modify the user profile. Core saves changed settings periodically and on shutdown. `app.layout.reset` is shared by Window > Reset layout and `sst_application.execute` (JSON); it also works on a headless workspace. It clears docking and restores the registered window defaults, including visibility and pin/maximize state.

The `SSTWindows` settings handler adds state and named workspaces to the existing layout INI, using Core IO/config persistence. INI owns the dock tree; headless JSON owns window state. Named workspaces contain layout/view options, not document snapshots. Invalid/conflicting IDs, types and state are rejected. View-only copies can be recreated by the factory; project IO must restore engine-backed copies first. Windows missing from an older workspace are retained. Editor detach freezes state for ImGuiLayer's final save.

## Migration and limits

PlotLab and JFM application panels use the shared chrome. Static panels implement `RenderContent`; `HostedWindows` adapts dynamic windows whose contents must stay in their owner's current document/undo scope. Call `BeginFrame`, synchronous `Render` callbacks, then `EndFrame` to unregister removed views. Hosted entries participate in WindowWorkspace commands and persistence, while their registry render is empty to avoid drawing twice. No callback or document reference is retained by the registry. `OnContentSkipped` lets static panels finish pending undo groups or release input contexts when hidden/minimized.

Existing identities remain unchanged for hosted views; duplicated owners receive separate IDs. Static panels with plain legacy names import their INI geometry and dock ID into `###sst-window-ID` on first render. The hosted factory restores workspace entries before owners reattach their content. JFM's standalone `solver-to-integrate/gui.cpp` is outside the application build and retains its own windows.

No ImGui patch. Pin freezes floating geometry and preserves docking; maximize uses the main viewport and retains the docking reference for restore. Arrangement skips pinned/minimized/closed views. The minimized strip has one row. Short names label titles; long names appear in context menus. Proposal removal points are listed in WindowChromeLab/REPORT.md.
