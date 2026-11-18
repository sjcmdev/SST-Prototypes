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
