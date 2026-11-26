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
2. Select the **Calculated** column header and press **Ctrl+Q**. Set formula `Y * 2`, then **OK**.
   Watch clean/dirty/running/failed/blocked and stale diagnostics. Edit `Y` and press
   **Enter**: calculated values and graphs refresh. Detach freezes current results
   and permits raw editing; an unfinished/stale calculation cannot be detached.
3. Select Y headers and use the **Line**, **Scatter**, **Line + Symbol** or **Column** toolbar icon.
   X defaults to the nearest designated X column or row numbers. **Layer Contents**
   adds/removes curves; its X/yErr pickers override the defaults. All references use engine IDs.
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
Rows are virtualized; up to 510 data columns share one horizontally scrolling grid.
Larger worksheets retain every column in the model; horizontal virtualization remains future work.
Workbook tabs switch sheets. Shift/Ctrl extend header selection; drag or Shift+click/arrows
select cell ranges. Ctrl+wheel zooms the grid; Ctrl+C/V copy/paste plain TSV with atomic undo.
Right-click a header or use Shift+F10 for designation, properties, sort, fill, mask and plot menus.
Ctrl+Q opens **Set Values**, Ctrl+D adds a column, F4 opens **Column Properties**.
Graph F2 opens the shared **Plot Details**; **Axes** and **Legend** use the shared editors.
Masked cells are red; plots and exports mark their points with red crosses. Analysis skips masks.
New saves use project format v9 (reads v1–v9); older v8 applications reject these files.
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
Missing Python export dependencies show a short message and keep the graph interactive
using the UI font. Install the dependencies and use **Font > Retry** to restore the publication font.
Preview converts pt to logical pixels at 96 DPI with ImGui display scaling.
Live ImPlot layout and matplotlib remain separate; 0.5 mm UI/export layout parity
is not claimed. See `experiments/README.md` for the native/Cairo comparison.
No annotations or JPEG. No portable runtime packaging, autosave/recovery,
non-Windows/Release validation, or million-point interactive performance gate here.
The engine remains responsible for its existing formula worker/lifecycle policy.

## Python in worksheets, formulas and scripts

**View > Python Console / Python Script / History as Python** share the same
`sst_plotlab` objects and Core command path. In embedded/headless Python:

```python
from sst_plotlab import project, books, sheets, sheet, plot
import numpy as np

iv = sheets["I-V 300 K"]       # also sheets.IV_300_K or books["Sample A"][0]
iv["Voltage"].units = "V"      # short name, long name, letter or zero-based index
iv.columns[0] = range(0, 10, 2) # rows grow; other columns retain their data
iv["J"] = iv["I"] / 0.105
iv.J.long_name = "Current density"
iv.J.units = "A/cm²"
iv.J.mask[1:3] = True
usable = iv.J.valid
iv.rows.append(Voltage=0.85, I=1e-3)
iv.add_column("P", -iv.Voltage * iv.J, units="W/cm²", role="Y")
frame = iv.to_pandas()
sheets.new("Copy", frame)
plot(iv.Voltage, iv.J, kind="line+symbol")
```

References keep stable IDs after rename. Ambiguous names list the candidates;
typos suggest close names. Row slices are views by row ID, including after sort.
`np.asarray(column)` exposes an immutable snapshot: assign through the column
to record undo. `project.run(code)` and every GUI run commit one Core undo step;
a failed/cancelled script leaves the worksheet unchanged. Returned run results
are JSON-serializable dictionaries (`ok`, `output`, `error`, `error_code`, `row`,
`line`), with attribute access for interactive use. Standalone sessions use
`sst_plotlab.WorksheetSession().project` and the same methods.

**Ctrl+Q > Language** selects Python or the existing Origin `col(A)` syntax.
Python accepts expressions or blocks assigning `result` / using `return`:

```python
np.log10(abs(I)) * 2
# or a block:
result = np.asarray(Voltage) * 2 + i
return result
```

Current-sheet names are available directly; `i` is one-based and `x`/`y`
resolve uniquely designated role columns. Live preview retains the previous
results while computing and reports sample errors by row. Auto recalc tracks
the columns actually read. The language and stable bindings survive save/load.
F(x) keeps that language; prefix a new header formula with `py:` or use `np.`
to select Python. Double-click F(x) opens the full editor and live preview.

Completion is available in Ctrl+Q, F(x), the console and Script editor, including
object members, names, keywords, NumPy signatures and help. Ctrl+Space requests
completion; select a suggestion (Tab inserts the first suggestion in F(x)).
The locked environment has pandas and no Jedi, so completion uses safe API
metadata and never evaluates calls in the text being completed.

Script windows include line numbers, basic syntax colours, Run / Run selection /
Stop, output and error line navigation. Scripts are normal project objects under
**Scripts** in Project Explorer; source can also be opened/saved as `.py` files.
Declare a parameter form with `params(area=0.105, T=(200, 330))`; scalar parameters
use drag fields and ranges use sliders. Parameter overrides are saved with the
project. History records set-values, add/delete-column, mask, sort and plotting
operations and can be sent to a new script.

Loaded Python content stays disabled until **Trust this project**. Trust is
remembered for the absolute path and SHA-256 of the file in user settings;
changed/moved files need trust again. Locally created projects start trusted.
Python runs on Core workers against immutable snapshots, with cancellation and
an execution budget (2 seconds for recalc, 5 seconds for normal runs, maximum
30 seconds headlessly). Cancellation/time limits are cooperative at Python
opcode boundaries: a native extension call finishes before Python can stop it.
Trusted Python has normal Python access to files and imports; it is not a sandbox.

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


### P6c data operations

The worksheet's **Sort, filter and data operations** section uses the shared left-label controls and Core commands/jobs. Python uses `sst_plotlab.WorksheetSession().command(request)`, the same nanobind entry point and undo stack. All mutating data requests accept `worksheet_id`; `wait` defaults to true headlessly. With `wait=False`, poll `{"action":"data_status"}` on the model owner thread. Results contain a stable `job_id`, `elapsed_ms` (worker preparation and validation), and `worksheet_ids` for created sheets. Concurrent changes reject the prepared result; retry against the new project revision. Core cancellation discards unfinished results. The GUI uses the application Core JobSystem; isolated headless sessions lazily create one worker. Published jobs release their snapshots; use a new worksheet command (not Core Retry) for a fresh snapshot.

Rows/ranges in data operations are zero-based, `[row_from,row_to)`. Formula ranges retain their existing one-based inclusive convention. Physical values use the referenced column's units; fields explicitly name `minimum_column_units`, `maximum_column_units`, `new_x_column_units`, `x_value_column_units`. Source row/object IDs survive edits, sort, undo, redo and project persistence (format 6, reads formats 1?5).

| action | Fields beyond worksheet_id |
|---|---|
| sort | `keys=[{column_id,descending}]`; stable multi-key order; optional `column_only=True` requires `confirm_break_rows=True`; NaNs last |
| filter | `filters=[{column_id,kind,...}]`, one condition per column, combined with AND; range: `minimum_column_units,maximum_column_units`; equality/contains: `value`; top_n: `count,descending` (largest by default); empty array clears |
| mask | optional `column_id` (omitted = row mask); `row_from,row_to,masked,color_rgba` (ImGui packed ABGR uint32) |
| find_replace | optional `column_ids,row_from,row_to`; exact typed `find,replace` values |
| transpose, stack | optional `column_ids,name`; homogeneous numeric or text columns |
| unstack | `key_column_id` (text), `value_column_id`; first-seen group order, ragged numeric groups padded with NaN (text with empty strings) |
| split | `at_row`; creates two sheets |
| join | `other_worksheet_id`; appends rows by column position; matching types and units required |
| extract | `filters` as above; creates a new sheet |
| reduce | `mode=every_nth` or `group_average`, `every_n`, optional `offset` for sampling; final partial average group retained |
| normalize | `column_id,mode=max/range/area/value_at_x`; area/value_at_x also `x_column_id`, latter `x_value_column_units`; range maps to 0..1; area output has reciprocal X units |
| interpolate | `column_id,x_column_id,new_x_column_units`; linear, strictly increasing usable source X, NaN outside source range; creates a sheet |
| statistics | `axis=column/row`, optional `column_ids,exclude_filtered,name`; linked mean/sum/min/max/sample std/count producers |

Filters never change source arrays and hide rows in worksheets, graphs and exports. Raw edits refresh filter visibility. Formula requests explicitly opt in with `exclude_filtered=True`; default false. Masks follow row IDs, colour cells and appear as cross markers in graph previews. Statistics always omit masks. `analysis_column` with `column_id` returns `values_column_units,units,row_ids`, with masked samples represented by JSON null; it optionally excludes filters.

Normalization, interpolation and group averaging omit masked/nonfinite input samples. Derived transformations create independent sheets with their source operation recorded in worksheet metadata; statistics alone stay linked and automatically recompute. Worksheet sort supports automatic full-range producers; frozen/ranged producers retain the engine's existing detach guard. Transpose/stack require homogeneous column types; join is row append, not a relational key join. Undo restores source value bytes (including signed zero and NaN payloads); commit/revision and ID reservation counters intentionally remain monotonic.

## Diagnostics, CLI and build identity

Use **Support ? About Semiconductor Studio** for build identity and **Support ? Export diagnostics?** to preview a redacted local report. `--version` and `--export-diagnostics report.txt` work without opening a window; invalid CLI syntax returns 2. Session/run logs rotate; native crash evidence stays local. See [shared runtime documentation](../../docs/PROFESSIONAL_RUNTIME.md) for retention, crash probes, recovery notices, Python metadata and EXE/PDB manifests.


## Settings and crash recovery

Settings validate a complete candidate before applying it. Preferences use a per-user root;
`SST_PORTABLE_ROOT` explicitly selects a portable root. Settings sections provide a reset preview,
Undo and Apply & Save. Dirty projects receive a separate recovery snapshot every 30 seconds;
after interruption, **Recover project** opens a copy for review and Save As. Errors distinguish
partial completion, cancellation and timeout. See the [shared contracts](../../docs/PROFESSIONAL_RUNTIME.md#error-settings-and-recovery-contracts)
for paths, Python reset, CLI exit contract v1, retention and safe `.writing` recovery.

## Scientific analysis, maps, templates and export

The **Analysis** menu and worksheet toolbar open the native scientific editor; **Batch...** maps X/Y/sigma explicitly per worksheet, checks source mappings before Run, and accepts successful reports with one undo. Python uses the same `plotlab.analysis.run` Core command, shared jobs and owner-thread acceptance:

```python
from sst_plotlab import WorksheetSession
s = WorksheetSession(active=True)
result = s.analysis.run(dict(
    id="fit-1", version=1, worksheet_id=sheet_id,
    x_column_id=x_id, y_column_id=y_id,
    settings=dict(operation="linear", weights="unweighted")))
if result["stage"] == "preview_ready":
    s.analysis.accept(result["job_id"])
```

Operations: `linear`, `exponential`, `gaussian`, `user`, `interpolate`, `mean3`, `sg5`, `constant_baseline`, `linear_baseline`, `differentiate`, `integrate`, `peaks`. Ranges use `x_from_x_units` / `x_to_x_units`; interpolation uses `target_x_units`. Parameters declare `name`, `units`, `start_parameter_units`, `lower_parameter_units`, `upper_parameter_units`, `fixed`. Sigma weights are `absolute_sigma` or `relative_sigma`, with `sigma_column_id` in Y units. Nonfinite results become Python `None`; rank deficiency and nonconvergence retain explicit error/status fields. User functions use `sst_plotlab.analysis.NamedModel` and explicit project trust; reopen does not execute them.

The user-model editor's **Load Python model...** explicitly executes a trusted `.py` file defining `model = NamedModel(...)` and `parameters = [...]` in the parameter format above. `s.analysis.load_model(path)` exposes the same load headlessly and returns its recipe/settings; subsequent `analysis.run` calls resolve the loaded callable by its stable model ID. Loading and evaluation use the existing five-second Python budget. Reopen restores metadata and requires an explicit reload of the callable.

`analysis.status(job_id)` returns scalar progress and report metadata; use `include_arrays=True` to request curves/covariance/peak arrays. Wait returns the requested result once; Accept/Cancel do not resend full arrays. Worksheet scripts use Core's cooperative child-job submit/wait path, including with a single worker. GUI and active-Python figure export capture the same current viewport; render caches are excluded from worker snapshots. Export rejects destinations that identify a known source file, including hard links.

Peak detection accepts one constant baseline anchor `{x_x_units: 0, y_y_units: baseline}` or two increasing X/Y anchors for a linear baseline. Detection and half-height crossings use the baseline-corrected signal. Reports contain strict interior maxima and linearly interpolated physical FWHM, with `None` when crossings cannot be resolved without crossing a mask/gap or another peak. Endpoints and plateaus are excluded; detection alone does not separate overlapping peaks. `include_curves=False` produces scalar fit/peak summaries; curve-only operations require `True`.

**Analysis → Global fit (shared / local)** fits the primary dataset and additional explicitly mapped datasets in one objective. The same worksheet may supply several different Y columns. Set each named parameter's `shared=True` for a common value; otherwise each dataset gets its own local value. Additional datasets can override local starts/bounds through their `parameters` list; shared definitions must agree. X/Y units must match across datasets. Python: `s.analysis.global_fit(recipe, [dict(id="330K", worksheet_id=sheet2, x_column_id=x2, y_column_id=y2)], model=optional_named_model)`. The equivalent recipe field is `global_inputs`. Results include each dataset and a `model_id="global"` summary with all qualified parameter names, joint covariance and global degrees of freedom. Member objectives/DoF also refer to the complete fit (`global_fit=True`). Accept creates all reports and the global covariance table in one undo step. A trusted `NamedModel` can supply an IRF-convolved TRPL model with shared IRF width and local lifetimes; each input grid crosses the Python edge once per job, and retained NumPy inputs remain valid.

**Analysis → Extrapolate (explicit range)** uses `operation="extrapolate"`, `target_x_units`, finite increasing `output_from_x_units` / `output_to_x_units`, and `acknowledge_extrapolation=True`. The stated model is linear continuation of the first/last two usable samples; gaps between those endpoint samples are rejected. Targets must remain within the explicit output range. The warning and range are retained in the recipe, and accepted curves are named `Extrapolated (linear endpoints)`. Interpolation keeps its original out-of-domain NaN policy. Generated X and measured X are separate (`x_x_units`, `observed_x_units`); raw measurements remain unchanged.

**Analysis → Multi-peak fit** uses `operation="multi_peak"`, one/two explicit baseline anchors and `peaks=[{id, shape}, ...]` (up to 32 components). Shapes are `gaussian`, `lorentzian`, `voigt` (Gaussian/Cauchy convolution), and `split_gaussian` (asymmetric left/right Gaussian widths). Parameter names are `<id>_height`, `<id>_center`, then `<id>_sigma`, `<id>_gamma`, or `<id>_sigma_left` / `<id>_sigma_right` as appropriate; Voigt uses both sigma and gamma. Height uses Y units and all widths/centres use X units. Heights are nonnegative and widths strictly positive. Baselines are fixed by the explicit anchors. Fit reports retain component positions, heights and physical FWHM; requested curves include baseline, individual baseline-subtracted components, total and residuals. Profiles are mathematical components, not physical identifications. All accepted curves use the shared plot renderer and existing PNG/SVG/PDF/JPEG export.

**Worksheet → Statistics** and the worksheet toolbar/context menu retain linked row/column count, sum, mean, min, max and sample standard deviation (N−1). Masks and missing/nonfinite values are omitted; `count` is the usable N, and filter exclusion is explicit. One sample has no sample standard deviation. Choose matching units when combining row values; existing mixed-unit rows retain their numeric convention and have no output unit. The `statistics` command remains the Python/headless entry point, with `axis="row"|"column"`, `column_ids` and `exclude_filtered`. Source edits recompute the linked results after save/reopen. The analysis sidecar now writes version 2 and reads version 1 with unshared parameters, no extra datasets and no extrapolation opt-in.

`s.create_map(request)` takes `worksheet_id`, explicit `x_column_id`, `y_column_id`, `z_column_id`, `grid_columns` (X fastest), optional `log_color`, `nonpositive="mask"|"error"`, `contour_levels_z_units`, `color_range_z_units`, `colormap`, `name`. Native **Plot → Heatmap / contour** has cancellable previews; **Graph → Map / contour settings** edits an existing map with one undo. `s.import_image(path, calibration)` accepts single-channel intensity images and explicit `first_x_x_units`, `first_y_y_units`, `step_x_x_units`, `step_y_y_units`, `x_units`, `y_units`, `z_units`; image row order is retained.

`s.save_graph_template(graph_id, path)` writes a versioned `.plottemplate`. `s.template_batch(path, [{worksheet_id, destination, bindings}], format="png")` preflights names/roles/units and output collisions, then publishes each graph and exports through Core jobs. The native **Graph templates / batch** window exposes the mapping overrides before Run. Successful items survive cancellation, and completion leaves the active worksheet unchanged.

`s.export_figure(graph_id, path, format, dpi=300, width_mm=None, height_mm=None, background_rgb=(1,1,1))` supports PNG/JPEG/SVG/PDF, explicit physical size and an opaque JPEG background. `s.export_data(request)` supports CSV/TSV/DAT with explicit `worksheet_id`, `column_ids`, `row_indices`, `destination`, `format`, `mask_policy="preserve"|"missing"|"omit_rows"`, `missing_policy="empty"|"nan"`. Both return `{ok, result}` or `{ok: False, error: {type, message}}`.

Keep `.plotlab`, `.plotlab.imports.json` and `.plotlab.analysis.json` together. The analysis sidecar restores the editor recipe/views and immutable accepted recipes (`s.analysis_recipes`); older projects without it use closed/default editor state. Unsupported or malformed editor state is rejected before replacing the current document. Saves stage a complete generation; interrupted commits finish before the next open/save. Recovery copies include these sidecars, current graph views, custom fonts and Python drafts.
