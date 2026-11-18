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

## CM3 / M3 — CV and raw DLCP

Scope: CapLab and the capacitance header/tests in SST-Physics. No git commands
or source JFM configuration edits. The existing sourced material library,
SST-Math weightedLinearFit, Core command/job/Undo pipeline, W1 EditorWindow,
and PlotLab-Engine graph/snapshot export are reused.

Acceptance values below are ordered V_b [V], x_d [m], E_F [eV], N_d [cm^-3].
Expected values come from PFS C1-C5 / thesis table 4.2; raw data and saved
comparisons come from the existing hashed M2 fixtures. No expected result is
computed with the capacitance kernel under test.

| Case | Spec (published precision) | Computed (owner preset) | Difference after rounding | Verdict |
|---|---|---|---|---|
| C1 | .76; 3.79e-7; .13; 5.93e15 | .761846577; 3.788878e-7; .131094124; 5.934648718e15 | 0; 0; 0; 0 | PASS |
| C2 | .46; 8.10e-7; .14; 7.23e14 | .457216186; 8.099113e-7; .142832712; 7.228334340e14 | 0; 0; 0; 0 | PASS |
| C3 | .42; 9.30e-7; .17; 5.44e14 | .424348122; 9.297361e-7; .172945393; 5.437378765e14 | 0; 0; 0; 0 | PASS |
| C4 | .37; 1.62e-6; .19; 1.53e14 | .367984307; 1.615938e-6; .194787650; 1.531048444e14 | 0; 0; 0; 0 | PASS |
| C5 | .76; 5.19e-7; .26; 3.15e15 | .759699431; 5.190986e-7; .256836168; 3.146832167e15 | 0; 0; 0; 0 | PASS |

Ranges are exactly PFS: C1 [-.30,.35], C2 [.15,.35], C3 [-.20,.30],
C4/C5 [0,.25] V. Default owner preset: eps_r=10, N_ref=1.24e19 cm^-3.
The saved-project comparison independently uses the PFS reconstructed
N_ref=1.24315e19 cm^-3 and historical eps0=8.8541878128e-12 F/m.
All four saved parameters pass relative 1e-6 with those compatible constants
and DLCP(V=0) source. Using the owner preset instead changes E_F relative
to the saved project by -0.03319%, -0.02602%, -0.02529%, -0.02245%,
-0.03065% for C1-C5 respectively; this is recorded, not fitted away.

Synthetic CV recovers V_b=.8 V and N=1e21 m^-3. Synthetic raw DLCP recovers
C0=1e-9 F, C1=-2e-10 F/V, C2=3e-11 F/V2,
x_e=8.8541878188e-8 m and N_DL=1.762304234502524e23 m^-3,
all within relative 1e-6 without noise. Independent decimal SI calculations
provide the literal expected capacitances/density.

Boundary checks cover zero C1, nonpositive C0, constant V, deficient polynomial
rank, invalid amplitude units, explicit peak/RMS/peak-to-peak conversion,
unknown maximum-voltage conditions, missing zero/AS bias, default blocking,
linear interpolation without extrapolation, explicit nearest-point warnings,
point masks, nonphysical density signs, missing materials and cancellation.
Zero fit degrees of freedom leaves covariance unavailable. The embedded test
runs real analyze command -> EventBus -> worker -> main-thread acceptance,
round-trip, Core Undo/Redo, area invalidation and stale-result rejection.

Each result stores input snapshots/IDs/revision, SI units, model/version,
material/reference band/source, actual constants, range, mask, coefficients,
covariance semantics, residuals, profiles and full x_d policy/provenance.
Changing area/data/material metadata invalidates dependent results; missing
measurement descriptors give missing_input. Graphs use PlotLab-Engine and the
existing snapshot PNG exporter. Full CSV/XLSX workflow remains M8.

Root graphify was queried before navigation. No root graph update: task scope
restricts source writes to CapLab and required library paths while other jobs
edit the same repository. No visual QA claim is made without looking at GUI.

Final CM3 execution (2026-11-18), sequentially through build-lock:

| Suite | GTest | pytest | Result |
|---|---:|---:|---|
| CapLab | 19 | 83 | PASS, embedded Python required |
| PlotLab | 8 | 22 | PASS |
| JFM | 59 | 96 + 3 xfail | PASS; 946 existing numerical warnings |
| SST-Libraries | 99 | — | PASS, full combined library suite |

Debug x64 application/tests built in SST-Prototypes/build/; library sources
and the full library test target built in SST-Libraries/build/. No TEMP
fallback. Every build/test was covered by build-lock; MSBuild used
-nr:false -m:1. CapLab pytest retains the existing cache-provider warning.
Final new catalog coverage checks the sourced JSON Si values and owner preset.
The JSON files are in SST-Physics/data/, loaded through the existing
MaterialLibrary merge API and copied beside both application/test executables.
The owner preset is distinct from the library's existing historical preset.

Evidence:
- build/caplab-m3-final.log: final 19 GTest + 83 pytest, app build, smoke attempt.
- build/caplab-m3-plotlab.log and caplab-m3-jfm.log: root regression runs.
- SST-Libraries/build/caplab-m3-build.log, caplab-m3-tests.log/.xml: own build/tests.

Start/exit smoke is PARTIAL, not passed. CapLab.exe was launched once, hidden,
from its output directory; WaitForInputIdle succeeded and the application log
confirmed embedded Python, successful startup and entry into the render loop.
A close request was sent, but the process did not exit within 15 seconds and
was terminated by the smoke script's cleanup. The cause is not established;
no Core source was changed to mask this result. Log:
build/bin/Debug-windows-x86_64/CapLab/logs/CapLab.log.
VISUAL QA NOT RUN. The process/window metadata and logs are not visual QA.

Known gap: graceful application exit has not been verified. Closed question:
Should a separate UI/lifecycle check investigate the failed close request?
Recommendation: yes, with the application visible and the actual native host
window selected. No additional M4-M8 mechanisms were added.


## CM4 / M4 ? AS, defects and both Arrhenius models

Scope: CapLab plus the explicit historical cross-section conversion in
SST-Physics arrhenius.hpp and its existing PhysicsTests.cpp. Existing
SST-Physics transformArrhenius/fitArrhenius, Core Commands/Undo/jobs, W1
EditorWindow and PlotLab-Engine graphs/snapshot PNG export are reused.
No git commands, JFM source/config changes, IS or Walter mechanisms.

Normative references: SST-CapLab Spec sections 4.2/6 M4/8; PFS A1-A7;
ProPho Code Findings Page1_AS.plot_as_data and ASCalc.calc_defect_params.
Expected literals come from those documents and frozen M2 source projects,
never from SST-Physics. A5 compares the entire first saved dC array for each
of the five Cd samples, including the first-order endpoints, with exact array
equality. The PCF five-point logarithmic fixture expects -1e-9/ln(2) F at all
five points. An independent quadratic in ln(f) checks unequal spacing and
first-order endpoints [1,2,6,9]. Optional smoothing operates on sample indices;
its derivative consistently uses ln(f), preserving raw C and D. The historical
helper's log10 SG derivative is not mislabeled as the default ln derivative.

The default synthetic omega=zeta*T^2*exp(-Ea/(kappa*T)) recovers Ea=.32 eV and
zeta=2e7 s^-1 K^-2 within relative 1e-6. Literal SI defining constants generate
the input independently. f->omega preserves energy and multiplies the
corresponding prefactor by 2*pi. Compat sigma explicitly uses Ncv=2e18 cm^-3,
vth=1e7 cm/s; default sigma stays null without positive gamma and its source.
Two distinct temperatures suffice; dof=0 has null covariance and a diagnostic.

Persistent defects/point IDs, manual provenance, measured row references,
enabled masks, models, smoothing settings, actual constants, covariance and
residuals survive project round-trip. Core integration tests cover
create/assign/rename/remove/disable/delete, failed edit atomicity, Undo/Redo,
late worker rejection, input changes, missing inputs and fit invalidation.
Candidates are approved explicitly; the existing renderer supplies screen
picking, while assignment revalidates the candidate/result and measurement.
The test host now flushes pytest output before asserting its exit code.

A7 is an expected source conflict: owner section 8 keeps the thesis normative.
The saved project's point set is retained and reproduced independently;
the algorithm is never adjusted to turn those points into the thesis values.
Full historical compatibility with that thesis entry is therefore unproven.

Root graphify query preceded source navigation. A root graph update was
omitted because the task permits writes only in CapLab and required library
paths while other jobs modify the same repository.


CM4 acceptance (energy in eV; sigma in cm2). The project comparison uses
relative tolerance 1e-6; the more precise literals below pass relative 1e-8.
Small deltas below reflect published decimal precision and the current SI
Boltzmann constant. Each pair is energy; sigma.

| Case | PFS value | Computed | Difference | Verdict |
|---|---|---|---|---|
| A1 | .550614291; 2.506903252e-15 | .550614290715; 2.506903251883e-15 | -2.849e-10; -1.170e-25 | PASS |
| A2 | .146513748; 2.018925754e-17 | .146513747802; 2.018925753531e-17 | -1.978e-10; -4.691e-27 | PASS |
| A3 | .390515655; 1.675548099e-15 | .390515654597; 1.675548098968e-15 | -4.030e-10; -3.207e-26 | PASS |
| A4 | .226592568; 2.637081964e-16 | .226592568496; 2.637081964094e-16 | +4.964e-10; +9.421e-27 | PASS |
| A5 | Full saved dC arrays, delta=0 | All 5 complete arrays identical | 0 | PASS |
| A6 | .489646714 | .489646713933 | -6.715e-11 | PASS |
| A7 project | .156167136; 1.020398281e-17 | .156167135802; 1.020398280872e-17 | -1.984e-10; -1.281e-27 | PASS |
| A7 thesis (owner normative) | .170; 2.54e-17 | .156167135802; 1.020398280872e-17 | -.013832864198; -1.519601719128e-17 | SOURCE CONFLICT retained |
| PCF ln-grid, every point including both edges | -1.4426950408889634e-9 F | Within relative 1e-13 | <1e-13 relative | PASS |
| Unequal ln-grid / quadratic | [1,2,6,9] | [1,2,6,9] within relative 1e-13 | <1e-13 relative | PASS |
| Synthetic Ea; zeta | .32; 2e7 | Recovered within relative 1e-6 | <1e-6 relative | PASS |
| f -> omega | Same Ea; prefactor factor 2*pi | Same Ea; factor 2*pi | <1e-12 relative | PASS |

A2/A4 also pass the PFS +/-0.005 eV thesis comparison (.150/.230 eV).
A1 sigma rounds to the thesis 2.51e-15 cm2. The A7 thesis discrepancy is
-8.137% in energy and -59.827% in cross section; no fit tuning is applied.
Candidate graphs put the candidate series first so equal-distance screen
picks prefer approved candidate markers over overlapping raw data points.


Final CM4 execution (2026-11-18), from the SST-Prototypes root, every build/test
under bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh:

| Suite | GTest | pytest | Result |
|---|---:|---:|---|
| CapLab | 20 | 101 | PASS, embedded Python required; no skips |
| PlotLab | 8 | 22 | PASS |
| JFM | 59 | 96 + 3 xfail | PASS, 946 existing numerical warnings |
| SST-Libraries | 100 | - | PASS, full combined library suite |

The filtered embedded host reruns the embedded GTest to run pytest; it is
counted once in the 20 GTests. The last CapLab run includes the candidate
series priority check. Existing pytest cache-provider warning remains.

Debug x64 application/tests built in SST-Prototypes/build/; SST-Libraries
built/tested in SST-Libraries/build/. No TEMP fallback. Every MSBuild invocation
used -nr:false -m:1. No git commands or commits were run.

Evidence:
- build/caplab-m4-final.log: final CapLab 20 GTest + 101 pytest.
- build/caplab-m4-app-final.log: final application build.
- build/caplab-m4-plotlab.log and caplab-m4-jfm.log: root regression runs.
- build/caplab-m4-A1.json through A4.json and A7.json: numerical comparisons.
- SST-Libraries/build/caplab-m4-build-tests.log and caplab-m4-tests.xml:
  own Debug build and full 100-test run.

Start/exit is NOT VERIFIED. One launch request for the built CapLab.exe via
the Computer Use skill's sky.launch_app was rejected by automatic access
review: "Computer Use was not approved to use caplab". No fallback app control
was used after that rejection. There is no successful CM4 launch/exit claim.
VISUAL QA NOT RUN. M3's earlier exit timeout remains unresolved.

Closed questions:
- Enable Computer Use access to CapLab for the required start/exit check?
  Recommendation: yes; this is the remaining runtime acceptance blocker.
- Obtain the exact source point selection behind the thesis A7 entry?
  Recommendation: yes; preserve both references and the current fit meanwhile.

M5-M8 were not implemented. Runtime/visual acceptance and A7 thesis agreement
remain the known limitations of this M4 delivery.
