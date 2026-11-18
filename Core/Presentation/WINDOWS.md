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

The base supplies Begin/End, title buttons, dock-tab actions and context menu. Declare capabilities in the constructor. Override `RenderContextMenu()` to append app items, `RevealInProject()` to select the backing object, and `IsDirty()` to read document state. Closing hides the view and preserves data; dirty close/bulk close requires confirmation. Document saving belongs to the application.

## Commands and factories

Use `editor.GetWindows()->Execute(WindowRequest{WindowAction::Minimize, id})`. Chrome routes through Core Commands (`window.action`) and the shared UndoStack. Core Input registers `window.next` / `window.previous` on Ctrl+Tab / Ctrl+Shift+Tab; user keymaps can override them. `GetState()` exposes short/long names, mode/restore mode, visibility, pin, geometry and dock ID (0 = floating). `ApplyState()` is for initialization/restoration.

`Duplicate()` returns a new, unregistered view (ID 0). The base copy constructor resets identity/runtime binding. For engine-owned data, also return an **undoable `contentCommand`**: it executes before view registration and shares one Undo/Redo record with view removal/restoration. It may bind the new view to its new engine object. Execute/Undo/Redo must be transactional on failure; Redo restores the same object ID. `IPanel::Clone()` is disabled to prevent bypassing engine Undo.

## Persistence

`Save()` / `Load(json)` work without ImGui and belong in project IO; PrototypeTemplate demonstrates UI-thread capture before a background save and restoration after successful open. Applications own document content. `SaveViewState()` / `LoadViewState()` store view options only; validate before mutation, return false on invalid input, and accept your own saved state for rollback.

The `SSTWindows` settings handler adds state and named workspaces to the existing layout INI, using Core IO/config persistence. INI owns the dock tree; headless JSON owns window state. Named workspaces contain layout/view options, not document snapshots. Invalid/conflicting IDs, types and state are rejected. View-only copies can be recreated by the factory; project IO must restore engine-backed copies first. Windows missing from an older workspace are retained. Editor detach freezes state for ImGuiLayer's final save.

## Migration and limits

PrototypeTemplate, JFMOverviewPanel and ImportStatusPanel use the base; JFM content/store calls stay unchanged and old INI placement is imported. Other IPanel windows keep their existing chrome until migration. No ImGui patch. Pin freezes geometry and floats/restores docking; maximize uses the main viewport. Arrangement skips pinned/minimized/closed views. The minimized strip has one row. Short names label titles; long names appear in context menus. Proposal removal points are listed in WindowChromeLab/REPORT.md.
