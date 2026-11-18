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
project v3 persistence; v1/v2 projects open with defaults. Custom fonts persist as
absolute paths, so moving a project requires keeping that font available.
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
