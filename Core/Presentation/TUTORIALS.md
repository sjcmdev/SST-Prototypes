# Interactive tutorials

`Tutorial` is independent of ImGui and app state. Load a JSON definition, provide a registry of named predicates returning JSON values, and call `Tick` on the main thread. Its executor submits the step through the existing app command registry. Pass the real job busy state and last response; command acceptance alone never passes a step.

Each definition has `schema_version: 1`, stable `id`, `title`, and nonempty `steps`. Each step has a unique `id`, `target: {window, item?}`, `instruction`, JSON `command`, and `condition: {predicate, op, value}`. Comparisons: `eq`, `ge`, `gt`, `le`, `lt`. Optional `timeout_s` defaults to 120 for automated steps. Unknown predicates, malformed definitions, and duplicate step IDs are rejected before execution.

- `learn` waits for live state; it never executes a command or times out the user.
- `demo` executes each command, waits for its job, then checks the condition.
- `verify` uses the same execution and checks, with per-step PASS/FAIL and dependent SKIP entries.

`Report()` returns JSON with stable tutorial/step IDs and typed errors. `Stop()` prevents further steps; the app owns cancellation of an already submitted job. Resume indices apply to learn mode and require the user to retain or recreate the corresponding app state.

`TutorialOverlay` registers visible window/item rectangles each frame and draws the highlight, arrow, and coach. Targets that are absent or clipped are reported to the user; stale rectangles are never highlighted. Register an item immediately after submitting its actual widget.

`CommandMeta.menuPath`, `shortcut`, and `enabled` feed `RenderCommandMenu` and the existing command palette. `CanExecute` enforces the predicate for every caller. Standard menus are opt-in through `EditorLayer::UseStandardMenu`; the existing workspace Window menu remains the shared extension.
