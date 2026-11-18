# PL3 validation — 2026-11-18

Build: Debug x64, MSBuild 17.14.40/v143, dynamic Debug CRT, C++latest. Built
`PlotLab`, `PlotLabTests`, `JFM`, `JFMTests`, and the SST-Libraries solution.
One C++ build at a time, `/m:1`. Build directories:

- `D:/SJCM/01_SemiconductorStudio/SST-Prototypes/build`
- `D:/SJCM/01_SemiconductorStudio/SST-Libraries/build`

No out-of-tree fallback was needed. GTest runs were made from the SST-Prototypes
repository root (including the final runs of all three library binaries).
Tests below were actually run in this job; no previous milestone results are reused.

| Suite | Passed | Failed | Expected xfail |
|---|---:|---:|---:|
| PlotLabTests (GTest) | 7 | 0 | 0 |
| PlotLab Python (pytest) | 14 | 0 | 0 |
| JFMTests (GTest) | 59 | 0 | 0 |
| JFM Python (pytest) | 96 | 0 | 3 |
| PlotLab-EngineTests | 46 | 0 | 0 |
| UI-LibraryTests | 22 | 0 | 0 |
| SST-LibrariesTests aggregate | 98 | 0 | 0 |

The library aggregate includes the 46 engine and 22 UI tests: **98 distinct library
tests**, not 166. JFM xfails are the existing auto model selection gaps for
HZB25 T181/T191/T227 L0. JFM pytest reports 946 numeric overflow/nonfinite warnings;
its expected-failure policy was not changed. Existing vendor/Core/JFM compiler
warnings were not suppressed. Final PlotLab and library builds passed without new warnings.

CPython 3.13.2, matplotlib 3.11.2. The root dependency declaration and `uv.lock`
were updated; `uv sync --offline --group dev` completed using a writable temporary
copy of existing uv cache metadata/artifacts because sandbox network/cache access
was restricted. No new plotting/C++ dependency was introduced.

## What is covered

- Engine operations executed through Core CommandRegistry/UndoStack; stable object
  and row IDs across edit/add/delete/undo/redo; dependent deletion restored atomically.
- Formula recompute stays outside Core history, detach/manual edit, selection by
  stable row ID in both directions, engine notifications through Core EventBus.
- Owning snapshot data, visibility, fixed ranges/ticks/style, immutable after later
  edits, stale-input rejection and headless construction.
- App-layer file roundtrip preserves IDs/formulas/results/rows; failed open keeps
  the current document and path. Engine suites retain atomic-save/future-version gates.
- C++ snapshot → actual embedded interpreter → SVG/PDF/PNG files.
- Exporter physical size **80×60 mm** for PDF and SVG, **945×709 px** at 300 DPI
  for PNG, vector SVG paths/no figure image, exact axis limits/ticks, log gaps/yErr,
  font validation, invalid input and replacement failure preserving old files.
- Shared renderer captures ticks and Ctrl+click point index with synthetic ImGui
  input, without a graphics backend; existing JFM draw-buffer parity remains covered.

## Launch check

`PlotLab.exe --smoke-test --log-file=build/pl3-smoke.log` was launched with a hidden
native window. It initialized Core, OpenGL, ImGui/ImPlot and embedded Python,
rendered a **70-column worksheet and graph**, edited data through Core Commands,
attempted close, verified the dirty-close guard deferred it, undid to clean,
then closed after eight frames. Process exit code **0**, empty stderr.
This is a real GUI start/render/exit test, not a GPU-free headless launch.
Publication export itself is headless; the engine test target still has no GUI/Core/Python links.

## Reproduce and evidence

From SST-Prototypes root, after Premake generation:

```powershell
$msbuild = 'D:/Aplications/VisualStudio/VisualStudioIDE/MSBuild/Current/Bin/MSBuild.exe'
& $msbuild build/generated/vs2022/PlotLab/PlotLab.vcxproj /m:1 /p:Configuration=Debug /p:Platform=x64
& $msbuild build/generated/vs2022/PlotLabTests/PlotLabTests.vcxproj /m:1 /p:Configuration=Debug /p:Platform=x64
.\build\bin\Debug-windows-x86_64\PlotLabTests\PlotLabTests.exe
$env:PYTHONPATH = (Resolve-Path Prototypes/PlotLab/python).Path
.venv/Scripts/python.exe -m pytest Prototypes/PlotLab/python/tests --basetemp=build/pl3-pytest-temp
.\build\bin\Debug-windows-x86_64\PlotLab\PlotLab.exe --smoke-test
```

Build logs: `build/pl3-{app,tests,jfm,jfm-tests}-build.log`, sibling libraries
`build/pl3-libraries-build.log`. GTest logs/XML: `build/pl3-{app-tests,jfm-tests}.*`,
pytest XML: `build/pl3-{python,jfm-python}.xml`; JFM pytest console log:
`build/pl3-jfm-python.log`. Library logs/XML: sibling
`build/pl3-{PlotLab-EngineTests,UI-LibraryTests,SST-LibrariesTests}.*`.
Launch evidence: `build/pl3-smoke-result.txt`, `build/pl3-smoke.log`.
Sandbox runs redirected TEMP/TMP to `build/pl3-app-temp` (app) and sibling
`SST-Libraries/build/pl3-test-temp` (libraries); no test semantics were changed.

Not checked: portable distribution, Release/Dist, non-Windows, manual testing of
every native dialog/gesture, 0.5 mm visual layout parity and interactive million-point
performance. Preview uses Core's shell font while publication uses DejaVu Sans 8 pt.
No JPEG, analysis, console, annotations, style editor, bulk paste or autosave was added.
No git commands or commits were made.


## PL4 fonts and export experiment - 2026-11-18

Debug x64 built in repository `build/` (SST-Prototypes and SST-Libraries), no TEMP
fallback. Every build/test used the shared build-lock.sh; MSBuild -nr:false -m:1.
PlotLab and JFM tests ran from the SST-Prototypes root. No git commands/commits.

| Suite | This job |
|---|---:|
| PlotLab GTest | 8/8 |
| PlotLab pytest | 22/22 |
| JFM GTest | 59/59 |
| JFM pytest | 96 passed, 3 xfailed (946 numerical warnings) |
| PlotLab-Engine GTest | 46/46 |

GTest covers graph font JSON/project serialization, v1/v2 defaults, immutable
snapshot/JSON fields, undo/redo, invalid pt and the embedded Python resolver.
Pytest checks chosen DejaVu Serif/Mono in PDF FontName and SVG glyph names,
custom file resolution, missing/corrupt/mismatched fonts and preservation of an
existing export on failure. Bundled DejaVu license is included.

The first app smoke found a missing nanobind string caster include; fixed and
covered in the embedded resolver test. Final smoke returned 0 and asserted all
three graph viewports rendered: bundled Sans, custom-file Serif and Sans Mono,
including 10 pt, wide worksheet, dirty-close guard and undo.

Results, reproduction command, all 12 vector artifacts and source snapshots:
[experiments/README.md](experiments/README.md),
[raw metrics](experiments/results/report.json).
XML/log evidence is in `experiments/validation/`. The experiment is Python
headless, not a C++ Release timing claim. Native matplotlib remains production.
Cairo's font substitution and 0.804-1.990 mm position differences fail the gate.
Graphify updated successfully after the source changes.

Not checked: live UI/export 0.5 mm parity, standalone ImPlot draw-list serializer,
C++ Release/Dist or other operating systems. Custom fonts remain external paths;
project format v3 does not package their bytes. No Segoe assets were removed.


## P6a ? 2026-11-18

Build: Debug x64, existing `SST-Prototypes/build` and `SST-Libraries/build`
directories; MSBuild `-nr:false -m:1`, every build/test under
`tools/agents/build-lock.sh`. No git commands. Source edits stay inside
`PlotLab-Engine` and `Prototypes/PlotLab`.

- PlotLab and PlotLabTests built successfully.
- PlotLabTests: 15/15 passed, including direct engine tests for designations,
  nearest-left-X grouping and reorder/no-X cases; format edge cases (NaN,
  infinities, 1e-300, denormals, SI rounding, significant trailing zeros, UTC);
  user parameters, deletion/undo, save/load and v1?v4 migration.
- Embedded Python tests execute `python/tests/worksheet_headless.py` against a
  C++ project fixture. Every metadata command checks object content before the
  edit, after undo, and after redo. Invalid IDs/formats/headers/parameters return
  typed JSON errors. Copy and rendering are separate and leave Values unchanged.
- PlotLab-EngineTests: 47/47 passed. The first run completed all bodies but failed
  to write XML across repository sandbox boundaries; rerun from SST-Prototypes
  completed with XML. Million-point edit ~2.6 ms, save ~15.1 s, load ~20.9 s in
  Debug; these are model measurements, not a scrolling FPS claim.

Evidence: `experiments/validation/p6a-PlotLabTests.xml`, `p6a-engine.xml`.

Changed source/documentation files:

- Engine: `include/sst/plotlab/Engine.hpp`, `src/Engine.cpp`, `src/Diff.cpp`,
  `src/Persistence.cpp`, `README.md`.
- Prototype: `src/App.cpp`, `src/PlotLabDocument.hpp/.cpp`,
  `src/PlotLabWorkspace.hpp/.cpp`, `tests/PlotLabTests.cpp`,
  `python/sst_plotlab/__init__.py`, `python/tests/worksheet_headless.py`,
  `examples/p6a-worksheet.plotlab`, `README.md`, `VALIDATION.md`.

Not checked: Release/Dist, other operating systems, measured scrolling FPS.
The clipper remains active, row IDs are retained without copying every frame,
and each sparkline uses at most 64 samples. Graphify output was not changed
because the task explicitly limits writable source paths. P6b formula expansion,
P6c sort/filter and P6d import are not part of this change.


P6a additional results:

- Pytest exporter regression: 22/22 passed; `experiments/validation/p6a-pytest.xml`.
- In-app `--screenshot` ran from a temporary portable runtime at
  `%TEMP%/plotlab-p6a-runtime`, using the built Debug executable and the committed
  example. The first image was inspected and reshot with a compact 1920?1000
  layout, wider row label/date columns and the logging panel collapsed.
- Reviewed final screenshot: `experiments/validation/p6a-worksheet.png`.
  The requested `tools/agents/runs/gallery/p6a/` directory could not be created
  by the sandbox, and the app also failed to write there. The image is retained
  in the writable PlotLab validation directory instead. No permission changes
  or writes to other jobs' lock state were made.


The extra standalone `--worksheet-script` verification was queued behind the
shared lock, then canceled before launch while another job's long experiment
held the lock. The embedded headless suite already passed in PlotLabTests; the
standalone CLI entry itself was not executed in this job. Only this job's waiting
shell was stopped; the other lock holder was left untouched.
