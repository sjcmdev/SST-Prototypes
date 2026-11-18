# CapLab - M4 AS, defects and Arrhenius

Core owns commands, EventBus, Undo, jobs, progress, diagnostics and embedded
Python. PlotLab-Engine owns tables, row/object IDs and graphs. No dependencies
were added. M4 reuses the Arrhenius fit and adds only the explicit historical
cross-section conversion in SST-Physics.

Run from the SST-Prototypes root, with one build/test at a time:

```powershell
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --app --configuration Debug
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --require-embedding
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py PlotLab --require-embedding
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py JFM --require-embedding
```

The runner builds in root `build/`, with MSBuild `-nr:false -m:1`.
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
before replacing the document. Old M1 projects remain readable.
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
exports the selected graph. Full CSV/XLSX workflows remain M8.

IS and Walter analysis (M5+) and full CSV/XLSX export (M8) remain outside M4. Frozen source paths/hashes are recorded in
`tests/fixtures/m2/sources.json`; PFS I2 remains in
`tests/fixtures/manifest.json`.

CM3 validation: 19 GTest + 83 pytest; PlotLab 8/22, JFM 59/96 + 3 xfail,
SST-Libraries 99 GTest. See VALIDATION.md for acceptance values and evidence.
The sourced JSON material catalog and owner preset are shipped beside the
executable. App startup reached the render loop, but its close request timed
out and cleanup terminated the process. Graceful exit remains unverified;
VISUAL QA NOT RUN.


## M4 ? AS and Arrhenius

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
