# CapLab — laboratory analysis and circuit simulation

Core owns commands, EventBus, Undo, jobs, progress, diagnostics and embedded
Python. PlotLab-Engine owns tables, row/object IDs and graphs. No dependencies
were added. M4 reuses the Arrhenius fit and adds only the explicit historical
cross-section conversion in SST-Physics.

Hold the global build lock separately for Premake, one MSBuild invocation, and
each test suite. See the claccept1 section for embedded Python test setup.

Run from the SST-Prototypes root:

```powershell
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh Vendor/Binaries/Premake/Windows/premake5.exe --scaps-core=D:/SJCM/01_SemiconductorStudio/scaps_core-wt/ui vs2022
$msbuild = & 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh "$msbuild" build/generated/vs2022/SSTPrototypes.sln -p:Configuration=Debug -p:Platform=x64 -nr:false -m:1 -p:CL_MPCount=6
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh build/bin/Debug-windows-x86_64/CapLabTests/CapLabTests.exe
```

Outputs are in root `build/`, with MSBuild `-nr:false -m:1`.
The app is `build/bin/Debug-windows-x86_64/CapLab/CapLab.exe`.

Choose an import profile, set input columns/units and read the preview.
Column numbers in the panel start at 1. Inspect samples, area, SI columns,
source rows, header, filename suggestions, conversions and quality diagnostics.
Confirming a whole manifest/project is one Core Undo entry; any failed
measurement leaves the existing document unchanged. Refresh the preview after
changing fields; stale settings cannot be confirmed.

Supported profiles:
- Lab AS Cp/Rp and converted AS C/G; additional source columns retained.
- ProPho nine-column DL/CV, with separate DLCP and CV profile descriptors.
  Duplicated CV profiles with zero polynomial coefficients are `CV-derived`.
- Lab CV with optional G, preserving acquisition order and sweep direction
  segments; mapped raw DLCP grouped by bias/T/f, with explicit amplitude and
  maximum-voltage conditions; IS Cp/Rp, C/G and ReZ/ImZ with explicit sign.
- SCAPS .cf and .cv. Repeated SCAPS blocks become separate measurements.
  Area is mandatory: C, G and Z are scaled using the confirmed area, with no
  historical 0.5 cm2 default or Napp/1.3 correction. Auxiliary columns remain.
- ProPho manifests and projects containing `__ndarray__` values.

A manifest defaults to its own directory. For the original Cd manifest,
select the ProPho root (the directory containing `data_Cd/`) explicitly.
For the frozen fixture, use `Prototypes/CapLab/tests/fixtures/m2/`.
The Cd files mix dot and decimal comma; select decimal comma (it also accepts
dot numbers) with whitespace/tab-separated columns. Missing directories report
their full path and ask for an explicit base directory. Duplicate sample names
create separate samples and never overwrite existing data.

Header metadata has precedence unless explicitly confirmed in the panel.
Filename T/f values remain suggestions. Missing metadata remains null.
Invalid text rows fail with source row numbers; exclusions are explicit.
Duplicate frequencies require exclusions; sorting keeps both original source
column order and the row mapping. Negative/zero Rp remains measured data with
a quality diagnostic; it is unsuitable for impedance conversion.

ProPho migration retains source data and settings, converts known measurement
indices, fit/defect selections and browser selections into persistent IDs, and
marks saved fits/results as `historical` / `ProPho-compat`. Defect points link
to a measurement/row only on a unique exact T/f match; unresolved points retain
their own T/f. Missing references are null, never rebound to another curve.
Nonfinite historical cells/results retain null and a reason; the engine uses
its existing missing-cell representation. No epsilon, band gap, covariance,
kernel version, emission prefactor or missing fit setting is reconstructed.
The ambiguous legacy SCAPS `Rp` field requires explicit C/G confirmation.
`Ea_eV` remains source metadata and never becomes Eg.

Ctrl+S atomically saves a self-contained `*.caplab.json`; Ctrl+O validates
and adds the project to the workspace. Old M1 projects remain readable.
The Python entry points are `caplab.import_preview`, `parse_measurement`,
`parse_lab_as` (M1 compatibility) and the real embedded
`project_from_preview` / `sst_caplab.roundtrip` bindings.

The CV / raw DLCP section selects a measurement, material, fit range and stable
row-ID mask. Choose the sourced SST-Physics material library, the explicit
owner ProPho-compat preset (eps_r=10, N_ref=1.24e19 cm^-3), or project material
values with a source and reference band. Missing values block dependent work.
CV uses SST-Math weightedLinearFit; raw DLCP uses NumPy least squares with
explicit degree 1/2, peak/RMS/peak-to-peak conversion, groups by bias/T/f,
relative covariance and residuals. Zero degrees of freedom means unavailable
covariance, never zero uncertainty. Unknown constant-maximum conditions give
a conditional DLCP result; invalid C0/C1 remain marked without a fake density.

Select x_d from CV at explicit AS bias, measured DLCP at zero bias, or a positive
manual value and description. Missing point policy is saved: block (default),
linear interpolation within measured bounds, or nearest point with a warning.
Source measurement, T/f/bias, policy and warnings are retained in the result.
No automatic extrapolation. Sources must belong to the same sample.

Fit starts a Core job from a detached snapshot. Main-thread acceptance is one
Core Undo entry; edits during fitting discard the old result. Enter a sample
area in mm2 to create an undoable edit; dependent analyses become stale.
Project save/open retains inputs, constants, materials, settings, covariance,
residuals, profiles, graph IDs and results. The CapLab window uses W1's
EditorWindow base. Select a result graph to view C-V, A2/C2 with fit, CV/DLCP
profiles, amplitude fit or residuals. Existing PlotLab snapshot PNG export
exports the selected graph. CSV/XLSX project-data export is available (M8).

The following CM3/CM4 notes are historical validation records. Frozen source paths/hashes are recorded in
`tests/fixtures/m2/sources.json`; PFS I2 remains in
`tests/fixtures/manifest.json`.

CM3 validation: 19 GTest + 83 pytest; PlotLab 8/22, JFM 59/96 + 3 xfail,
SST-Libraries 99 GTest. See VALIDATION.md for acceptance values and evidence.
The sourced JSON material catalog and owner preset are shipped beside the
executable. App startup reached the render loop, but its close request timed
out and cleanup terminated the process. Graceful exit remains unverified;
VISUAL QA NOT RUN.


## M4 ? AS and Arrhenius

AS temperature maps are available in **Compare stored data / AS maps** as
`AS C(f,T)` and `AS G/omega(f,T)`. Select measurements from one sample/bias at
distinct confirmed temperatures, then choose per-temperature 3D lines, surface
or heatmap. Measured grids are the default. Unequal grids remain separate 3D
lines with a diagnostic; only an explicit `interpolate_log_f` policy creates
a common requested grid. Interpolation is linear in ln(f), never in T, never
outside measured bounds and never across missing endpoints. Missing values
remain holes; source columns and row IDs are unchanged.

The existing nanobind `caplab.execute` command/job path accepts:

```python
result = execute(project, {
    "operation": "plot", "plot_type": "AS C(f,T)",
    "measurement_ids": measurement_ids, "view_kind": "surface",
    "grid_policy": "interpolate_log_f", "frequency_Hz": [100, 1000, 10000],
})
```

Omit `frequency_Hz` with `grid_policy="measured"`. The default `view_kind` is
`lines`; other values are `surface` and `heatmap`. Failures return typed
`plot_failed` errors and leave the project unchanged. Comparison records retain
the source snapshots, policy, requested grid, actual view and diagnostics.
The shared cl3d1 renderer supplies log-frequency axes, units, colour scale and
camera interaction. Camera, zoom, visibility, colormap and colour limits persist
per graph. Existing `plot`/`action="view"` accepts `colormap` (Viridis, Plasma,
Greys) and `color_range_F` (two increasing limits, or null for automatic).
Existing single/batch PNG/SVG/PDF export uses the saved/rendered view, including
the camera, colour limits and missing cells; headless 3D export needs a resolved
camera in `view`, as in `python/tests/test_as_map.py`.

The AS section accepts both old M1 Cp/Rp sweeps and M2 FrequencySweep C/G or
Cp/Rp data. Select the sample and measurement, then calculate AS in a Core
job. The default is the unequal-grid `numpy.gradient(C, ln(f), edge_order=1)`;
frequencies are sorted with stable row IDs and duplicates block processing.
Optional Savitzky-Golay filtering has explicit window/degree, operates on
sample indices, and keeps the raw C and raw derivative beside the result.
Minima use `find_peaks(-D, distance=2)` (PCF GUI path). No candidate is assigned
automatically. C-f, D-f and G/omega-f use PlotLab-Engine log-frequency graphs;
invalid Rp leaves G/omega unavailable with a diagnostic.

Create/name a defect and approve candidates from the list or Ctrl+click their
markers in the D-f graph (the shared renderer picks within 10 screen pixels).
Select the active defect and confirm the assignment. Measured points retain
measurement/row IDs and T/f; manual points require positive T/f and provenance.
Defects can be renamed/deleted; points removed or disabled. Every edit uses
Core Commands/Undo. Historical imported results remain historical records.

Fit the default `omega-T2` model or explicit `ProPho-compat`, or compare both.
Both use SST-Physics transformArrhenius/fitArrhenius; fitting uses 1/T while
plots present 1000/T. Default rate is omega=2*pi*f, prefactor zeta [s^-1 K^-2].
Default sigma is unavailable unless a positive gamma and source are supplied.
Compat fits ln(f), stores a Hz prefactor, and uses the sourced assumptions
Ncv=2e18 cm^-3, vth=1e7 cm/s for sigma [cm2]. Models have separate plots,
energies, prefactors, residuals and covariance. Two distinct temperatures
suffice; two points show no residual degrees of freedom and null covariance.

Changing/removing a defect or point invalidates the dependent fit. Measured
T/f changes require reassignment before refitting. Results keep snapshots,
model/version, constants, provenance, stable IDs, masks and smoothing settings
in the self-contained project. A late worker result is discarded; acceptance
is one Core Undo entry. The existing snapshot exporter handles AS/Arrhenius PNG.
A7's project/thesis conflict is retained explicitly; see VALIDATION.md.


CM4 validation: 20 GTest + 101 pytest passed with embedded Python required.
Debug application and tests are built in repository build/. Runtime launch was
rejected by Computer Use: "Computer Use was not approved to use caplab".
Start/exit is not verified; VISUAL QA NOT RUN. See VALIDATION.md for the full
acceptance matrix, regression counts and the explicit A7 source conflict.


## M5 ? IS simulation

The existing CircuitCore-UI CircuitDocument, SchematicEditor and compileDocument
are embedded directly from the sibling SST-Experiments/CircuitCore-UI sources.
The GLFW host, app lifecycle and PlotViews are not embedded. CircuitCore sources
are compiled into the app/test targets with their matching CRT. No source files
in CircuitCore or the experiment were changed. W1 EditorWindow remains the host.

Open **IS / CircuitCore schematic**, choose a sample and create a topology (for
example R0-p(R1,C1)). The reused editor handles placement, wires, ground, port,
rotation and component parameters. Each document edit uses caplab.circuit and
Core Undo/Redo. Schematic JSON, terminal IDs, layout, parameters and the circuit's
stable ID survive project save/open. An unfinished port can be saved; simulation
requires a valid port. Invalid edits never replace the document.

Choose a same-sample FrequencySweep in ReZ_ImZ, C_G or Cp_Rp form. Simulation
uses exactly its frequency values and ordering, measured temperature and bias.
Missing T/bias require explicit confirmation; existing metadata takes precedence.
The runner applies a temporary unit AC port-voltage source with the measured DC
bias, then derives Y from the delivered source current and Z=1/Y. The virtual
source never enters the saved schematic. The DeviceRegistry outlives compilation
and every solve. Core jobs check cancellation between frequencies; computation
never runs in panel rendering. Edits during computation reject the old result.
Main-thread acceptance is one Core Undo entry.

All IS graphs use the same PlotLab-Engine graph/snapshot path as CV/AS: Nyquist
(Re Z, -Im Z) with equal physical axis scales, Bode magnitude/phase, C-f,
G/omega-f and both complex residual components. Original Im Z retains its sign.
The existing PNG exporter receives the same resolved ranges/styles; equal-scale
exports match physical plot dimensions to those ranges. Bad measurements/model
points retain null values and diagnostics with stable codes and row IDs.
Changing a circuit invalidates its results; deleting it gives missing_input
without rebinding another circuit. Invalid ports, source conflicts and singular
AC systems have explicit diagnostics.

The existing CircuitCore SPICE junction/diffusion model is reused for PN and
Schottky diodes: Cj0/(1-V/Vbi)^m, linear continuation above FC*Vbi, plus tau*dI/dV.
Cj0 [F], Vbi [V], m, FC and tau [s] are editable and persisted. The existing
IJunctionCapacitance interface remains the hook for a future charge-consistent
physical model. This milestone adds no circuit optimizer or fitting UI (M6).
Source: owner decision 8.4; CircuitCore Devices/JunctionCapacitance.h and Diodes.h.
Y/Z conversions reside in the sourced SST-Physics impedance.hpp header (PFS,
Capacitance Spectroscopy of Thin-Film Solar Cells, pp.94?96).

Headless Python uses the real nanobind layer and the same Core commands/jobs:

```python
from caplab import execute
created = execute(project, {
    "operation": "edit_circuit",
    "edit": {"action": "create", "sample_id": sample_id,
             "name": "Randles", "topology": "R0-p(R1,C1)"},
})
assert created["ok"], created["error"]
project = created["project"]
result = execute(project, {
    "operation": "simulate",
    "settings": {"circuit_id": project["circuits"][-1]["id"],
                 "measurement_id": measurement_id},
})
```

Inputs/outputs are JSON values; project/measurement/circuit/row IDs are stable.
Use edit action replace with circuit_id and schematic, or delete with circuit_id.
The schematic keeps CircuitCore-UI version-1 keys and includes parameter units.
Simulation outputs use explicit frequency_Hz, temperature_K, bias_V and
*_ReZ_Ohm/*_ImZ_Ohm fields. The envelope has ok and error (null or code/message);
invalid_request and simulation_failed are stable error codes. Completed analyses
also carry valid/invalid/stale/missing_input and point diagnostics. No GUI or
source measurement files are needed. Native worker execution releases the GIL.


## M6 ? IS circuit fit

The IS panel fits a detached circuit with SST-Math SANelderMead (deterministic
Nelder-Mead, no annealing). Select each element/field, free/fixed and finite
bounds; starts come from the current schematic. Unlisted parameters stay fixed.
Passive R/C/L/CPE/Warburg and existing PN/Schottky SPICE parameters, including
Cj0/Vbi/m/FC/tau, are supported. Independent voltage/current sources block fit.
Parameter units match the schematic (`ohm`, `F`, `H`, etc.).

The objective is sum((Re residual)^2+(Im residual)^2)/scale_Ohm^2. Default
scale_Ohm=max(abs(measured Z), Z_floor_Ohm); a positive floor is explicit.
Headless callers may instead supply positive sigma_Ohm for every original row.
The selected frequency interval is inclusive, evaluated on exact measured f,
T and bias. Invalid initial points block fitting; invalid trial points receive
infinite objective and are counted, never published as usable simulations.
This score is not labeled statistical chi-squared. Covariance is estimated from
the weighted residual Jacobian at the optimum; insufficient degrees of freedom
or a rank-deficient Jacobian give null rather than zero uncertainty.

Fit/stop run through Core commands/jobs. Cancellation is checked between
frequencies, objective calls and iterations. A finished draft shows parameters,
score, stop status, model/measurement and complex residual plots through the
same PlotLab-Engine renderer. **Accept fit (Undo)** applies the circuit and
analysis together as one Core Undo record; discard/cancel leaves accepted
parameters untouched. Edits reject stale worker results or stale acceptance.
The saved result retains starts, fixed/free flags, bounds, fitted values, units,
weighting/floor/scales, iteration count, stop status, residuals and input snapshots.
Max-iterations results remain explicit drafts, requiring user acceptance.

The JSON API follows the same command/job path:

```python
fit = execute(project, {"operation": "fit", "settings": {
    "measurement_id": measurement_id, "circuit_id": circuit_id,
    "Z_floor_Ohm": 1.0,
    "parameters": [
        {"element": "R0", "field": "value", "unit": "ohm",
         "free": True, "min": 1.0, "max": 2000.0},
        {"element": "C1", "field": "value", "unit": "F",
         "free": False, "min": 1e-6, "max": 1e-3},
    ],
}})
assert fit["ok"], fit["error"]
# fit["project"] is unchanged. Review fit["draft"]["project"]["analyses"][-1].
accepted = execute(project, {"operation": "accept_fit", "draft": fit["draft"]})
assert accepted["ok"], accepted["error"]
project = accepted["project"]
```

Optional settings: max_iterations (1..10000, default 500), score_tolerance
(default 1e-12), frequency_min_Hz/frequency_max_Hz and sigma_Ohm. `cancel: true`
on the outer fit request requests Core cancellation before queued work starts.
For a running fit, pass a caller-supplied `operation_id` (at most 128 characters):

```python
# Fit on the Core main thread; execute releases the GIL while waiting for the job.
fit = execute(project, {"operation": "fit", "operation_id": "my-fit", "settings": settings})
```

From another Python thread, while the fit call is running:

```python
stop = execute({}, {"operation": "cancel", "operation_id": "my-fit"})
```

`cancel` routes to caplab.cancel/Core JobSystem; it returns `not_running` when
the ID is absent or the job has already stopped. Other document operations
require the Core main thread and return invalid_request on a wrong thread.
GUI jobs support interactive stop. Other typed errors are fit_failed and
cancelled. Dropping a headless draft discards it without mutation.
M7 Walter and M8 data exports are described below.


CM6 validation: 33 GTest + 116 pytest; PlotLab 9/22, JFM 67/96 + 3 xfail.
Final normal Debug build (with project references) succeeded in repository
build/. One Computer Use launch request was rejected: "Computer Use was not
approved to use caplab". Start/exit remains unverified; VISUAL QA NOT RUN.
See VALIDATION.md for the acceptance table and evidence.

## M7 — Walter 1996 and ProPho-compat

The Walter section selects same-sample AS series, an optional CV fit and an
optional omega-T2 Arrhenius fit. The default is Walter1996-linear-field,
Walter et al. (1996), equation 25; ProPho-compat uses its separate square-root
factor and an explicit constant B. B is never inferred from a historical fit
or multiplied by T squared. Every model change creates a separate analysis.
The sourced physical kernels and the two quadratures live in SST-Physics
`walter.hpp`; Python only selects inputs and differentiates C/area against
ln(2*pi*f) after the frequency/row mask (PCF semantics).

Select x_d from CV at AS bias (default), measured DLCP at V=0, or a positive
manual width with a description. Existing block/linear/nearest policies are
reused and saved with measurement, fit, T/f/bias and warning provenance.
A selected CV fit supplies its source measurement/material when those are
not overridden. Known measurement bias takes precedence; conflicting
confirmations block analysis. Missing material, Fermi energy or emission
parameters block dependent work. Manual physical inputs require a source.

The panel offers T/f/E filters, an integration interval and overlaid spectra
through the shared PlotLab-Engine graph/snapshot exporter. Results preserve
row IDs, model, constants, source fit snapshots, parameters and quality.
Invalid-domain cells retain null and a reason; they remain visible in the
result details, and missing plot cells break the line. Negative spectra are
preserved. Default integration sorts E, interpolates interval boundaries and
never joins across invalid samples. Compat uses minus trapezoids in supplied
order with sample-only interval selection; it does not interpolate boundaries.
A compat interval with invalid samples has an unavailable integral. Default
partial integrals report gaps and the count of valid segments.

Analyses use caplab.analyze -> Core job -> caplab.accept_analysis/Core Undo.
Cancellation, stale revisions, source edits/removal and round-trip persistence
use the existing workflow. JSON headless operations use the real nanobind
binding and exactly the same command/job path:

```python
from caplab import execute
result = execute(project, {"operation": "walter", "settings": {
    "measurement_ids": [measurement_id],
    "model": "Walter1996-linear-field",
    "V_b_V": 0.8, "E_F_eV": 0.15, "zeta_s_1_K_2": 8e7,
    "parameter_source": "independent reference",
    "xd_source": "manual", "xd_manual_m": 2e-6,
    "xd_description": "reference width", "integration_range_eV": [0.1, 0.4],
}})
assert result["ok"], result["error"]
project = result["project"]
```

Optional cv_fit_id / arrhenius_fit_id / dlcp_fit_id select valid same-sample fits; default
Arrhenius emission must be omega-T2. Compat requires B_s_1 and emission_source.
Filters are temperature_range_K, frequency_range_Hz, energy_range_eV;
excluded_row_ids refer to the original stable rows. Source controls are
xd_source, xd_policy, xd_measurement_id, as_bias_V, material/project_material.
Returned errors use walter_failed / cancelled / invalid_request. operation_id
supports cancellation from another Python thread as in M6.

For imported historical records use operation=integrate_saved_walter with
settings={sample_id, optional integration_range_eV}. It replays the frozen
walter.dist_for_integration slice and records its original selection/range.
Owner decision §8 limits W1–W3 compatibility to these saved integrals; full
historical spectrum reconstruction remains unsupported. Source file and slice
hashes, sample counts, intervals, independent PFS expectations and tolerances
are frozen in tests/fixtures/m7/sources.json. M8 completes the CSV/XLSX workflow below.

CM7 numerical acceptance: CapLab 33 GTest / 155 pytest; PlotLab 10 / 22;
JFM 71 / 96 plus 3 xfail; SST-LibrariesTests 107 GTest. Debug builds use
SST-Prototypes/build/ and SST-Libraries/build/, all under build-lock.
W1-W3 relative differences are 7.95e-12 / 9.37e-12 / 9.31e-11 (limit 1e-4).
One post-build Computer Use launch was rejected: "Computer Use was not approved
to use caplab". Start/exit remains unverified; VISUAL QA NOT RUN.
See VALIDATION.md for exact values, commands, logs, scope and remaining gaps.


## M8 ? complete workflow and exports

The existing export command accepts `.csv`, `.xlsx` or `.png` destinations.
CSV/XLSX export the entire project data, including every measurement, stable IDs,
units, model/version, settings, parameters, masks, residuals and provenance.
CSV is a lossless `path,value_json` table using JSON pointers and explicit JSON
nulls. XLSX has the same Metadata table plus a numeric worksheet per measurement
with unit-bearing column headers, original source rows and stable row IDs.
XLSX uses standard-library OOXML inline strings: imported text never becomes an
Excel formula. Files exceeding Excel's row/column/text limits fail explicitly;
CSV remains available. Data exports and the shared PlotLab PNG exporter replace
the destination atomically, preserving the original on failure.

PNG exports only the active graph's visible series with its resolved preview
ranges, styles, units, legend and dimensions through the existing PlotLab
snapshot/exporter. Nyquist preserves equal physical axis scales. Missing runtime,
missing PNG dependencies, unresolved viewport and failed writes are explicit
errors. CSV/XLSX require embedded Python but no additional packages.

Delete measurement (Undo) removes the domain input and marks dependent results
missing_input. Saved engine curves and historical references retain their original
IDs; deletion never reconnects an analysis to a different measurement. Core Undo/
Redo restores the full document, including statuses. Area and defect point edits
also refresh downstream results. Release regression covers import ? CV/raw DLCP
? AS ? approved defect points ? Arrhenius ? Walter ? save/open/export, plus
measurement ? IS simulation ? fit draft ? accept ? save/open.

Additional JSON operations use the same native commands/jobs as the GUI:

```python
from caplab import execute
r = execute(project, {"operation": "analyze", "settings": {
    "type": "AS", "measurement_id": measurement_id}})
project = r["project"]
execute(project, {"operation": "save", "path": "sample.caplab.json"})
opened = execute(project, {"operation": "open", "path": "sample.caplab.json"})
execute(project, {"operation": "export", "path": "sample.xlsx"})
execute(project, {"operation": "export", "path": "plot.png",
    "graph_id": graph_id, "ranges": [100, 1e6, 0, 1e-9],
    "visibility": [{"plot_id": plot_id, "visible": True}]})
```

`analyze` accepts the existing CV, DLCPPolynomial, AS and Arrhenius settings;
Walter and circuit operations retain their existing API. `edit_defect` takes the
same `edit` object as the defect command; `area` takes `sample_id,area_m2`;
`delete_measurement` takes `measurement_id`. Save/open/export return `save_failed/open_failed/export_failed`; analysis errors
use `analysis_failed`. All return JSON `ok/project/error` envelopes. PNG additionally
returns the actual exporter snapshot in `export`; its ranges must be supplied
explicitly for headless use. Selection uses stable graph/plot IDs. PNG export
changes the active graph selection only if `graph_id` is provided.

Frozen sources and independent tolerances are indexed by
`tests/fixtures/m8/sources.json`, linking the M1/M2/M7 manifests and the independent
synthetic SI inputs. No expected result is generated by the kernel under test.
Release validation commands (run from repository root):

```powershell
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --require-embedding --configuration Release
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --app --configuration Release
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py PlotLab --require-embedding --configuration Release
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py JFM --require-embedding --configuration Release
```

### Startup and headless import

`CapLab.exe <project-or-import-file>` and `CapLab.exe --open <project-or-import-file>`
use the same Core command/job path. CapLab JSON projects open directly; measurement
files open an import preview for confirmation. ProPho JSON is detected automatically.
Python callers use `caplab.execute(project, {"operation": "import", "path": path})` to
read and confirm through the same commands, with a JSON error envelope on failure.
Use `options` for the existing import profile, mapping, units and confirmed metadata
(e.g. `temperature_K`, `area_m2`). Filename temperature suggestions stay unknown
unless explicitly confirmed through these options.

IS fit results include covariance in the order given by `covariance_parameters` and
per-parameter `sigma` in the accompanying `unit`. The estimate is
`inverse(J.T @ J) * weighted_residual_sum / (2 * frequency_count - free_parameter_count)`
for the weighted complex residual Jacobian at the optimum. Unidentifiable parameters
or insufficient degrees of freedom give null covariance and an explicit
`covariance_status`; fixed parameters have null sigma. Saved older fits remain readable.

## Model-only IS (clis2)

The IS panel can create a **New sample** and circuit in an empty project, then run
**Simulate model IS** without importing a measurement. The existing nanobind
`caplab.execute` command/job path accepts the same operation:

```python
created = execute(project, {"operation": "edit_circuit", "edit": {
    "action": "create", "sample_name": "Simulation", "name": "Randles",
    "topology": "R0-p(R1,CPE1)-W1"}})
assert created["ok"], created["error"]
project = created["project"]
result = execute(project, {"operation": "simulate", "settings": {
    "circuit_id": project["circuits"][-1]["id"],
    "temperature_K": 298.15, "bias_V": 0,
    "frequency_sweep": {"start_Hz": .1, "stop_Hz": 1e6,
                        "points_per_decade": 20}}})
```

Supply an existing `sample_id` instead of `sample_name` to attach a circuit to a
sample. Omit `measurement_id` for model-only simulation. T and bias are required;
there is no measurement object or implicit condition. Choose exactly one grid:
`frequency_Hz: [1000, .3, 25]` preserves explicit order, while `frequency_sweep`
uses positive start/stop Hz and integer points per decade, always including both
endpoints (one point if start equals stop). Frequencies must be positive, finite
and unique. Supplying a measured grid and an explicit grid together is an error.

The shared limit is **1..1201 points** for either grid. This preserves the existing
measured-grid budget: each frequency requires a CircuitCore AC solve (including
DC linearization), and results persist model arrays and plotting columns. The cap
bounds job cost, project size and UI/export work; it is not a physics limit.
For example, six decades at 200 points/decade use 1201 points. Larger sweeps fail
with `simulation_failed` rather than truncating or silently coarsening the grid.
Cancellation remains available between solves.

Results contain `grid_source: "model"`, explicit `frequency_Hz`, `temperature_K`,
`bias_V`, model Z, and five graphs: Nyquist, Bode magnitude/phase, C-f and G/omega-f.
`row_ids`, measured Z and residual arrays are empty; there are no residual graphs.
Every compatible same-sample measurement overlays on its original frequency grid;
legend entries retain its stable ID, T and bias, including unknown conditions.
Overlay inputs are snapshotted for staleness checks. Invalid measurement points
produce gaps and warning diagnostics without invalidating an otherwise valid model.
Project save/open, Undo, graph selection and PNG export use the existing workflow.

`fit` continues to require `measurement_id` and exact measured frequencies/T/bias.
The robustness fixtures independently evaluate R-CPE and R-CPE-W formulas over
7.5 decades, with clean/0.2% seeded-noise spectra and starts at 0.6/1.6 of truth.
The real-data case uses every 60th acquisition row of the existing frozen
`tests/fixtures/cf_100K_7.dat` (PFS I2, provenance in `tests/fixtures/manifest.json`),
without interpolation. It checks finite bounded fitting and score reduction;
it does not claim that this empirical topology identifies the device physics.

## Shared publication export (clshare3)

JFM and CapLab use `Core/ScientificRuntime/PlotExport` and the Core-free
`PlotLab-Engine/SnapshotJson` boundary. They no longer compile or deploy PlotLab
app sources. PlotLab retains its paused copy.

The figure panel exposes PNG, SVG and PDF, plus batch selection. Render every
selected graph first so its ranges and legend visibility are captured. Batch
files use `<chosen stem>-<graph_id>.<format>`, independent of editable labels.
Data tables remain available alongside the figure controls.

The existing nanobind `caplab.execute` uses the same command/job path:

```python
execute(project, {"operation": "export", "path": "figure.svg",
                  "graph_id": graph_id, "ranges": [xmin, xmax, ymin, ymax],
                  "visibility": [{"plot_id": plot_id, "visible": False}], "dpi": 300})
execute(project, {"operation": "export", "path": "figures.pdf", "graphs": [
    {"graph_id": first_id, "ranges": [xmin, xmax, ymin, ymax]},
    {"graph_id": second_id, "ranges": [xmin2, xmax2, ymin2, ymax2]}]})
```

Single export returns the actual JSON snapshot in `export`. Batch returns
`export.graphs`, each with `graph_id`, `path` and `snapshot`. Optional `x_ticks`
and `y_ticks` contain `{value, label, major}` objects per graph. A batch needs
distinct existing graph IDs and explicit finite increasing ranges. Nyquist
preserves equal physical axis scales. Rendering/commit failure leaves existing
files intact; errors use the existing `invalid_request` / `export_failed` codes.

CSV keeps its lossless `path,value_json` contents and adds `<stem>.results.csv`.
XLSX keeps Metadata and measurement sheets, and adds a Results sheet. Results
are tidy lab rows with analysis ID/type/model/status, section, row ID, quantity,
value and unit: CV profiles, DLCP coefficients, AS points, Arrhenius fits,
Walter spectra/integrals and circuit fit parameters. Blank values remain
unavailable; stale/invalid statuses are retained. Both CSV files commit as one
batch. `export.results_path` and `export.result_rows` identify the lab table.

## Shared document lifecycle (clshare1)

Core `App/DocumentState.hpp` tracks path and saved revision. Core
`App/ProjectWorkspace.hpp` keeps stable project handles and document addresses;
JFM retains its domain store, selection and analysis data. `IO/RecentFiles.hpp`
implements the shared newest-first, deduplicated, ten-file list used by JFM,
ScapsLab and CapLab without changing their existing settings formats.

The CapLab menu provides New/Open/Save/Save As/Close and Recent projects.
The Project control switches among open documents; each retains its own data,
dirty state and Undo history. Opening another project keeps existing documents.
Application close reviews every dirty project with Save/Discard/Cancel. Active
jobs block destructive actions. Save failures keep the project open; a completed
save only marks its snapshot revision as saved, so later edits stay dirty.
Recent projects persist as `recent_projects` in `caplab-recent.json` beside the
application configuration. Autosave/recovery is outside this change.

Headless operations use `caplab.execute(project, request)` and the same Core
commands/jobs. Requests can include the previous response's `workspace` to retain
multiple documents across calls. Operations: `workspace`, `new`, `activate`
(`project_id`), `close` (`application` optional), `close_decision`
(`decision`: `save`, `discard`, `cancel`; `path` required for unsaved documents),
and `save_as` (`path`). Responses include stable `active_project_id`, each
project's data/path/revision/saved_revision/dirty, recent projects and a typed
error envelope. Live Undo command objects are not serialized into JSON.

For full Python regression with the built-in nanobind modules, set
`SST_RUN_PYTEST` to the app's `python/tests` directory and run the embedded
GTest host: CapLab `PrototypePython.EmbeddedRoundtrip`, JFM
`JFMProjectJobsTests.NanobindFitAcceptsJsonAndMethodKeyword`.
`SST_ROOT_PYTHON_PATHS` carries the repository environment's JSON `sys.path`.

The inherited plotting dependency mismatch was repaired with the existing shared
axis implementation and Core `ScientificRuntime/PlotExport` adapter plus the
PlotLab-Engine snapshot serializer. JFM and CapLab use that adapter; paused
PlotLab app sources were not modified. Frozen fixture bytes were restored only
when they matched the existing SHA256 after undoing checkout CRLF conversion;
fixture attributes now prevent that conversion. Scientific expected values were
not changed.

## Project tree and module windows (clproj1)

Each open project retains its own selection and Undo history. The tree groups
measurements by kind, temperature [K], frequency [Hz] and bias [V]; unknown
conditions are shown as `?`. Select an object to update details and the plot.
CV/DLCP, AS/Arrhenius, Walter and IS have separate Core windows and share this
selection. WindowWorkspace persists their docking and tab layout.

Context actions and details use Core commands: create/rename/duplicate/delete
sample, edit area/material/notes, move selected measurements (Ctrl+click for
several), merge all measurements into another sample, delete analyses/circuits.
Merging preserves the source sample metadata and historical/accepted results.
Moving a measurement out of a defect's sample disables its old point and keeps
its original measurement/row IDs in `moved_from`; no point is rebound.
Deletion preserves engine snapshots used by saved results. Undo restores the
same IDs; a deleted selection remains empty after Undo until explicitly selected.

The import window offers an explicit target sample or creation of new samples.
Historical bundles keep their own samples; their measurements can be moved
after import. No filename-derived condition becomes confirmed metadata.

Analysis edits recalculate a preview after editing ends, in cancellable Core
jobs. In AS/Arrhenius, the selected measurement or defect determines the preview.
Accepted results retain their own status (`valid`, `stale`, `missing_input`).
Accept preview is a separate Undo command; discard leaves accepted data intact.
IS simulation previews and fit drafts use the same acceptance path.

Headless parity: `caplab.execute(project, {"operation":"tree", "edit":...})`
exposes the actions documented in `python/caplab/__init__.py`; `operation=select`
uses `{kind, id}`. `operation=import` accepts `target_sample_id`. Responses return
stable selection IDs and a workspace that retains each project's selection.
No schema version change or additional dependency is required.

## Compare browser (2D)

The plot window selects saved graphs by ID and compares stored measurements or
analysis graphs across samples and temperatures. Raw actions cover C-V,
Mott-Schottky (requires area), imported N_CV(W)/N_DLCP(W), C-f, G/omega-f,
Nyquist and Bode magnitude/phase. Imported profiles also have direct plot buttons
in selection details. Select existing analysis graphs for calculated profiles,
Arrhenius defects and Walter spectra; no analysis runs when building comparisons.
Graphs with different quantities or scales cannot share axes. CV-derived profiles
cannot be presented as measured DLCP. Missing values remain gaps.

Legend order is sample name, numeric temperature and stable source ID. Colors
depend on sample ID and temperature, so selection order does not change them.
Axis scales, per-graph zoom, hidden curves and resolved tick labels survive
save/open. Engineering and power-of-ten labels use the shared axis renderer.
The existing exporter handles selected/batch PNG/SVG/PDF with each graph's saved
or explicitly supplied view. A graph needs resolved ranges before export.

`caplab.execute(p, {"operation":"plot", "plot_type":"C-f",
"measurement_ids":[id1,id2]})` returns `graph_id` and an updated project.
`graph_ids` selects existing analysis plots instead. `action="view"`, `graph_id`
and the shared `view` JSON persist ranges in the labelled axis SI units,
visibility and ticks. `x_scale`/`y_scale` accept `linear` or `log10` and refit the
changed axes. GUI and Python use `caplab.plot`, a Core job and one Undo entry.
Failures return `plot_failed`; cancellation keeps the original project.

## Import profiles and source updates (clfiles1)

The import preview accepts an existing sample ID, a single new sample name for
the whole batch, or separate new samples per source. Series groups show T [K],
f [Hz], bias [V] and measurement kind/variant before the atomic Undoable import.
Saved, named formats reuse PlotLab-Engine `ImportOptions`; they round-trip in
the project and persist in the app's `caplab-import-profiles.json` configuration.
Cp/Rp and C/G remain separate profiles. Z import requires `ImZ` or `-ImZ`.

Filename/folder rules provide candidates using `{stem_0}`, `{folder_0}`,
`{parent_folder_0}` and JFM's shared custom token/slice variables. Headers win
by default. Applying candidates is explicit and conflicting values require
confirmation. Every new preview resets that confirmation; import and source
diffs have independent confirmation controls, including the legacy SI preview.

Re-import parses the saved mapping and units in a Core job, then shows changed
cells and metadata before acceptance. Measurement/column IDs are retained;
changed grids get new row IDs and disable dependent approved defect points.
Accepted results become stale when their inputs change. A schema change must
be imported as a new measurement. Relink verifies the original SHA-256 and
updates provenance only, without reinterpreting a renamed file or its extension.
OS file drop opens the same preview and never imports automatically.

Python uses the same `caplab.execute` commands: `preview`, `import`, `profile`,
`reimport`, `relink`, `accept_source`. See its docstring for JSON request fields,
stable IDs, explicit units, typed errors and source-draft acceptance.

Open question for the owner: which additional instrument export formats are
actually used in the laboratory? Provide one valid and one damaged export per
format before adding adapters. The current adapters cover the repository's
Lab AS/CV/DLCP/IS, ProPho and SCAPS text/JSON fixtures; no instrument-specific
support is inferred from a compatible column layout.

## Integrated acceptance (claccept1, 2026-11-20)

The existing embedded `caplab.execute(project, request)` surface is the batch
interface; no separate CLI or server is needed. `python/tests/test_owner_workflow.py`
opens two projects, imports a folder into an existing sample, compares stored
curves, runs CV/DLCP/AS/defect/Arrhenius/apparent Walter analysis, creates AS maps,
saves/closes/reopens/exports, then simulates a circuit on an explicit model grid
and fits/accepts its measured spectrum. It reuses the established command/job path.
Pass the returned `workspace` on every subsequent workspace operation; its active
project is authoritative. SI values and IDs are JSON serializable; failures retain
stable error codes. See `caplab.execute.__doc__` for the complete request contract.

The real AS folder is `tests/fixtures/m2/data_Cd/#133_Ann250C_10.7mm2/AS`:
34 acquisitions, 33 distinct temperatures, 40 frequencies. The acceptance test
explicitly excludes the repeated `140K_REL_1.dat` acquisition from the map while
retaining every imported measurement. It does not average or synthesize data.

Plot labels use compact SI notation (`800 pF`), including 3D axes and colour scales.
Raw comparison legends default to sample and temperature; source/series IDs are
available in tooltips. The source list has a checkbox and a colour picker.
`plot.source_colors` maps source ID strings to RGBA arrays with channels in [0,1].
AS lines also accept measurement colours; surfaces and heatmaps use the colormap.
Styles persist in the engine and use the existing save/export path.

3D Z and colour ranges fit the measured values by default. **Include zero (Z /
colour)** is explicit. Python uses `plot`, `action="view"`, `graph_id`, and
`z_range_F` / `color_range_F` (two increasing finite limits or null for automatic).
Existing view camera/ranges remain authoritative when reopening a saved graph.
Headless views without captured ticks obtain labels from the same C++ axis
formatter. Export consumes the saved font, colormap, camera, ranges and labels.
It uses the existing Matplotlib vector/raster exporter, so rasterization and
3D text placement are not pixel-identical to ImPlot3D.

Logging starts hidden and remains available from Window. Unchecked controls have
a visible shared frame border. Build comparison and Edit / Undo are separate.
The broader standard menu bar and the Origin-like import redesign remain planned.

Builds use MSVC Debug x64 in this worktree's `build/`, with sibling library outputs
in `../SST-Libraries/build/`. Generate with the vendored Premake and an explicit
`--scaps-core` checkout, then build `build/generated/vs2022/SSTPrototypes.sln` with
`-p:Configuration=Debug -p:Platform=x64 -nr:false -m:1 -p:CL_MPCount=6`.
Each command and each test executable must have its own `build-lock.sh` call.
For Python suites, use the embedded GTest host with `SST_REQUIRE_EMBEDDING=1`,
`SST_ROOT_PYTHON_PATHS` set to JSON-encoded environment `sys.path`, and
`SST_RUN_PYTEST` pointing at the application's Python test directory. Set
`PYTEST_ADDOPTS=--basetemp=build/<unique-suite-directory>` (forward slashes).
Set `SST_CAPLAB_ACCEPTANCE_OUTPUT` to retain the workflow projects and figures.
Current counts and evidence are in VALIDATION.md.

## Diagnostics, CLI and build identity

Use **Support ? About Semiconductor Studio** for build identity and **Support ? Export diagnostics?** to preview a redacted local report. `--version` and `--export-diagnostics report.txt` work without opening a window; invalid CLI syntax returns 2. Session/run logs rotate; native crash evidence stays local. See [shared runtime documentation](../../docs/PROFESSIONAL_RUNTIME.md) for retention, crash probes, recovery notices, Python metadata and EXE/PDB manifests.


## Settings and crash recovery

Settings validate a complete candidate before applying it. Preferences use a per-user root;
`SST_PORTABLE_ROOT` explicitly selects a portable root. Settings sections provide a reset preview,
Undo and Apply & Save. Dirty projects receive a separate recovery snapshot every 30 seconds;
after interruption, **Recover project** opens a copy for review and Save As. Errors distinguish
partial completion, cancellation and timeout. See the [shared contracts](../../docs/PROFESSIONAL_RUNTIME.md#error-settings-and-recovery-contracts)
for paths, Python reset, CLI exit contract v1, retention and safe `.writing` recovery.
