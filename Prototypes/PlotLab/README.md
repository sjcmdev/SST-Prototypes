# SST-PlotLab — PL3

Minimal Core host for PlotLab-Engine: one project, multiple worksheet/graph windows,
Core docking and one Core undo stack. Generated with
`python scripts/generate_prototype.py PlotLab`; the generated example workflow was
replaced by the engine document bridge. Do not regenerate over the existing app.

From the SST-Prototypes root:

```powershell
uv sync --group dev
Vendor/Binaries/Premake/Windows/premake5.exe vs2022
python scripts/build_prototype.py PlotLab --configuration Debug
.\build\bin\Debug-windows-x86_64\PlotLab\PlotLab.exe
```

Builds are Debug x64 in the repository `build/` directories. Keep C++ builds serial.
MSBuild can also build `build/generated/vs2022/PlotLab/PlotLab.vcxproj` directly with
`/m:1 /p:Configuration=Debug /p:Platform=x64`.
The executable has adjacent `install/` and `python/` directories. Core owns CPython;
export uses the root `.venv` during development. This is not a portable packaged distribution.

## Five-minute feedback workflow

1. **Import CSV / TXT** → `Prototypes/PlotLab/examples/demo.csv`. Inspect detected
   delimiter, decimal, header and units; explicit overrides refresh the preview.
   Import commits one undoable operation. The source file is never modified.
2. In the worksheet click **Calculated**. Set formula `Y * 2`, then close the dialog.
   Watch clean/dirty/running/failed/blocked and stale diagnostics. Edit `Y` and press
   **Enter**: calculated values and graphs refresh. Detach freezes current results
   and permits raw editing; an unfinished/stale calculation cannot be detached.
3. **Create graph from columns…** → worksheet, X=`X`, Y=`Calculated`, yErr=`yErr`.
   **Add series…** can add `Y` to that graph. All references use engine IDs.
4. Select a worksheet row; the graph highlights it. **Ctrl+click** a graph point
   selects and opens its source worksheet. Ctrl+click row toggles selection;
   Ctrl+Shift+click point toggles a point. Selection stays outside undo history.
5. Pan/zoom, hide a legend item, then **Export…**. Defaults: **80×60 mm**, **300 DPI**,
   SVG. PDF/SVG preserve vectors; PNG is **945×709 px**. Export uses the current
   ranges/ticks/visible series without auto-fit. Dirty/running/failed visible inputs
   must be recomputed before export. Recompute does not add undo entries.
6. Save `.plotlab`, reopen, use Ctrl+Z/Ctrl+Y before closing to review operations.
   History is session-local. New/open/close asks Save/Discard/Cancel when dirty.

Explorer creates folders, workbooks and worksheets under the selected compatible
parent. Click a worksheet/graph to reopen its window; closing a view never deletes data.
Rename and delete use Core Commands. Delete lists the subtree and all dependent
formulas/plots/error columns before confirmation; one undo restores the operation.
The worksheet supports numeric/text cells, rows, columns, role, units and long name.
Enter commits a cell; invalid numeric text is rejected. Blank numeric cells become NaN.
Rows are virtualized; wide sheets use pages of 64 columns and horizontal scrolling.
Import preview shows up to five rows/eight columns; the full table is imported.

Ctrl+N: new; Ctrl+O: open; Ctrl+S: save; Ctrl+Z/Y: Core undo/redo.
Export runs in Core JobSystem and appears in the Core progress/task monitors.
A failed export preserves the old destination and removes its temporary file.

## Boundaries and reuse

- `PlotLabDocument.*`: Core command adapter, engine events → Core EventBus,
  file entry points, IDs and operations. No second production undo stack.
- `PlotLabWorkspace.*`: thin worksheet/graph windows. Views read the same project;
  UI-Library `RenderPlot` draws every series and selection overlay.
- `PlotLabExport.*`: owning engine snapshot → JSON → embedded Python.
- `python/sst_plotlab/exporter.py`: matplotlib Figure + Agg/SVG/PDF, no pyplot or GUI.
- `SST-Libraries/PlotLab-Engine/GraphSnapshot.hpp`: immutable owning snapshot,
  independent of Core, Python, ImGui and GPU. Font/line/marker sizes are in pt.
- Core adds an optional close guard, used for unsaved documents and active exports.
  This window/document layer is the starting point for CapLab; no framework was added.

Graph text defaults to **DejaVu Sans 8 pt**. Each graph offers bundled DejaVu
Sans/Serif/Sans Mono, a size in pt, and **Load font (.ttf/.otf)**. Preview and export
resolve the same file; SVG uses outlines and PDF embeds/subsets the chosen font.
The bundle includes `LICENSE_DEJAVU`. Font changes participate in Core undo and
project v4 persistence; v1/v2 projects open with defaults and v3 retains its font settings.
Save copies custom .ttf/.otf files into `<project>.fonts/` beside the project,
using an FNV-1a 64-bit checksum. Move that directory together with the project.
Open uses the checksum-verified copy first, then the original path as fallback;
if neither matches, opening fails with a diagnostic and keeps the current document.
Missing/corrupt/mismatched fonts show a diagnostic and block rendering/export.
Preview converts pt to logical pixels at 96 DPI with ImGui display scaling.
Live ImPlot layout and matplotlib remain separate; 0.5 mm UI/export layout parity
is not claimed. See `experiments/README.md` for the native/Cairo comparison.
No annotations, style editor, analysis, Python
console, bulk paste or JPEG in PL3. No portable runtime packaging, autosave/recovery,
non-Windows/Release validation, or million-point interactive performance gate here.
The engine remains responsible for its existing formula worker/lifecycle policy.

## Verification

`python scripts/test_prototype.py PlotLab --require-embedding` builds/runs all app
GTest and pytest tests. GTest must run from the **SST-Prototypes root**. In restrictive
sandboxes, set TEMP/TMP to a writable directory and pass pytest
`--basetemp=build/pl3-pytest-temp`; production code does not change for that restriction.
`PlotLab.exe --smoke-test` renders a wide worksheet and graph, checks the dirty-close
prompt, undoes the edit back to clean, then exits after eight frames.

See [VALIDATION.md](VALIDATION.md) for results from this job.
Matplotlib API references: [Figure.savefig](https://matplotlib.org/stable/api/_as_gen/matplotlib.figure.Figure.savefig.html),
[physical figure sizes](https://matplotlib.org/stable/gallery/subplots_axes_and_figures/figure_size_units.html).


## P6a worksheet headers and formats

Worksheets show Long Name, Units, Comments, read-only F(x), Sparklines and named
parameter rows. **Header rows** controls visibility and parameter add/remove;
Enter commits metadata. Column tags open designations and display settings.
Double-click a numeric cell to edit its full stored value. Right-click ?
**Copy full precision**, or Ctrl+C over a numeric cell, copies its original value.
Header rows stay frozen. The data clipper uses a retained row-ID snapshot without
copying a million IDs every frame; sparklines sample at most 64 points per column.

The embedded package exposes `sst_plotlab.WorksheetSession().command(dict)` and
`WorksheetSession(active=True)` for the GUI document. Both use the same engine
Operation ? Core `plotlab.edit` ? UndoStack path. Commands are synchronous model
edits; no separate worker path is needed. Results are `{ok, result}` or
`{ok: false, error: {type, message}}` with `validation` / `not_found` error types.

```python
from sst_plotlab import WorksheetSession
session = WorksheetSession()
session.command({"action": "load_project", "project": project_json_object})
session.command({"action": "column_settings", "column_id": column_id,
                 "role": "Y", "display": {"mode": "engineering", "digits": 3}})
session.command({"action": "parameter_add", "worksheet_id": sheet_id, "name": "T"})
# snapshot returns the project JSON object, including the allocated parameter ID.
project = session.command({"action": "snapshot"})["result"]
```

Other metadata actions: `header_visibility` (header, visible), `parameter_remove`
and `parameter_visibility` (parameter_id, visible), `parameter_value` (column_id,
parameter_id, value). Worksheet actions take worksheet_id. Queries: `paired_x`
(column_id), `render_cell` / `copy_cell` (column_id, zero-based row_index).
`undo` / `redo` use Core history. Date/time values are Unix seconds in UTC.

Run without a window: `PlotLab.exe --worksheet-script script.py`.
`python/tests/worksheet_headless.py` is the executable assert-based command suite;
it is also run by `PlotLabTests.EmbeddedPythonWorksheetCommands` with a C++ fixture.
`examples/p6a-worksheet.plotlab` demonstrates all formats and header rows.
P6b formula extensions, P6c sort/filter and P6d import remain separate work.


### Set Column Values

Click a worksheet column header to open **Column definition**, then use the
**Set Column Values** panel: expression, inclusive one-based row range, optional
Python scalar assignments and None/Auto/Manual recalculate mode. The function
library lists syntax and algorithm conventions. Recalculate now starts the
worker without blocking rendering; hover a Failed/Blocked worksheet status to
read its diagnostic. Missing columns and cycles leave the formula Blocked with a diagnostic and preserve the previous values.

Embedded/headless Python uses the same commands and history:

```python
from sst_plotlab import WorksheetSession
s = WorksheetSession(active=True)  # False (default) creates an isolated document
response = s.command(dict(action="set_column_values", column_id=6,
    expression="scale*source+i", bindings={"source":5}, row_from=1, row_to=0,
    before_script="scale = mean(col(5))", recalculate="Auto"))
assert response["ok"], response
response = s.command(dict(action="recompute"))  # waits and publishes results
status = s.command(dict(action="recompute_status", column_id=6))
```

Inputs/outputs are JSON compatible. IDs are stable integers. Headless calls can
provide `bindings` (expression token to column ID) and `parameter_bindings`
(reference token to header-row ID), bypassing editor name lookup. `row_from` and
`row_to` are one-based inclusive; zero end means the last row. `i` is one-based.
Before-formula scripts run in Core's embedded interpreter on the recompute
worker. Their restricted AST allows finite scalar assignments, arithmetic,
math functions and mean/sum/min/max over `col(literal_stable_id)`. It exposes
read-only numeric data from this project, with no imports, attributes, I/O,
loops, arbitrary functions or object access. Script column IDs participate in
the dependency DAG, invalidation and dependent deletion. Script inputs are
read-only, and a script failure keeps the previous output.

Each formula evaluates once when set. Afterwards None freezes results, Manual
requires Recalculate now and Auto follows sources. Domain failures have typed
`domain` errors and row diagnostics; blocked recomputes have `cycle` or `missing_column` errors; invalid requests have
`validation` or `not_found` errors. History and persistence include range, mode,
script and stable bindings. Evaluator v1 remains supported.


Cross-sheet sources may have different row counts. Scalar math retains the
source vector length; destination rows beyond a reference use NaN, and `interp`
uses all source samples. Before-script column IDs are listed in the function
library. Changing rows requires detaching frozen/ranged formulas; formula edits
and their undo/redo preserve those cached values.


UI validation preset: `--column-values-preview` waits for the P6b fixture's
derived B column (stable ID 6), opens the same editor initialization as a header
click, and expands its function library. With `--open examples/p6b-formulas.plotlab
--screenshot <path.png> --screenshot-after-frames 60 --exit-after-screenshot`,
this captures the actual panel without desktop automation. The preset only sets
view state; data operations continue through the shared Core/Python commands.
