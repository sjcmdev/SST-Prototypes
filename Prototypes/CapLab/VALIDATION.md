# C1 / CapLab M1 validation ? 2026-11-18

Debug x64 built in `SST-Prototypes/build/`; dependencies built in the existing
`SST-Libraries/build/`. No TEMP build fallback was needed. All builds/tests ran
from the SST-Prototypes root under `tools/agents/build-lock.sh`, one at a time.
The local `check.py` reuses template tools, supplying direct `.vcxproj` MSBuild
with absolute `SolutionDir`, `-nr:false -m:1` and local uv/pytest work directories.

| Suite | GTest | pytest | Result |
|---|---:|---:|---|
| CapLab | 17 passed | 20 passed | Embedded Python required; no skips |
| PlotLab | 8 passed | 22 passed | Regression passed |
| JFM | 59 passed | 96 passed, 3 xfailed | Regression passed; existing numerical warnings |

CapLab's embedded case is additionally selected by the template runner to host
pytest. It is counted once in the 17 GTests. Its first pass and pytest pass both
execute the actual import command/EventBus/job/document pipeline and matplotlib
PNG export, with the interpreter owned by Core.

Evidence under root `build/`:
- `caplab-app-build.log`: CapLab.exe Debug x64 build succeeded.
- `caplab-tests-final.log`: GTest and embedded pytest results.
- `caplab-regression-plotlab.log`, `caplab-regression-jfm.log`: regressions.
- `caplab-graphify.log`, `caplab-graphify-final.log`: AST graph updates.

PFS I2 fixture SHA-256 matches the original ProPho file and the frozen manifest.
Tests assert 40 points, first 100 Hz / 8.8601e-10 F / 134160 Ohm, confirmed T=100 K,
and full JSON round-trip preserving engine/domain/row IDs and all columns.
Tests also cover Core Undo/Redo, duplicate names, schema/reference validation,
atomic-save errors, future-schema overwrite protection, stale open/preview,
failed open preserving the document, queued cancel, progress, main-thread
publication, snapshot save and cancellation after committed replacement.

GUI acceptance is **not complete**: Computer Use rejected `sky.launch_app` for
CapLab with `Computer Use was not approved to use caplab`. The app was built,
but a live window, visual C-f smoke and clean start/exit were not verified.
The user was asked to enable CapLab access; no alternate UI control was used.

Scope excluded: M2+ import profiles, CV/DLCP/AS analysis, IS, Walter and XLSX.
No PlotLab or library source files were edited; exporter code is reused read-only.
No git commands or commits were run.

## CM2 / M2 validation — 2026-11-18

Executed from the SST-Prototypes root, sequentially under
`bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh`.
Debug x64 outputs are in repository `build/`; no TEMP fallback was needed.
MSBuild used `-nr:false -m:1`. Existing library build outputs were reused or
built by project references; no library source edits were needed.

| Suite | GTest | pytest |
|---|---:|---:|
| CapLab | 19 passed | 57 passed |
| PlotLab | 8 passed | 22 passed |
| JFM | 59 passed | 96 passed, 3 xfailed |

The embedded interpreter was required; the CapLab pytest suite calls the real
`sst_caplab` binding. The extra filtered GTest invocation hosts pytest and is
not counted twice. CapLab has one pytest cache-provider configuration warning;
JFM emitted its existing numerical warnings (946 in this run).

Evidence in root `build/`:
- `caplab-m2-tests.log`: CapLab C++/embedded Python and pytest.
- `caplab-m2-plotlab.log`, `caplab-m2-jfm.log`: full regressions.
- `caplab-m2-app.log`: application build.

PFS I1 uses the original Cd manifest and all referenced AS/DL files, frozen
under `tests/fixtures/m2/data_Cd/`; it verifies #132/#133/#138/#139/#143 and
3.7/10.7/9/5.5/3.75 mm2. PFS I2 retains the original M1 case. I3 verifies the
blank line/DL header, 21 rows and confirmed 360 K / 1000 Hz. I4 verifies 49
C/G rows and its first values. I5 verifies 81 rows classified as CV-derived.
D1 independently checks SI DLCP and CV values at the first IDUB point.

Independent tests cover C/G versus Cp/Rp, two SCAPS areas and all C/G/Z
scalings, Napp without the historical correction, decimal comma, input/SI
units, mapped extra columns, Unicode names/paths, distinct manifest base
directories, duplicate sample names and frequencies, explicit exclusions,
truncation, metadata conflicts, UTF-8 BOM, SCAPS batch blocks and original
row mappings, CV directions, raw DLCP grouping, cancellation and progress.
Migration tests cover real Cd and SCAPS projects, ndarray data, historical
results, stable references/selections, unresolved inputs, nonfinite values,
quality diagnostics, and complete self-contained round-trip.

C++ tests additionally verify all-or-nothing bundle publication, persistent IDs,
mixed M1/M2 Undo/Redo, and failed last measurements preserving the document.
The embedded workflow test exercises a DL preview through the actual worker,
main-thread confirmation and Core Undo/Redo.

No GUI visual smoke was run in CM2. Historical results are retained, not
recomputed (M3+). Missing constants/settings remain absent. Root graphify was
queried before navigation; its output was not rewritten because CM2 restricts
writes to CapLab. No git commands or commits were run.
`CapLab.exe` final Debug x64 rebuild succeeded; deployed caplab Python files match source SHA-256.
