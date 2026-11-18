# CapLab — M2 importers and ProPho migration

Core owns commands, EventBus, Undo, jobs, progress, diagnostics and embedded
Python. PlotLab-Engine owns tables, row/object IDs and the C-f graph; no library
source changes or new dependencies were needed.

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

M3+ analysis, recomputation of historical results and GUI visual acceptance
are outside this milestone. Frozen source paths/hashes are recorded in
`tests/fixtures/m2/sources.json`; PFS I2 remains in
`tests/fixtures/manifest.json`.
