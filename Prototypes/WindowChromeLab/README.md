# WindowChromeLab

Generated with `python scripts/generate_prototype.py WindowChromeLab` from PrototypeTemplate. Builds and runs through the repository's normal prototype tooling. Core API: [WINDOWS.md](../../Core/Presentation/WINDOWS.md).

The experiment opens a real template document/evaluation panel, independent plot and editable table views, and a Project Explorer tool. Right-click the title or a dock tab for the shared menu. `P`, `+`/`=`, `-` are pin, maximize/restore and minimize. Double-click a title to maximize/restore; middle-click a tab to close. Ctrl+Tab and Ctrl+Shift+Tab cycle views. The Window menu lists views, arranges them, locks the layout and saves/loads named workspaces. The minimized strip restores views. Rename supports short and long names without changing IDs.

The template Document menu saves actual document data plus window state. Lab sample vectors are in-memory demonstration data, independently copied by the duplication factory. Editing the table makes it dirty; closing asks before hiding it and preserves its data. Layout/workspace restoration changes view options and never overwrites sample values. Show in Project Explorer updates the tool's selected window ID. The context menu's sample-count/reveal section demonstrates application extensions.

GoogleTest covers headless transitions, ID allocation, capabilities, dirty bulk close, layouts, Core keymaps/Undo and engine command integration; backend-free ImGui tests exercise actual docking/tab interactions and INI persistence.
