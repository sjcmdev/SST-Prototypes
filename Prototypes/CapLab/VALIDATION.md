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


## CM5 / M5 ? CircuitCore-UI i symulacja IS (2026-11-18)

Implementacja M5 jest gotowa; odbi?r runtime pozostaje zablokowany przez dost?p
Computer Use. Bez komend git i bez commit?w. Nie zmieniano ?r?de? CircuitCore,
CircuitCore-UI, Core, JFM ani JFM/install/app/config/*.yaml.

Zakres: CapLab oraz niezb?dne zmiany bibliotek: SST-Physics impedance.hpp,
PlotLab-Engine persistence, flaga equalAspect we wsp?lnym UI-PlotSpec/rendererze
i ich istniej?ce testy. Bez nowego optimizera, hosta GLFW ani mechanizm?w M6?M8.
Model/widget/compileDocument i selfCheck CircuitCore-UI s? kompilowane wprost
z istniej?cych ?r?de? repozytori?w siostrzanych. Istniej?cy model SPICE Cj(V)
i IJunctionCapacitance CircuitCore zosta?y ponownie u?yte, nie skopiowane.

Przeczytano ca?? SST-CapLab Spec, PFS, README/VALIDATION, raporty c1/cm2/cm4
oraz Core/Presentation/WINDOWS.md. W Vault 80_AI/Codex Jobs.md jest wpis d1
2026-11-18 06:12 z odno?nikiem [[CircuitCore-UI]], ale brak docelowej notatki;
wyszukiwanie nazw i tre?ci ca?ego Vault nie odnalaz?o raportu. Bie??cy
runs/d1-report.md dotyczy scaps_core, wi?c nie zast?puje tego ?r?d?a. U?yto
aktualnego README CircuitCore-UI, kodu modelu/runnera i jego rzeczywistych
self-checks. Brak notatki pozostaje jawn? luk? dokumentacyjn?.

Przep?yw: Core command -> immutable snapshot/revision -> Core job ->
main-thread acceptance -> document/Undo -> wsp?lny PlotLab graph/snapshot.
API caplab.execute przez nanobind wykonuje te same commands/jobs headless;
wej?cia/wyj?cia s? JSON, b??dy maj? kody, wyniki jawne jednostki i trwa?e ID.
SchematicEditor nie uruchamia solvera podczas renderowania. Anulowanie jest
sprawdzane mi?dzy cz?stotliwo?ciami. Edycja w trakcie pracy odrzuca wynik
starej rewizji; zmiana/usuni?cie schematu daje stale/missing_input.

Oczekiwania pochodz? z r?wna? analitycznych M5 / PFS (Capacitance Spectroscopy
of Thin-Film Solar Cells, s.94?96) oraz decyzji 8.4. ?aden expected value nie
jest generowany przez testowany kernel. R=20 Ohm, C=20 uF; Randles R0=20 Ohm,
R1=500 Ohm, C1=20 uF. Dok?adna, celowo nier?wnomierna i nieuporz?dkowana
siatka testu C++: [1234.5, .001, 7.25, 94001, 9e7] Hz. T=270 K i bias=-.15 V
s? zachowane z pomiaru; brakuj?ce warunki wymagaj? jawnego potwierdzenia.

W tabeli numerycznej nasze = maksymalny b??d wzgl?dny, diff = nasze minus limit.
Wszystkie por?wnania dotycz? punkt?w poza osobliwo?ciami.

| Test | Spec value | Ours | Diff | Verdict |
|---|---:|---:|---:|---|
| R: Z=R | <=1e-8 | 0 | -1e-8 | PASS |
| C: Z=1/(j omega C) | <=1e-8 | 0 | -1e-8 | PASS |
| Szeregowy RC: Z=R+1/(j omega C) | <=1e-8 | 2.66911e-11 | -9.9733089e-9 | PASS |
| Randles: Z=R0+1/(1/R1+j omega C1) | <=1e-8 | 3.0608e-15 | -9.9999969392e-9 | PASS |
| Y -> Z -> Y | <=1e-12 | 2.01946e-16 | -9.99798054e-13 | PASS |
| Schemat/port/parametry/layout/ID po save/open | identyczne | identyczne | 0 | PASS |
| Edycja podczas oblicze? | odrzucenie starego wyniku | odrzucony | 0 | PASS |
| Nieprawid?owy port / singular circuit | diagnostyka | port/conflict/singular_matrix | 0 | PASS |
| Istniej?ce CircuitCore-UI self-checks | PASS | PASS | 0 | PASS |
| Responsywno?? GUI | worker, bez blokowania UI | async command/edit potwierdzone headless; GUI nieobejrzane | n/a | RUNTIME NOT VERIFIED |
| Start/exit aplikacji | uruchomienie i zamkni?cie | launch odrzucony przez Computer Use | n/a | BLOCKED |

Dodatkowo przesz?y: Cp/Rp oraz C/G i rzeczywisty znak Im Z; zero Y, z?y Rp,
null z kodem przyczyny; b??dne jednostki schematu; wymagane T/bias i SI;
anulowanie po pierwszym punkcie; rzeczywista osobliwo?? AC wewn?trznej ga??zi LC
przy rezonansie; konflikty idealnych ?r?de?; nieudana edycja bez zmiany projektu;
Core Undo/Redo edycji i przyj?cia wyniku; missing_input bez przepinania ID.

Obie diody PN/Schottky: T=250/350 K, bias=-2.4/0/.32/.48 V; testy sprawdzaj?
reverse depletion, granic? FC, liniowe przed?u?enie i tau*dI/dV. Niezale?ne
expected Cj to 1e-9, 2e-9, 2.581988897471611e-9, 3.012320380383546e-9 F,
z tolerancj? absolutn? 1e-15 F po dodaniu analitycznej pojemno?ci dyfuzyjnej.
Podmiana modelu przez IJunctionCapacitance daje zadane 3e-9 F w prawdziwym
CircuitCore impedance solve. Wszystkie pi?? parametr?w diody przechodzi JSON
self-check. Registry ?yje d?u?ej ni? CompiledCircuit i wszystkie solves.

Nyquist/Bode/C-f/G-over-omega i reszty s? obiektami PlotLab-Engine. Testy
wsp?lnego renderera potwierdzaj? r?wn? fizyczn? skal? osi (bez GUI backendu);
persistence i snapshot zachowuj? t? w?a?ciwo??. Nie jest to visual QA.

Ko?cowa walidacja, uruchomiona w tym zadaniu z SST-Prototypes root:

| Suite | GTest | pytest | Verdict |
|---|---:|---:|---|
| CapLab | 26 | 107 | PASS, real embedded Python required |
| PlotLab | 8 | 22 | PASS |
| JFM | 59 | 96 + 3 xfail | PASS |
| SST-Libraries (pe?ny combined suite) | 103 | ? | PASS |

Filtered embedded host s?u?y do pytest i nie jest liczony ponownie jako GTest.
CapLab zachowuje istniej?cy pytest cache-provider warning; JFM ma 946 istniej?cych
ostrze?e? numerycznych. Wszystkie buildy i testy wykonano kolejno przez
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh, MSBuild
-nr:false -m:1. Debug x64 app/tests: SST-Prototypes/build/; w?asny library
build/tests: SST-Libraries/build/. Nie by? potrzebny fallback TEMP.

Dowody: build/caplab-m5-final.log (26/107, po ko?cowej walidacji jednostek),
build/caplab-m5-app.log, build/caplab-m5-plotlab.log, build/caplab-m5-jfm.log;
SST-Libraries/build/caplab-m5-build.log, caplab-m5-tests.log i caplab-m5-tests.xml.
Graphify query poprzedzi?o nawigacj?; graphify update . od?wie?y?o graf AST.
Ostrze?enia graphify dotycz? istniej?cych plik?w danych, .m i brakuj?cego parsera
SQL; nie instalowano dodatkowych zale?no?ci.

Start/exit: wykonano jedn? pr?b? sky.launch_app dla w?a?nie zbudowanego
build/bin/Debug-windows-x86_64/CapLab/CapLab.exe. Automatic access review
odrzuci? akcj?: "Computer Use was not approved to use caplab". Proces nie zosta?
uruchomiony t? akcj?; nie stosowano alternatywnego sterowania po odmowie.
Nie potwierdzono czystego zamkni?cia ani responsywno?ci rzeczywistego okna.
VISUAL QA NOT RUN.

Pytania zamkni?te:
- Czy udost?pni? CapLab przez Computer Use, aby domkn?? start/exit i GUI smoke?
  Rekomendacja: tak; to pozosta?y blocker odbioru runtime M5.
- Czy uzupe?ni? brakuj?c? notatk? CircuitCore-UI z d1 06:12 w Vault?
  Rekomendacja: tak, dla kompletnego provenance; bie??cy kod/self-checks s? sprawdzone.

Pomini?to M6?M8 zgodnie z zakresem. Ryzyko: testy headless nie zast?puj? odbioru GUI.

## CM6 / M6 — IS circuit fitting (2026-11-18)

Implementacja i odbiór numeryczny M6 gotowe; odbiór runtime zablokowany.
Zmiany zadania obejmują tylko CapLab. Bez komend git, zmian źródeł Core/JFM,
JFM/install/app/config/*.yaml, CircuitCore i CircuitCore-UI oraz bibliotek.
Nie dodano zależności, nowego optimizera ani mechanizmów M7/M8.

Przeczytano SST-CapLab Spec w całości (zakres §6 M6, §4.3 oraz wiążące §8),
ProPho Functional Spec, README/VALIDATION, raporty c1/cm2/cm5 i W1 WINDOWS.md.
Graphify query poprzedziło nawigację. Aktualizacji głównego graphify-out nie
wykonano: wykracza poza wskazane ścieżki zapisu zadania i obejmuje zmiany
równoległych prac.

FitIS używa istniejących CircuitCore-UI/CircuitCore oraz SST-Math SANelderMead.
Objective pozostaje lokalnie w CapLab, zgodnie z §4.3; nie dodano kerneli
fizycznych. Ponownie użyto źródłowych konwersji SST-Physics oraz modelu SPICE
Cj(V) CircuitCore. Cytowania: SST-CapLab Spec §4.3/§8.4; PFS K s.94–96.
Wyniki oczekiwane testów powstają z niezależnego idealnego równania
Z=R0+1/(1/R1+j*2*pi*f*C1), nigdy z SimulateIS/FitIS.

Przepływ: Core command -> detached snapshot/revision -> cancellable Core job
-> draft -> jawna akceptacja przez istniejący caplab.accept_analysis/Core Undo.
Akceptacja zmienia obwód i analizę jako jeden wpis Undo; Undo/Redo przywracają
pełny projekt. Edycje odrzucają stare wyniki lub starą akceptację. Stop/discard
używa caplab.cancel; anulowanie nie nadpisuje zaakceptowanych parametrów.

GUI: free/fixed/bounds, starty ze schematu, zakres f, jawny floor, start/stop,
przegląd parametrów i draftu, reszty oraz akceptacja. Wykresy korzystają z tego
samego PlotLab-Engine/renderer/snapshot, z osobnym stanem osi draftu.
Obsługiwane parametry R/C/L/CPE/Warburg oraz PN/Schottky, w tym Cj0/Vbi/m/FC/tau.
Niezależne źródła V/I blokują fit. Parametry wskazuje nazwa elementu i pole;
jednostki są jawne i zgodne ze schematem. Współrzędne simpleksu są skalowane,
a zerowe starty przesunięte, aby umożliwić użyteczny pierwszy krok.

Default objective = sum((Re residual)^2+(Im residual)^2)/scale_Ohm^2,
scale_Ohm=max(abs(measured Z), Z_floor_Ohm). Opcjonalnie sigma_Ohm na oryginalny
wiersz pomiaru. Wybrana siatka używa dokładnych zmierzonych f/T/bias i trwałych
row IDs. Początkowy nieważny punkt blokuje fit; nieważne punkty próbne dostają
infinite objective i są zliczane, bez publikowania pozornych wyników.

Wynik zapisuje weighting/floor/scales, starty, free/fixed/bounds i wartości
fitted z jednostkami, initial/final score, stop_status (score_tolerance albo
max_iterations), iteracje, invalid_evaluations, reszty i oba schematy. Score
nie jest nazywany statystycznym chi-squared. Covariance=null; brak niepewności
nie jest prezentowany jako zero. Kontrakt zapisu waliduje te metadane.

Headless caplab.execute przez rzeczywisty nanobind korzysta z tych samych
commands/jobs/Undo. operation=fit zwraca niezmieniony project i draft z projektem
bazowym; operation=accept_fit akceptuje go po kontroli aktualności. Wejścia
oraz wyjścia są JSON; błędy mają stabilne kody fit_failed/cancelled/invalid_request.
`cancel: true` w żądaniu headless anuluje pracę przed uruchomieniem kolejki;
GUI pozwala zatrzymać trwającą pracę. Odrzucenie headless draftu to pominięcie
akceptacji. Pełny przykład i kontrakt znajdują się w README.md.

Akceptacja: C++ Randles R0=20 ohm, R1=500 ohm, C1=20e-6 F; ustalony start
35 ohm / 300 ohm / 35e-6 F; 31 punktów f=10^(-1+0.2*i) Hz, i=0..30.
Zaszumiony przypadek: mt19937 seed=6006, niezależny Gaussian noise każdego
składnika Re/Im o sigma=0.005*abs(Z). Testy Python mają osobny seeded generator.

| Akceptacja | Spec value | Ours | Diff | Verdict |
|---|---|---|---|---|
| Randles bez szumu, max względny błąd R/C | <1e-3 | 2.59505e-7 | -9.99740495e-4 względem limitu | PASS |
| Seedowany szum, final score < initial score | <9.46177845842543 | 0.0012929479664675193 | -9.460485510458963 | PASS |
| Fixed R0 | 20 ohm bez zmiany | 20 ohm | 0 | PASS |
| Bounds | każdy fitted w [min,max], C1 min=30e-6 F | zachowane | 0 naruszeń | PASS |
| Zły punkt początkowy / próbny | blokada / kara bez publikacji | blokada / invalid_evaluations>0, valid final | zgodne | PASS |
| Brak free parameters | jawny błąd bez zmiany projektu | fit_failed | zgodne | PASS |
| Anulowanie | brak nadpisania zaakceptowanych parametrów | zachowane; brak draftu/commit | 0 zmian | PASS |
| Draft, akceptacja, Undo/Redo, stale | jawny commit, 1 Undo, stare odrzucone | potwierdzone | zgodne | PASS |
| Weighting/floor/stop/covariance | zapisane; covariance unavailable | zapisane; null | zgodne | PASS |
| PN/Schottky Cj0 ze startu 0 F | odzyskanie 2e-9 F (independent SPICE at V=0) | w tolerancji względnej 1e-3 | <1e-3 względnie | PASS |
| Start/exit | start i czyste wyjście | launch odrzucony przez kontrolę dostępu | n/a | BLOCKED |

Dodatkowo: jednostki, sigma, dodatni floor, nieprawidłowe bounds/start,
nieobsługiwane źródła, domkni?ty zakres f, brak punktów w zakresie,
round-trip projektu, walidacja uszkodzonych metadanych i typed errors.
Anulowanie sprawdzono w trakcie iteracji, przez ??danie z drugiego w?tku Python
oraz w kolejce po uprzedniej akceptacji wyniku; stary projekt pozostaje identyczny.

Wszystkie poniższe buildy/testy wykonano w tym zadaniu z SST-Prototypes root,
przez bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh.
MSBuild -nr:false -m:1. Debug x64 w SST-Prototypes/build/; zależności we
wspólnym SST-Libraries/build/. Bez fallbacku TEMP. Niezgodny współdzielony
Core.pch wymagał odświeżenia artefaktu pod lockiem; nie zmieniano źródeł Core.

| Suite | GTest PASS | pytest PASS | Uwagi |
|---|---:|---:|---|
| CapLab | 33 | 116 | --require-embedding; 1 cache_dir warning |
| PlotLab | 9 | 22 | --require-embedding |
| JFM | 67 | 96 | 3 xfail, 946 numerical warnings |

Końcowy przebieg CapLab po poprawce zerowego startu ponownie przeszedł
32 GTest + 114 pytest; filtrowany host embedded nie jest liczony drugi raz.
Pełnego osobnego suite SST-Libraries nie uruchamiano: brak zmian ich źródeł.

Dowody:
- build/caplab-m6-final.log — końcowe testy CapLab i build CapLab.exe.
- build/caplab-m6-plotlab.log — pełna regresja PlotLab.
- build/caplab-m6-jfm.log — pełna regresja JFM.

Próba start/exit: po końcowym buildzie wysłano jeden launch request przez
Computer Use dla build/bin/Debug-windows-x86_64/CapLab/CapLab.exe.
Automatyczna kontrola dostępu odrzuciła go komunikatem:
"Computer Use was not approved to use caplab". Nie uruchamiano alternatywnego
sterowania ani smoke helpera w celu obejścia odmowy. Nie ma potwierdzonego
startu ani wyjścia aplikacji w tym zadaniu. VISUAL QA NOT RUN.

Otwarte pytanie zamknięte: czy udostępnić CapLab w Computer Use do wymaganego
start/exit i odbioru GUI fitu? Rekomendacja: tak. Runtime pozostaje jedynym
blockerem odbioru M6; M7/M8 i aktualizacja głównego graphify-out są poza
ścieżkami/zakresem tej zmiany.

## CM7 — Walter spectra (2026-11-18)

Zakres: §6 M7 oraz §4.4; wiążące decyzje §8 mają pierwszeństwo.
Dwa modele: Walter1996-linear-field (domyślny, eq.25) i ProPho-compat
(pierwiastek / 2*Vb^1.5, PCF). Fizyczne kernele i całkowanie są w
SST-Libraries/SST-Physics/include/sst/physics/walter.hpp. Cytowania:
Walter et al., J. Appl. Phys. 80, 4411–4420 (1996), eq.20/25,
https://paperzz.com/doc/9224551/kirchartz1_walter96jap;
ProPho Code Findings, WaltCalc calc_A / omega_to_E / calculate_concentration;
PFS Walter oraz SST-CapLab Spec §4.4/§8.

Python przelicza C/area, maskuje f/row IDs przed gradientem po ln(omega),
omega=2*pi*f. Domyślny prefaktor to zeta [s^-1 K^-2] razy T²; compat B [s^-1]
jest stałe dla wybranych temperatur, z jawnym źródłem i bez automatycznego
wyprowadzania z historycznego fitu. Zapis obejmuje stałe SI, model/version,
rewizję, pełne snapshoty wejść/fitów, T/f/bias, source/policy/description x_d,
energię [eV], density [cm^-3 eV^-1], gradient [F/m2], factor [V/(m C)],
quality, interval/semantics/integral [cm^-3].

Wybór x_d współdzieli helper z CV/DLCP: CV przy bias AS, profil DLCP lub fit
raw DLCP przy V=0, albo manual. Block/linear/nearest pozostają jawne i zapisane;
linear nie ekstrapoluje. Wybrany fit CV dostarcza Vb/EF i domyślny pomiar oraz
materiał dla width. Wybrany omega-T2 Arrhenius dostarcza zeta. Braki parametrów,
niezgodna próbka, stare/missing fity i konflikt potwierdzonego bias są błędami.
Wynik bez ważnej dziedziny zachowuje null/reason zamiast pozornej liczby;
ujemne widma pozostają ujemne. Default całkuje rosnące E z interpolacją granic
bez łączenia nieważnych przerw. Compat wykonuje -trapz po przekazanej kolejności,
po wyborze punktów w przedziale, bez interpolacji. Nieważny compat wycinek
nie zwraca pozornej całki; default raportuje luki/liczbę ważnych segmentów.

Panel na W1 EditorWindow: T/f/E, model, serie, fit CV/defektu, x_d, prefaktor,
zakres całki, nakładane widma i szczegóły invalid points. Ten sam PlotLab-Engine
zachowuje null jako przerwę linii i obsługuje istniejący snapshot PNG export.
Nowe publiczne operacje caplab.execute(operation=walter / integrate_saved_walter)
używają rzeczywistego nanobind i tego samego Core command -> cancellable job ->
main-thread acceptance / Undo. Integracja saved wybiera rekord przez trwały
sample_id (istniejący model ma jeden historyczny rekord na importowaną próbkę);
nie wprowadzono osobnej migracji ID. Zmiana/usunięcie wejścia lub fitu daje
stale/missing_input; późny worker nie nadpisuje nowej rewizji.

Naprawa lokalnego root cause headless: ReadAnalysis bierze standardowy GIL scope
nanobind zamiast ponownie blokować mutex zewnętrznego Core PythonGilAcquireScope.
Headless caller zwalnia GIL na czas joba, lecz może nadal trzymać ten mutex.
Pozostawienie poprzedniego wywołania powodowało deadlock Python-backed jobów.
Core, JFM i inne aplikacje nie zostały zmienione przez CM7.

Oczekiwania testów pochodzą z literalnych równań i stałych SI z §4.4/PCF oraz
PFS W1–W3; nie pochodzą z kernela testowanego. W1–W3 używają wyłącznie
zamrożonych walter.dist_for_integration, osobno od nowych synthetic spectra.
Manifest m7/sources.json zapisuje hashe źródeł i wycinków, liczbę punktów,
przedział, wartości PFS i tolerancję 1e-4 względnie. Owner §8 dopuszcza taką
ograniczoną zgodność v1. Pełna rekonstrukcja historycznych widm/wyborów GUI
pozostaje jawnie unsupported; brak wycinka blokuje tę operację. W3 zawiera
techniczny wzorzec projektu, bez przypisywania mu wartości z tabeli pracy.

Testy M7: niezależne oba współczynniki i J/eV/m/cm, T² i 2pi; c=a+b ln(omega),
PCF B i gradient po maskowaniu; 4e14 i zmiana znaku po odwróceniu compat;
interpolowane granice default, nieważne przerwy i zero/ujemny mianownik;
trzy źródła x_d, trzy polityki, same-sample/no extrapolation, raw DLCP fit;
T overlay/filter, źródła CV/Arrhenius i brak domyślnego B; headless JSON,
typed errors/cancel, save/open, Undo/Redo, stale revision i dependency removal;
walidacja uszkodzonego wyniku oraz zamrożone W1–W3 i ich hashe.

| Akceptacja CM7 | Spec value | Ours | Diff | Verdict |
|---|---:|---:|---:|---|
| c=a+b ln(omega), PCF b [F/m2] | -1e-6 | -1e-6 | <1e-19 | PASS |
| Oba factor/density, SI/T²/2pi | niezależna arytmetyka §4.4 | zgodność rel. 1e-13 | <1e-13 rel. | PASS |
| PCF B, energia środkowa [eV] | 0 | 0 (tol. 1e-15) | <1e-15 | PASS |
| -trapz [0.3,0.2,0.1] eV [cm^-3] | 4e14 | 4e14 | <4 | PASS |
| Odwrócony compat / default [cm^-3] | -4e14 / +4e14 | -4e14 / +4e14 | <4 | PASS |
| W1 saved slice, 9 punktów [cm^-3] | 475105311000000 | 475105311003778.2 | +3778.1875 (7.95e-12 rel.) | PASS |
| W2 saved slice, 9 punktów [cm^-3] | 610460254600000 | 610460254605720.6 | +5720.625 (9.37e-12 rel.) | PASS |
| W3 saved slice, 19 punktów [cm^-3] | 4138087352000000 | 4138087352385057 | +385057 (9.31e-11 rel.) | PASS |
| Invalid domain / gap | null + reason, bez mostkowania | zachowane | 0 pozornych wartości | PASS |
| Pełne przeliczenie historycznych W1–W3 | unsupported zgodnie z owner §8 | unsupported | zgodne | OGRANICZONY ZAKRES |

CapLab: pełny Debug build w SST-Prototypes/build/, 33 GTest PASS i 155 pytest
PASS (39 nowych przypadków M7), embedding wymagany. Jeden istniejący warning
pytest cache_dir. Dowód: build/cm7-caplab-final.log; wartości W1–W3:
build/caplab-m7-W1.json, caplab-m7-W2.json, caplab-m7-W3.json.
Przerwane wcześniejsze przebiegi (deadlock przed lokalną poprawką GIL i brak
własnego ID historycznego rekordu) nie są liczone jako zaliczone testy.

Końcowa regresja i build CM7 (wszystko uruchomione z SST-Prototypes root,
każdy build/test pod tools/agents/build-lock.sh; MSBuild -nr:false -m:1):

| Suite | GTest PASS | pytest PASS | Uwagi |
|---|---:|---:|---|
| CapLab | 33 | 155 | --require-embedding; 39 nowych przypadków M7 |
| PlotLab | 10 | 22 | --require-embedding |
| JFM | 71 | 96 | --require-embedding; 3 xfail, 946 warnings |
| SST-LibrariesTests | 107 | — | 3 nowe testy Walter; pełny target Math/Physics/UI/PlotLab |

Zbudowano Debug x64 w SST-Prototypes/build/ oraz SST-Libraries/build/.
Nie użyto fallbacku TEMP. Normalny build CapLab.exe z project references
zakończył się powodzeniem. Po równoległych zmianach Core powtórzono także
CapLab. Zastany niezgodny Core.pch (C1853) odtworzono wyłącznie z artefaktów
build/ pod blokadą; nie edytowano źródeł Core. Przejściowy błąd linkowania
PlotLab (nowe RegisterApplicationPythonModule z równoległej pracy) zniknął
po ukończeniu/odbudowie współdzielonego Core i powtórzeniu zwykłego builda.

Dowody:
- build/cm7-caplab-final.log — CapLab build, pełne GTest i embedded pytest.
- build/cm7-plotlab.log — PlotLab build i pełne GTest/pytest.
- build/cm7-remaining.log — JFM build/test, standalone SST-Libraries build/test,
  normalny build aplikacji CapLab; bez równoległych kompilacji/testów.
- SST-Libraries/build/cm7-tests.xml — 107 zaliczonych testów bibliotecznych.
- build/caplab-m7-W1.json / W2.json / W3.json — rzeczywiste różnice akceptacji.

Graphify: wykonano graphify update . (AST-only, bez API); zapisane graph.json /
GRAPH_REPORT.md: 75170 nodes, 147510 edges. build/cm7-graphify.log zawiera
ostrzeżenia o plikach bez ekstraktora (.m), brakującym tree_sitter_sql i pustych
węzłach JSON; graph.html pominięty ze względu na limit rozmiaru. Aktualizacja
została zapisana, choć PowerShell oznaczył stderr warnings jako błąd polecenia.

Start/exit: po udanym buildzie wysłano jeden request sky.launch_app z pełną
ścieżką build/bin/Debug-windows-x86_64/CapLab/CapLab.exe (Computer Use skill).
Automatyczna kontrola odrzuciła go: "Computer Use was not approved to use caplab".
Nie obchodzono odmowy shellowym launcherem ani innym sterowaniem UI.
Nie ma potwierdzonego startu ani czystego wyjścia w CM7. VISUAL QA NOT RUN.

Znane luki: start/exit i odbiór wizualny zablokowane dostępem; pełne przeliczenie
historycznych widm ProPho pozostaje unsupported zgodnie z owner §8 (v1 obejmuje
całki zapisanych widm). M8 CSV/XLSX/end-to-end workflow jest poza zakresem.
Czy udostępnić CapLab w Computer Use do domknięcia start/exit i visual smoke?
Rekomendacja: tak. Nie ma otwartych pytań o model fizyczny ani politykę x_d.
Nie uruchamiano komend git; nie zmieniano źródeł Core/JFM/CircuitCore ani
Prototypes/JFM/install/app/config/*.yaml.

Ko?cowa kontrola po aktualizacji wsp??dzielonego Core: 33 GTest i 155 pytest
PASS (56.22 s), build/cm7-caplab-final.log. Ostatni graphify update ponownie
zapisa? aktualny graf (75170 nodes / 147510 edges / 2171 communities).


## CM8 / M8 ? v1 workflow, export and Release regression (2026-11-18)

M8 workflow and automated acceptance are implemented. Full milestone acceptance
remains incomplete because runtime launch and visual smoke were denied access.
Read the whole CapLab spec (binding owner ?8), PFS, README/VALIDATION, c1/cm2/cm7
reports and W1 WINDOWS.md. Graphify query preceded navigation. No git commands,
commits, source Core/JFM/CircuitCore/library edits or JFM YAML edits were issued.
The only product source changes are in CapLab; graphify artifacts are updated
under the repository's mandatory AST update rule. No new physics kernel or
optimizer was needed: M3?M7 kernels, Core commands/jobs/Undo, W1 EditorWindow
and PlotLab-Engine snapshots/export are reused.

Completed:
- CSV/XLSX data export through caplab.export -> immutable project -> Core job.
  Both include original values, explicit units, stable IDs, models, parameters,
  residuals/covariance, masks and full provenance. CSV uses JSON-pointer/value_json
  rows. XLSX includes the same lossless Metadata rows and numeric measurement
  worksheets with unit-bearing headers; row IDs are text to preserve 64-bit IDs.
  M1 auxiliary columns and original source/hash metadata are retained.
- XLSX uses standard-library ZIP/XML OOXML inline strings, with no new dependency.
  Formula-like imported text stays text. Excel row/column/text limits fail with
  an explicit CSV alternative. CSV/XLSX preserve the old file on errors/cancel;
  replacement is atomic. PNG continues to use the shared atomic exporter.
- Native headless execute operations analyze(CV/DLCPPolynomial/AS/Arrhenius),
  edit_defect, area, delete_measurement, save, open and export. These use the same
  commands/jobs as the GUI; no alternate calculation path. Requests/results are
  JSON, references are stable IDs and errors have codes. Headless PNG requires
  explicit resolved ranges and accepts graph_id plus per-plot visibility.
- Measurement deletion uses the existing undoable document-change command.
  Removed inputs remain referenced by their original IDs; dependent results become
  missing_input. Archived engine curves and historical snapshots are retained.
  Undo/Redo restores the full project and statuses. Area/point edits propagate
  stale status; manual T/f Arrhenius correctly remains valid after an area change.
- Full P1 synthetic workflow: import CV and raw DLCP with AS, fit both, approve
  defect points, fit Arrhenius, select CV/Arrhenius inputs for Walter, save/open,
  CSV/XLSX and PNG. Full IS workflow: independent analytic Randles measurement,
  simulation, fit draft, explicit acceptance, save/open with all inputs/results.

| Acceptance | Spec value | Ours | Diff | Verdict |
|---|---|---|---|---|
| P1 | Preserve data/IDs/relations/models/ranges/results after save/open | Full project JSON equality | 0 changed fields | PASS |
| P2 deletion | Missing inputs retain IDs; no rebinding | AS/Walter missing_input; source IDs retained | 0 rebound references | PASS |
| P2 Undo/Redo | Restore original/edited document and statuses | Native Core Undo/Redo exact serialized equality | 0 changed fields | PASS |
| Parameter/point edits | Dependent analyses stale | CV/DLCP/AS/Walter stale after area edit; manual Arrhenius valid; point edit invalidates Arrhenius/Walter | 0 incorrect statuses | PASS |
| E1 XLSX | Values, units, models, parameters, provenance | Metadata equals lossless CSV; headers Hz/F/Ohm; raw first row 100 Hz / 3e-12 F / 1e6 Ohm | 0 value/metadata differences | PASS |
| E1 PNG | Same visible series/ranges/styles as snapshot | Worker snapshot equals renderer input; actual figure limits equal supplied limits, visibility by plot ID | 0 snapshot differences | PASS (automated) |
| Synthetic CV | Vb=0.8 V; N=1e21 m^-3 | Both within relative 1e-6 | <1e-6 relative | PASS |
| Synthetic IS fit | R0=20 Ohm; R1=500 Ohm; C1=20e-6 F | Every accepted fitted value within relative 1e-3 | <1e-3 relative | PASS |
| Missing runtime/export dependency, failed save/export | Explicit errors; preserve source/destination/document | Native missing-runtime error; injected PNG dependency error; save/open/export_failed; old files preserved | 0 silent successes | PASS |
| Release / embedded runtime | Release builds and C++/Python regression | All required suites passed with --require-embedding | 0 failed tests | PASS |
| Start/exit and visual smoke all modules | Start, clean exit, inspected GUI | Computer Use launch denied before start | Not measured | BLOCKED |

Frozen sources:
- tests/fixtures/m8/sources.json indexes data_Cd, data_IDUB, data_scaps_ref and
  synthetic tables, SHA-256 hashes, independent expected-value citations and
  tolerances. It links prior M1/M2/M7 manifests rather than copying those sources.
- m8/synthetic.json freezes literal CV values and full independently generated
  raw DLCP and analytic Randles arrays, expected parameters and equations. Tests
  compare imported data with these arrays before fitting. No expected quantity
  is calculated by an SST kernel under test.
- The full existing PFS I1?I5, D1, C1?C5, A1?A7 and W1?W3 suites ran again.
  W1/W2/W3 saved-integral relative differences remain 7.95e-12 / 9.37e-12 /
  9.31e-11 (limit 1e-4). Owner ?8 limits historical Walter compatibility to
  saved-spectrum integrals; historical full-spectrum reconstruction is unsupported.
  The inherited A7 project/thesis conflict remains explicit (0.156167136 versus
  0.170 eV); M8 neither invents missing source points nor changes that result.

Final validation was run from the SST-Prototypes root, each build/test wrapped
by bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh. MSBuild used
-nr:false -m:1. Release x64 outputs are in SST-Prototypes/build/; referenced
library targets were built in SST-Libraries/build/. No TEMP fallback was needed.
No standalone library suite was run because no library source was changed.

| Suite | C++ PASS | pytest PASS | Other |
|---|---:|---:|---|
| CapLab | 34 | 164 | --require-embedding; 1 existing cache_dir warning |
| PlotLab | 10 | 22 | --require-embedding |
| JFM | 71 | 96 | --require-embedding; 3 xfail, 946 numerical warnings |

CapLab adds one native missing-runtime test and nine embedded pytest cases.
The filtered EmbeddedRoundtrip invocation hosting pytest is not counted twice.
Native integration also checks delete -> missing_input -> Undo/Redo and exact
project restoration. Tests independently read CSV and XLSX ZIP/XML, including
raw numeric worksheet values; render real PNGs for every result graph in CV,
DLCP, AS, Arrhenius, Walter and IS; compare snapshots/visibility and figure limits;
check cancellation and injected replacement/dependency failures. PNG bytes and
figure assertions are automated checks, not visual QA. XLSX was not opened in
Microsoft Excel during this job.

Exact commands:
```
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --app --configuration Release
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --require-embedding --configuration Release
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py PlotLab --require-embedding --configuration Release
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py JFM --require-embedding --configuration Release
```

Evidence:
- build/cm8-app.log ? successful normal Release CapLab.exe build with references;
  installed caplab/exporter.py SHA-256 matched source after deployment.
- build/cm8-caplab-final.log ? final 34 GTest / 164 embedded pytest PASS after
  the last source/fixture/test edits. Earlier failed assertions/build attempts
  are superseded by this complete successful run.
- build/cm8-plotlab.log and build/cm8-jfm.log ? full Release regressions.
- build/caplab-validation/CapLab/tmp/ ? generated P1 CSV/XLSX and per-graph PNGs.
- build/caplab-m7-W1.json / W2.json / W3.json ? regenerated independent PFS
  saved-integral comparisons from the current full CapLab run.
- build/cm8-graphify.log and cm8-graphify-final.log ? mandatory AST-only update;
  warnings mention missing SQL extractor, unsupported .m files, zero-node JSON
  and oversized HTML visualization. Check the final graph-written message;
  PowerShell treats these stderr warnings as an error even when graph is saved.

Runtime attempt: after the successful Release app build, one Computer Use
sky.launch_app request targeted
D:/SJCM/01_SemiconductorStudio/SST-Prototypes/build/bin/Release-windows-x86_64/CapLab/CapLab.exe.
Automatic access review rejected it: "Computer Use was not approved to use caplab".
The process was not confirmed started; clean exit could not be checked. No shell
launcher or alternate UI control bypassed the rejection. VISUAL QA NOT RUN.

Closed question: should CapLab be enabled in Computer Use for start/exit and
visual smoke of all modules? Recommendation: yes; this is the remaining M8
runtime acceptance blocker. No new physics/model decision is needed.

## CLSCI1 science acceptance (2026-11-19)

A7 source investigation is frozen in `python/tests/fixtures/a7_sources.json`,
with original file SHA-256, JSON pointers and every source point (including duplicates).
M p.46 Table 4.2, anneal 250 C, reports 0.170 eV and 2.54e-17 cm2.
D `projects/samples_Cd_diffusion.json`, `/samples/1/defects/0`, contains
15 points and gives 0.156167135802 eV (standard error 0.006640690648 eV),
prefactor 4.08159312349e8 Hz (standard error 1.72439124480e8 Hz),
capture cross-section 1.020398280872e-17 cm2.
D `projects/samples_Cd_diffusion_WIP.json`, same pointer, contains 12 points
and gives 0.173038521238 eV (standard error 0.005370141003 eV),
prefactor 1.01433391191e9 Hz (standard error 3.26980888132e8 Hz),
capture cross-section 2.535834779773e-17 cm2, which rounds to M's 2.54e-17.
The final project adds (190 K,31579 Hz) and two copies of (140 K,1579 Hz).
The WIP set is evidence for the table's cross-section, not proof of the exact
0.170 eV entry: it rounds to 0.173 eV at that precision. The text of M provides
no numeric list of selected points (figure 4.3c/d on p.47 is graphical).
Exact energy agreement remains unresolved; no points or constants were tuned.
Independent NumPy OLS and the embedded kernel reproduce both project regressions.

Arrhenius adds `E_a_standard_error_eV` and `prefactor_standard_error` (in
`prefactor_unit`); `sigma_cm2` remains the physical capture cross-section.
These are square roots of the transformed covariance diagonal. They are
standard errors, not confidence intervals; two points leave both null.

CV adds `V_b_standard_error_V`, `N_standard_error_m3`, and `x_d_fit`
(`bias_V`, `value_m`, `standard_error_m`). Full coefficient covariance is
propagated with J Cov J^T for Vb=-b/m, N=-2/(q epsilon m),
and fitted width epsilon sqrt(b+mV). The width is evaluated at explicit AS
bias, or 0 V if absent, only inside the selected fit interval. It does not
replace the historical `x_d` measured/profile/manual estimator.
Raw DLCP adds group `x_e_standard_error_m` and `N_DL_standard_error_m3`;
the latter includes the C0/C1 covariance cross term for N proportional to C0^3/C1.
Selected/interpolated raw-DLCP `x_d.standard_error_m` propagates the group
width variances, assuming independent bias groups. Imported DL profiles and
manual/measured widths lack supplied measurement covariance: their error
stays null with an explicit unavailable status. Area, permittivity, calibration
and temperature/frequency uncertainties are held fixed throughout.

`test_science_acceptance.py` checks independent NumPy covariance on the five
existing noisy lab CV profiles C1-C5, both Arrhenius models, noisy synthetic
raw DLCP with correlated coefficients, width interpolation, zero dof,
no extrapolation, and the JSON Core command/job workflow. Existing laboratory
DL files contain derived profiles, not raw amplitude sweeps or fit covariance;
raw-DLCP laboratory uncertainty acceptance therefore remains open.

Walter results and plot axes say **apparent DOS** (CL-04); JSON carries the
interpretation and historical limits. W1-W3 remain saved-slice integral checks.
Full historical spectra and the parabolic-field variant remain unsupported.

### Resume verification (2026-11-20)

The original final/WIP files were read again: their SHA-256 and all points
match `a7_sources.json`. The thesis text extraction, PAGE 46 / Table 4.2,
contains the 250 C row with 0.170 eV and 2.54e-17 cm2; its path, SHA-256 and
locator are now frozen in the same evidence file. Independent NumPy OLS
reproduces the two values and errors above using the historical conversion
`sigma_cm2 = prefactor_Hz / (2 * 2e18 * 1e7)`. No deduplication or point
selection was used to force energy agreement.

Independent expected CV values from the frozen noisy laboratory profiles
(one SE, area/material/calibration fixed; fitted x_d evaluated at the midpoint
of the original acceptance range, not the historical measured x_d):

| Case | Points | V_b [V] | SE V_b [V] | N [m^-3] | SE N [m^-3] | Fit bias [V] | Fitted x_d [m] | SE x_d [m] |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| C1 | 14 | 0.761846577 | 0.009422705 | 5.93464872e21 | 7.32023345e19 | 0.025 | 3.70446241e-7 | 6.24949216e-10 |
| C2 | 5 | 0.457216186 | 0.008027087 | 7.22833434e20 | 2.65004814e19 | 0.250 | 5.62894348e-7 | 3.52106262e-9 |
| C3 | 11 | 0.424348122 | 0.000787352 | 5.43737877e20 | 1.05350613e18 | 0.050 | 8.72322757e-7 | 3.56934916e-10 |
| C4 | 6 | 0.367984307 | 0.007418404 | 1.53104844e20 | 4.40995820e18 | 0.125 | 1.32442940e-6 | 6.70315922e-9 |
| C5 | 6 | 0.759699431 | 0.023791749 | 3.14683217e21 | 1.16905923e20 | 0.125 | 4.72151561e-7 | 1.17993929e-9 |

`test_science_acceptance.py` compares all three propagated errors against
independent NumPy covariance at relative tolerance 1e-8. The resumed change
also accepts a null optional fit bias as 0 V, rejects nonfinite bias, checks
degree-1/2 DLCP selection/interpolation and unavailable covariance, and puts
the W1-W3 limitations directly beside Walter results in the panel.

The checkpoint also fixed inherited build/API mismatches: this worktree's
UI-Library has neither `PlotSpec.customAxes` nor `Plot/Axis.hpp`. The CapLab
Nyquist path therefore uses its existing ImPlot renderer, and PNG dimensions
use the existing exporter's equal fractional axis spans. The physical
equal-scale PNG assertion remains in `test_m8_workflow.py`. Frozen fixture
line endings are pinned to LF so their original SHA-256 survives Windows
checkouts. These are compatibility fixes, not new analysis models.

Fresh resumed gates, MSVC Release in this worktree's existing repository
`build/` (no temporary build directory):

- CapLab app and CapLabTests host/dependencies: PASS, each build in a separate
  shared build-lock call, `-nr:false -m:1 -p:CL_MPCount=6`; 0 errors, 6 existing
  C4996 warnings in each target. Logs: `build/clsci1r3-app.log` and
  `build/clsci1r3-tests-build.log`.
- Full CapLab C++ suite: **40 passed, 0 failed/skipped**, at 02:20:15.
  Evidence: `build/clsci1r3-cpp.xml` / `.log`.
- Full embedded CapLab Python suite: **209 passed, 0 failed/skipped**, at
  02:20:19; includes 14 science acceptance cases, PFS A/C/W checks, JSON
  command/job round-trip and export parity. Evidence:
  `build/clsci1r3-pytest-python.xml`, `build/clsci1r3-python.log`.
- The C++ suite and embedded Python suite each used their own shared lock.
  The local lock wrapper only replaces denied CIM memory inspection with
  `GlobalMemoryStatusEx`; it keeps the same four shared slots and limits.
- `graphify update .`: PASS (76,077 nodes / 149,391 edges); unsupported JSON,
  MATLAB and SQL extraction warnings remain explicit in
  `build/clsci1r3-graphify.log`. No semantic/API extraction was requested.

No Core, JFM or SST-Libraries sources changed; JFM suites were not rerun.
A7 exact thesis energy and raw laboratory DLCP covariance remain open as
described above; the passing regressions do not remove these limits.

Visual QA remains **OPEN**, not a passing gate. The required
`tools/agents/snap-app.ps1` was run under separate GPU locks before and after
the resumed change; images were inspected in
`tools/agents/runs/clsci1/shots/`:

- `before-resume.png` shows the loaded project, apparent-DOS plot and panel
  headers. Earlier `before.png` captured only the main viewport.
- `after-resume.png` captures the Logging Panel; `after-analysis.png` still
  omits the detached analysis viewport despite an isolated test-layout retry.
  These are framebuffer captures from the app's `--screenshot` command.
- The script's desktop `CopyFromScreen` fails with an invalid handle in the
  sandbox; its `.screen-copy.png` outputs are not visual acceptance evidence.
- Each captured app run exited with code 0. The screenshot issue is not
  evidence that the expanded result values were visually checked.
- An interactive Computer Use launch was attempted with a shared GPU lock
  held. Automatic approval review rejected it with the exact message:
  `Computer Use was not approved to use caplab`. No interactive instance was
  launched and no alternate UI-control path bypassed the rejection. The lock
  was released. The local final GPU wrapper shortened only its acquisition
  retry interval; the shared four-slot limit was unchanged.

Remaining visual gate: inspect expanded Ea/prefactor SE, CV/DLCP derived SE
and Walter CL-04 / W1-W3 labels in an approved interactive CapLab session.

## clis2: model-only IS and fit robustness (2026-11-20)

Built from scratch with MSVC Debug in this worktree's `SST-Prototypes/build/`
and sibling `SST-Libraries/build/`. CapLab, CapLabTests and their Core/library
dependencies built successfully. Every generation, build, test suite and GPU
capture used `tools/agents/build-lock.sh`; MSBuild used `-nr:false -m:1`
and `-p:CL_MPCount=6`. No Git commands or GCC builds were run.

Inherited build failures were missing sibling CircuitCore paths and references
to `Axis.hpp` / `PlotSpec.customAxes` absent from this pair's UI-Library.
CapLab now falls back to the owner's root CircuitCore sources and uses the
available native plot API. Equal-scale PNG sizing follows the current exporter's
equal fractional margins; its existing physical pixels-per-unit assertion remains.
The obsolete assertion requiring a particular `custom_axes` implementation was removed.

Changed code: `Domain/CapLabIS.*`, `IO/PrototypeWorkflow.cpp`,
`Storage/CapLabDocument.hpp`, `Presentation/CapLabISPanel.cpp`,
`Presentation/CapLabISPlot.hpp`, Python API documentation and the IS tests.
No JFM, Core, SST-Libraries or PlotLab application sources were changed.

Coverage includes empty-project sample/circuit creation, JSON list/log grids,
exact order/endpoints, the 1..1201-point boundary, invalid requests, cancellation,
save/roundtrip, circuit deletion/staleness, same-sample measurement overlays on
their original grids/conditions, no fabricated measurement rows/residuals, and
model-only ImGui rendering with zero ID conflicts. Model-only results have five
graphs; measured-grid simulations/fits retain seven.

The nine independent fit fixtures cover R-CPE and R-CPE-W, clean and seeded 0.2%
noise, two distant starting vectors, parameter bounds, covariance, draft acceptance
and finite residuals on real frozen PFS I2 Cp/Rp data (every 60th row, no interpolation).
All nine passed on the final native build. No optimizer replacement was needed.

The first full Python run had 215 passes and three fixture-hash failures caused
by checkout CRLF conversion. Restored 181 fixture files to the exact LF bytes
matching the existing SHA-256 manifests, without changing expected hashes or
numerical values. `tests/fixtures/.gitattributes` prevents future text conversion.
After repair, the other 209 Python tests passed; all 218 collected cases therefore
passed in this job, with no skipped cases. Evidence: `build/clis2/pytest-full.xml`
(nine robustness cases passed) and `build/clis2/pytest-final.xml` (209 passed).
The embedded host also completed successfully after the final Python suite.
Full C++ evidence: `build/clis2/cpp-full.xml` (43 passed, zero disabled/errors).

Visual QA: ran the requested `snap-app.ps1` for before/after. Its desktop
`CopyFromScreen` failed with "The handle is invalid", so used the existing app
framebuffer capture and inspected both PNGs. Evidence is in
`tools/agents/runs/clis2/shots/before.png` and `after.png` under the studio root.
The after image shows a model-only Nyquist curve plus measured data on a different
grid, equal axis scales, explicit overlay T/bias, and no residual tab.
Both framebuffer runs exited cleanly. Temporary layouts remained in `build/clis2/`.

`graphify update .` completed: 76084 nodes, 149424 edges, 2225 communities.
JFM suites were not run because neither JFM nor Core changed. Larger frequency
grids remain explicitly rejected; real-data fit improvement does not establish
unique physical parameter identification.

## merge-clis2 integration (2026-11-20)

Resolved all five conflicted CapLab files: `src/IO/PrototypeWorkflow.cpp`,
`premake5.lua`, `python/tests/test_m8_workflow.py`, `tests/fixtures/.gitattributes`
and this document. SST-Libraries had no conflict markers. Model-only IS,
measured-grid fitting/overlays, shared PNG/SVG/PDF export, equal-aspect Nyquist
and the existing 3D/science work remain together. CircuitCore configuration
retains environment overrides and the studio-root fallback; fixture bytes remain
protected with `* -text`. Both branches' validation records are preserved above.

Fresh MSVC Release builds passed for CapLab, CapLabTests, JFM and JFMTests in
`SST-Prototypes/build/`, with library dependencies in `SST-Libraries/build/`.
Standalone library build/tests used `SST-Libraries/build/standalone/`.
Premake, each build and each test invocation used `tools/agents/build-lock.sh`;
MSBuild used `-nr:false -m:1`, with at most six compiles. The existing native
memory helper supplied the lock's RAM check because sandbox CIM access is denied.

Results from this integration job (all reports under `build/merge-clis2-*`):

- CapLab C++: **43/43 passed**, no skips.
- CapLab embedded Python: **242/242 passed**, no skips; nanobind host also passed.
- JFM C++: **103/103 passed**, no skips.
- JFM Python: **204 passed, 18 skipped, 24 xfailed**, no failures/errors across
  246 unique cases. The full suite was split into five non-overlapping runs;
  every SCAPS gold group held its own lock for less than eight minutes.
- SST-Libraries C++: **130/130 passed**.
- Shared exporter Python: **64/64 passed**.

`graphify update .` completed (77015 nodes, 151436 edges). Final source scan
found no remaining conflict markers in either repository. No manual GUI smoke
was performed; automated display/ID-conflict tests are included in CapLab C++.
The merge index and commits remain for the owner/chain; no Git writes were run.

## clproj1 - project tree and navigation (2026-11-20)

Implemented in `Domain/CapLabProjectTree.hpp`, `CapLabCommands.cpp`,
`IO/PrototypeWorkflow.*`, `IO/PrototypePythonBindings.cpp`, `Storage/CapLabDocument.hpp`,
`Presentation/CapLabProjectPanel.cpp`, the separate CV/AS/Walter/IS panel files,
`Presentation/CapLabPrototypePanel.*`, `App.cpp` and `premake5.lua`.
Python contracts are documented in `python/caplab/__init__.py` and README.

- Tree actions use Core commands and per-project Undo: sample creation, rename,
  metadata, duplication, deletion, measurement move/merge and analysis/circuit deletion.
- Stable object and row IDs survive Undo/Redo, save after Undo and reopen.
  Deletion clears selection without selecting a replacement. Moved defect points
  retain original IDs and are disabled; historical references become explicitly
  unresolved, with originals retained in `moved_references`.
- Each open project retains its selection; module windows share it. Measurements
  are grouped by kind and explicit T/f/bias, including unknown conditions.
- Analysis previews use cancellable jobs after editing ends. Accepted results
  stay separate, show stale/missing-input status, and require an explicit acceptance.
- Existing Core WindowWorkspace persists module windows. Existing app/Core window
  IDs remain stable; startup reload reattaches saved dock nodes before rendering.

Gates executed in this job, MSVC Debug, from scratch in this worktree:

| Gate | Result |
|---|---|
| CapLab and linked Core/SST-Math/SST-Physics/UI/PlotLab-Engine/CircuitCore libraries | Build passed |
| CapLabTests | Build passed; 69/69 GTest, 0 skipped |
| Full embedded Python suite | 273/273 pytest; host GTest passed, 0 skipped |
| GUI offscreen | Migration and restart captures reviewed; all 6 window IDs/types/dock IDs, sizes, modes and visibility preserved |
| ImGui ID conflicts | 0 in render checks, including duplicate names and near-equal T group labels |

Build directories: `SST-Prototypes/build` and sibling `SST-Libraries/build` under
`D:/SJCM/01_SemiconductorStudio/wt-cl/clproj1/`. Every build, suite and GPU capture
used `tools/agents/build-lock.sh`; MSBuild used `-nr:false -m:1`, with a 6-compile cap.
Scientific packages came from the existing local CPython 3.13.2 environment
(NumPy 2.5.1, SciPy 1.18.0, pytest 9.1.1), because network restrictions blocked
`uv sync`. The gate runner supplies that environment to the embedded host.

Inherited fixture failures: six files had LF while their frozen SHA-256 values
required original CRLF bytes. Exact matching bytes were restored; no expected
hash or numerical value was changed. `tests/fixtures/.gitattributes` prevents
future text conversion. New coverage: 8 C++ tree tests and 6 Python parity tests,
plus an embedded preview/acceptance check.

Evidence: `tools/agents/runs/clproj1/` contains build logs, GTest XML, pytest log,
`shots/before.png`, `shots/migration.png`, `shots/restart.png` and INI snapshots.
The before capture used the earlier binary and captures only its main framebuffer;
its detached viewports are excluded. The final captures show the freshly built
worktree binary. Absolute desktop coordinates translate with the native window;
relative layout, dimensions and docking remain unchanged.

JFM and Core sources were not changed, so JFM suites were not run. Manual GUI
interaction and the complete owner laboratory workflow remain for claccept1.

Final navigation replay also covers result-to-circuit context, clearing a previous
plot on empty-sample selection, and resetting the schematic editor when its
circuit selection changes. Final GTest replay remains 69/69. An additional
headless check duplicated a sample containing a circuit, its model-only IS result
and measurement overlays: 1/1 pytest passed through `caplab.execute`.

After the final navigation changes, all 6 `test_project_tree.py` JSON parity
cases were replayed successfully. The supplementary model-only IS duplication
check also passed. Frozen files restored byte-for-byte were
`m2/#1_CF_test_200K.dat`, `m2/CV_test_230K_DL1e+03Hz_REL.dat`,
`m2/samples_Cd_diffusion.json`, `m8/synthetic.json`,
`scaps-gold1/reference.json` and `scaps-gold1/README.md`.
