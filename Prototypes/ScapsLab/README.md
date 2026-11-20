# ScapsLab multi-device projects

Core application for the read-only sibling `scaps_core` checkout. The old viewer remains independent. No solver sources are copied. `ScapsCore` is a static library compiled with MSVC `/fp:precise`, sharing SST's yaml-cpp target.

Generate with `bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh Vendor/Binaries/Premake/Windows/premake5.exe --scaps-core=D:/SJCM/01_SemiconductorStudio/scaps_core-wt/ui vs2022`. Build the generated `ScapsLab`, `ScapsLabHeadless`, and `ScapsLabTests` vcxproj files sequentially through the same lock, with `-p:Configuration=Release -p:Platform=x64 -nr:false -m:1`. Run `ScapsLabTests.exe` through the lock. Build outputs are under `build/bin/Release-windows-x86_64/`.

ScapsLab opens a workspace of independent devices. `Workflow::project.devices` owns `DeviceState`: document, I-V/band/AC snapshots, undo, dirty state and editor selection. `Current()` selects the active device. Every job captures `pendingDevice`, so changing selection cannot redirect its result. Edits invalidate only their owner's snapshots. Delete retains the actual state and undo stack for undo/redo.

Devices starts on the left above Layers. Select, rename inline, edit colour, duplicate, delete/undo delete, import YAML, drag to reorder and check devices for compare. New uses the existing device template. I-V and AC overlays use device names and colours. Run all checked submits one Core job per device in order; Devices shows device and point progress, and Cancel stops the batch. Existing editors and single-device commands operate on the active device.

`.scapslab` is JSON: readable device mappings, compact result columns, one atomic Core TextFileIO replacement. The measured 3 x 100,000 scalar I-V point fixture is 76,418,790 bytes as rows and 28,419,444 bytes as columns (`tests/storage-size.json`), so no sidecar commit protocol is needed. Spatial profiles add their actual array sizes; repeating the stored full-profile samples to 3 x 100,000 points counts to 35,394,890,712 bytes. That size was counted, not written or stress-tested; serialization remains in memory and larger profile workloads need streaming/sidecars. Device mappings retain the scaps_core schema and unknown extensions. Provenance records the canonical-document FNV-1a hash, solver source stamp and Unix time in ms. Hash mismatches are rejected on load; this identifies a model, not a security signature. Undo is runtime state and is not serialized.

File offers Open / Save / Save As / Recent files / Examples. Open and close guard unsaved changes and active jobs; the native title includes `*`. Recent files use Core TextFileIO in the existing configuration directory. `ScapsLab.exe <project.scapslab|device.yaml>` loads that file. Legacy YAML opens as one device with a `.scapslab` save destination; explicit `save` to YAML exports the active device. Examples install beside the executable, with a source-tree fallback.

Seven examples are in `examples/`: pn 270/300/350 K; diffusion/SRH; baseline/deep-defect pn; two-layer pn; C-V/C-f-ready diode; finite-S contact playground; pn compare with stored results. Descriptions identify validation sources and decks. The first six contain no results. Tests solve every device, preserve document dumps and result points through save/load, and solve again bitwise. `SCAPSLAB_WRITE_STORED_EXAMPLE=1` materializes engine defaults in fixtures and refreshes the stored-results example through normal commands.

File imports/exports SCAPS `.def` through the integrated engine. Unsupported native items remain visible as warnings in every panel. The sl5 single-device light/QE path is described below. The batch engine still rejects illumination and reports solar Recorder metrics as null with `pending st6`; no dark approximation is substituted. Engine cancellation remains between member solves.

Additional JSON operations through nanobind and the same Core commands/jobs: `project_load`, `project_save` (`path`), `device_list`, `device_add` (optional `document`, `path`, `name`, `description`), `device_duplicate`, `device_remove`, `device_select` (`device_id`), `device_update` (`name`, RGBA `colour` 0..1, `description`, `compare`), `device_move` (`index`), `project_edit` (`name`, `ui`), `run_all`. Headless callers explicitly decide whether to save before `project_load`. Responses add `devices`, `active_device`, `project`, `dirty`; polling adds `batch_progress`. Python methods expose the eight requested session operations.

sl4/slio: extend `DeviceState`; keep results/history per device. Capture stable device IDs for jobs/batches/recordings, preserve documents verbatim, attach provenance to new results and dispatch the same operations from GUI/Python. Rectangular points pack as columns; heterogeneous point mappings retain rows.

The full YAML mapping is retained, including fields without v0 editors and unknown extension mappings. Defaults are materialized and stable `scapslab_id` strings are added to layers. YAML formatting and comments are not retained. Save uses Core's atomic TextFileIO replacement. JSON infinity is the string `.inf`; failed current values are JSON null.

The embedded nanobind module is `sst_scapslab`; the Python package is `python/scapslab`. In the embedded runtime, add that directory to `sys.path`, then:

```python
import scapslab
session = scapslab.Session()
project = session.execute({'operation': 'load', 'path': '/absolute/device_project.yaml'})
started = session.start({'operation': 'run'})
progress = session.poll()  # running, progress {completed,total,stage}, response
# session.start({'operation': 'cancel'})
```

`execute(request)` waits without opening a GUI. `start` submits the same Core command/job as the panel; `poll` publishes completed snapshots. Successful responses contain `ok`, `document`, `result` (I-V), `ac_results` (independent `cv`/`cf` snapshots), and `error`. Project edits clear both result types. Failures contain stable `error.code` and `error.message`; codes include `invalid_request`, `command_rejected`, `operation_failed`, and `cancelled`. Keep a session on its creating thread.

Operations:
- `load`, `save`: `path` (YAML).
- `inspect`: current document/results.
- `edit`: recursive object `patch`, or explicit replacement `document`. Arrays replace as a whole; preserve layer IDs. Replacement supports removing optional fields such as `flat_band`.
- `layer`: `action` = `add`, `remove`, `move`; remove/move use `layer_id`, move also uses zero-based `index`. Add duplicates the final layer's material/doping/defects and assigns a new ID.
- `run`, `cancel`: no additional fields. Temperature, voltage and solver controls come from the document; field names carry SI units. Results contain `bias_id`, consumer `voltage_V` (left minus right), signed `J_A_m2`, `conservation_error_A_m2`, convergence, mesh and profiles.

Cancellation is cooperative between bias points and after mesh preparation; scaps_core has no cancellation callback inside mesh/Newton. Nonzero external networks replay continuation prefixes for exact core behavior and may be slower. Arora uses the core's sourced `caughey_thomas` parameters; selecting it without those parameters returns a validation error. No material parameters are invented.

AC commands (same Core command/job path from GUI and Python):

```python
cv = session.execute(dict(operation='cv', start_V=-0.5, stop_V=0.5, step_V=0.05,
                          frequency_Hz=1e4, temperature_K=300.0))
cf = session.execute(dict(operation='cf', voltage_V=0.0, start_Hz=10.0, stop_Hz=1e7,
                          points=61, temperature_K=300.0))
session.execute(dict(operation='export_ac', kind='cv', path='cv.csv'))
session.execute(dict(operation='export_ac', kind='cf', path='cf.csv'))
```

C-V follows the signed range/step (up to 10001 points). C-f uses logarithmically spaced positive frequencies including both endpoints (2..10001 points). All inputs are explicit SI quantities; temperature is independent of the stored project. AC snapshots include `area_m2`, stable `point_id`, `voltage_V` (left minus right), `frequency_Hz`, `temperature_K`, `capacitance_F`, `conductance_S`, `G_over_omega_F`, `inverse_C2_F_minus2`, solver `status` and `conservation_relative`. Values include the core's external network and area, with no scaling or sign changes to C/G. Nonfinite values are JSON null / empty CSV cells; nonconverged points remain gaps. Mott-Schottky uses total C in F (no area normalization). C-V/C-f show G/omega alongside C on a separate Y axis; C-f/G-omega-f have log f axes.

Cancellation is checked after mesh preparation, between DC and AC, and between AC points; an individual core solve cannot be interrupted. C-V replays DC continuation prefixes to match `solve_cv` exactly, so long sweeps cost more. Completed snapshots replace prior data only on success; failed/cancelled jobs preserve prior snapshots. CSV uses atomic Core TextFileIO and 17-digit precision.

CapLab import: use `lab_cv` for C-V (`V`, `C`, optional `G`) or `converted_as_c_g` for C-f (`f`, `C`, `G`), units V/Hz/F/S, and confirm the snapshot's `area_m2`, temperature and fixed frequency/bias. Its current reader requires every cell to be numeric. The smallest adapter removes ID/status/derived columns and excludes failed points; no CapLab change is required:

```python
import csv, io
with open('cf.csv', newline='') as source:
    rows = list(csv.DictReader(source))
columns = ('frequency_Hz', 'capacitance_F', 'conductance_S')  # CV: voltage_V instead of frequency_Hz
numeric = io.StringIO()
writer = csv.writer(numeric)
writer.writerow(columns)
writer.writerows([row[key] for key in columns] for row in rows
                 if row['valid'] == 'true' and all(row[key] for key in columns))
# Import numeric.getvalue(); mapping {'f': 0, 'C': 1, 'G': 2} (CV: 'V': 0).
```

Run offers independent Auto-recalculate and Auto-recalculate band toggles. Edits queue through Core jobs, cancel outdated DC/band work and debounce for 250 ms after validation while no item is active. A single core mesh/Newton solve remains cooperatively cancellable only at its boundary. Calculate buttons explain their disabled state. Results reports measured `compute_time_ms` and `mesh_node_count`.

Additional Python operations (same registry and jobs): `undo`, `redo`; `band` with explicit `voltage_V`; `auto_recalculate` with `enabled` and optional `band_enabled`. `edit.gesture_id` merges successive successful device edits for one drag; use a distinct ID for every gesture and omit it for separate edits. `band_result` is an independent snapshot; `poll` exposes status and `auto_recalculate_pending` during debounce. Keep polling to drive automatic calculation headlessly, or use `run`/`band` directly. AC sweep settings are local view state.

Gallery: set `SCAPSLAB_GALLERY_PANEL=default` for the docked layout, or 0..10 for Project, Layers, Contacts, Solver, Run, Results, C-V, Mott-Schottky, C-f, G-omega-f, I-V. Launch with `--reset-layout --screenshot <absolute.png> --exit-after-screenshot`. Gallery solves I-V and both AC sweeps through Core. PNGs: `../tools/agents/runs/gallery/scapslab-v2/`. A portable runtime may be staged in `%TEMP%` when the build directory disallows runtime settings writes.

Validation: `ScapsLabTests` also checks undo gesture merging, distinct queued edits, auto-recalculate cancellation/debounce, band snapshots, masked failed bias points, and floating/docked Project title IDs with conflict highlighting enabled. Existing checks cover C/G against `solve_cv`/`solve_cf` on `device_project.yaml` at relative tolerance 1e-12, mesh/conventions, CSV, invalid requests, snapshot preservation/cancellation, and embedded Python AC/export commands. Existing I-V and YAML roundtrip tests remain.

Batch supports temperature and other numeric project parameters; adaptive-mesh visualization remains separate.


Defect/structure editors (slf): `Defects` selects a layer and an explicit list of stable defect IDs; `Interfaces` selects a boundary and its list of sheet defects. Bulk distributions are discrete, uniform, Gaussian, CB/VB tails, plus the current core's correlated/independent amphoteric single transitions. Energy references and both capture cross sections are explicit. Sheet distributions are single/uniform/Gaussian, with SCAPS references 3..9, independent left/right electron/hole cross sections and peak/integrated density conventions. The core does not support sheet tails or amphoteric sheets. Gaussian Echar is sigma times sqrt(2); the editor states this convention.

Auto-recalculate band is enabled at GUI startup. Each successful band/I-V point carries `defect_previews`: analytic density samples per eV, band edges and the core's actual expanded-level occupations at the layer midpoint or interface, at the reported voltage/temperature. Discrete levels are vertical lines (their integrated Nt is edited separately); correlated amphoteric occupancy is P-. Density is shown as cm^-3/eV or cm^-2/eV; canonical JSON remains SI. Preview state is cleared on model edits. All curves on the selected object are plotted together. Imported quadrature/normalization fields are retained.

`Rs-Rsh` edits canonical `external.area_m2`, `series_ohm_m2` and `shunt_ohm_m2`; controls show cm^2 and Ohm cm^2, with the corresponding Ohm values. Zero Rs and infinite Rsh are explicit controls. `Structure` (also above Results) draws true thickness proportions and vacuum-referenced Ec/Ev from chi/Eg, allows layer/interface selection and thickness dragging, and badges each interface with its defect count. Layer removal/reordering is rejected while sheet defects exist to prevent silent reassignment of their indexed attachment. Remove the sheet defects first.

Every editor uses the existing `edit` command/job, undo merging and nanobind JSON surface; no additional editor-only API is required:

```python
state = session.execute({'operation': 'inspect'})
doc = state['document']
doc['layers'][0]['thickness_m'] = 2e-6
doc['layers'][0]['defects'] = [dict(scapslab_id='defect-1', label='Gaussian',
    energy_distribution='gaussian', energy_reference='relative_to_Ev', energy_offset_eV=0.5,
    characteristic_energy_eV=0.05, Nt_m3=1e19, sigma_n_m2=1e-19, sigma_p_m2=1e-19,
    charge_type='neutral')]
session.execute({'operation': 'edit', 'document': doc})
preview = session.execute({'operation': 'band', 'voltage_V': 0.1})['band_result']
```

Results carry independent solver `status`, `valid`, `validity_status`, and immutable `project_snapshot`. The numerical validity gate requires convergence and current conservation within 1% (DC absolute floor 1e-10 A/m^2); it does not establish physical model applicability. Invalid points are masked in plots. AC CSV retains raw rows with separate `valid`, `validity_status`, `conservation_relative` and `area_m2` columns. DC exposes consumer `terminal_voltage_V` and `internal_voltage_V` independently for external networks. Changes clear DC/band/AC snapshots, preventing export of a previous model's AC data.

Gallery indices 11..14 select Defects, Interfaces, Rs-Rsh and Structure. These gallery views use a synthetic two-layer fixture with Gaussian plus both tails and a Gaussian interface sheet, submitted through `edit`; normal startup continues to load the checkout example. Required slf images are under `../tools/agents/runs/slf/shots/`. Index 4 is Run. Window titles retain the shared EditorWindow icon before the title.

Validation and handoff for slproj: [SLPROJ-REPORT.md](SLPROJ-REPORT.md). Final MSVC Release build and all 25 tests passed in `build/slproj/`; screenshots and engine limitations are listed in the report.


Illumination, absorption and grading (sl5): gallery panels 19/20/21. Illumination selects bundled SCAPS AM1.5G (Hulstrom/Bird/Riordan 1985 bin powers), an absolute SCAPS spectrum file, or monochromatic power; intensity, side, transmission, reflections and front/back filter files use canonical core fields. File selection uses the native picker; Enter applies an edited path. Optical files remain external dependencies. Relative paths in loaded YAML are anchored to its directory.

`run` and `band` now support light through the integrated core. Both expose `optics.x_m`, box-averaged `generation_m3_s`, incident power and per-layer absorption samples (`layer_id`, `wavelength_nm`, `alpha_m1`, `without_tail_m1`). `run.photovoltaic` contains refined Voc in consumer volts, delivered Jsc in A/m^2, FF/efficiency fractions and Pmax in W/m^2, with an independent core status/message; unbracketed or failed metrics are null. Choose a sweep stop beyond Voc. Absorption supports none/sqrt/SCAPS/file and the distinct Eg-joint Urbach extension, off by default; a pure sqrt with alpha(Eg)=0 cannot produce that tail. Imported SCAPS power terms are retained.

```python
session.execute(dict(operation='edit', patch={'illumination': {'enabled': True, 'scapslab_spectrum': 'AM1.5G'}}))
qe = session.execute(dict(operation='qe', start_nm=300., stop_nm=4100., points=61,
                          probe_mode='constant_flux', probe_photon_flux_m2_s=1e16))
# Alternative: probe_mode='constant_power', probe_power_W_m2=1e-3.
result = qe['ac_results']['qe']
```

QE is dark differential EQE at terminal 0 V and the device temperature, retaining optical losses and filters. Every point includes effective wavelength, flux, power, solver status and validity; the request and effective mode are retained. Integrated Jsc sums incident bin photon flux times linearly interpolated EQE and requires coverage of all active spectral bins (otherwise null). It is compared with a separate finite-light short-circuit solve; nonlinear illumination response and wavelength sampling can cause differences. There is no extrapolation or forced agreement. Failed/cancelled operations preserve previous results; edits invalidate QE. Cancellation is checked between wavelengths, not within a core solve.

All editors use existing `edit` commands, undo and auto I-V/band recalculation. QE uses its Calculate button, like the AC sweeps. Grading exposes uniform/linear/parabolic/logarithmic/exponential laws and endpoints for Eg, chi, permittivity, doping, density of states and mobility, plotted in canonical SI/eV units and marked in Structure. More complex imported profiles remain in the YAML; the simple editor is intended for position-dependent analytic profiles. The example `08-pn-illumination.yaml` is synthetic, not a calibrated solar cell.

Screenshot mode: `SCAPSLAB_GALLERY_PANEL=19`, `SCAPSLAB_OPTICAL_TAB=0/1/2` selects G(x), light I-V or QE. Panels 20/21 show Urbach absorption and grading/Structure. Run with `--reset-layout --screenshot <absolute.png> --exit-after-screenshot`.


ScapsLab v4 batch/Recorder (sl4): panels 16/17/18 select Batch, Recorder and Batch overlay in screenshot mode. Batch chooses numeric parameters from a searchable project tree, with stable layer/defect IDs, explicit units and list/linear/log values. One axis, two nested axes and Together are supported (at most 10,000 members). Draft settings may be temporarily invalid; the preview explains the error and Run remains disabled. Run/Stop use Core jobs, report member progress and preserve failed/cancelled rows. Retry updates only the chosen member. A member solve cannot be interrupted inside the engine. Preview shows combinations, bias/frequency samples, polarity and a time estimate after a previous run.

Recorder selects J@V, C/G@V,f, Vbi and the engine's solar columns before or after a run. Re-extract uses retained curves without solving; a quantity absent from the retained samples has a typed missing status and null value. The Run range supplies bias samples; requested Recorder biases inside that range and requested AC frequencies are added explicitly to the preview before solving. Values outside the range remain missing. Configure quantities before batch to acquire their data. Solar columns remain null (`pending st6`) in the current batch engine, even though the separate single-device light workflow supports them. The worksheet retains status, errors, time, attempt count, parameter values and unit-bearing columns. CSV exports all or a selected member. Send to PlotLab writes a native workbook with parameter/scalar/status columns and provenance; failed values are NaN in numeric workbook columns with separate status columns. Overlay uses a Viridis map by axis 1, parameter values/units in an outside legend, and member selection from list or curve samples. Recorder plots one-dimensional scalars or a family of curves grouped by axis 2. Editing a device invalidates its batch results; `.scapslab` persists batch snapshots and settings, with provenance and worksheet validation on load.

```python
spec = {'mode': 'nested', 'axes': [
    {'parameter_id': 'temperature_K', 'start': 270., 'stop': 330., 'count': 3, 'spacing': 'linear'}],
    'voltages_V': [0., .05], 'frequencies_Hz': [1e4]}
session.batch_parameters()  # operation_result: stable IDs, tree labels, units, current values
session.batch_settings(spec, [{'id': 'C0', 'quantity': 'C', 'voltage_V': 0., 'frequency_Hz': 1e4}])
session.preview_batch()
session.run_batch()  # start({'operation': 'run_batch'}) + poll() for asynchronous use
session.record_batch()  # re-extract, no solver
session.retry_batch('point_1')
session.export_batch('recorder.csv')
session.send_to_plotlab('recorder.plotlab')
session.import_def('device.def')  # operation_result.warnings also retained in document.scaps_native
session.export_def('device.def')  # engine also writes a numerical-settings .act companion
```

Stage-05 gate uses `tests/stage05-gaussian.def` and an independent YAML mapped by the established stage-05 validator. Numerical settings are aligned to the def1 audit: 201 mesh nodes/layer and 63-level Gauss-Legendre defect quadrature, 300 K, 0/.05/.1 V. Native import's original quadrature is otherwise preserved. Tests compare current points bitwise, test Python/File operations, actual UI Run batch vs Python, failed/cancelled members, retry isolation, workbook load and workspace persistence. Final logs and reviewed screenshots: `tools/agents/runs/sl4/` in the workspace root's sibling tools directory.


Generation-recombination (slgr): panel 22 follows the I-V point selection or automatically solves an explicit terminal voltage through `band`. Voc uses the refined metric; Vmp selects the valid sampled point with maximum delivered power and solves that voltage again. G and each engine R channel use a logarithmic bulk-rate axis; interface sheets are markers on a separate logarithmic sheet-rate axis. Zero/negative rates are gaps on log axes; signed values remain in tables/export. Legend labels retain stable defect IDs and the engine IDs remain in export. The structure bar and plot boundaries use the snapshot's layer geometry. Tables contain per-channel/per-layer signed currents and layer totals; sheets belong to the left layer exactly once.

Each DC/band point now carries `generation_recombination`: `generation_m3_s`, channels (`id`, `engine_id`, `mechanism`, `R_m3_s`, `R_sheet_m2_s`, `integrated_current_A_m2`, `by_layer`, validity), layer budgets, and generation/recombination/net currents in A/m^2. Profiles and channel integrals are engine outputs, not reconstructed SRH formulas. G and per-layer sums use the engine finite-volume half-box widths on the actual point mesh. This retains contact half boxes and signed thermal generation. The terminal comparison reports `terminal_budget_relative_error` and `terminal_budget_valid` at 1e-6, independently of profile validity. Engine contact currents are signed carrier removal (negative means injection). In native current sign, `J = G - R - right.electron - left.hole + J_shunt`; the residual is normalized by the largest magnitude of G?R, the selected contact sum, shunt and terminal current (floor 1e-30 A/m?). The panel and G/R exports retain both carrier currents at each contact, shunt, series and predicted terminal currents. Series current already equals terminal current and is not added twice. Older bulk-only snapshots require recalculation for this closure check.

```python
session.execute(dict(operation='band', voltage_V=0.2))
session.execute(dict(operation='export_gr', source='band', point_index=0,
                     format='json', path='gr.json'))
# source='iv' exports the selected zero-based I-V point; format='csv' is also supported.
```

`export_gr` runs through the existing Core command/job and nanobind Session. It captures the immutable source snapshot and device ID when submitted, validates the requested point and format, and writes atomically through TextFileIO. JSON includes the full point, project snapshot (illumination, IDs, temperature), model hash, solver stamp and timestamp. CSV contains spatial rows and per-layer integral rows, with working-point IDs/voltages, effective illumination JSON and provenance JSON repeated per row; empty numeric fields distinguish sheet/bulk/integral rows. Invalid raw data retain validity flags. Model edits retain immutable snapshots as previous runs; exports retain the original model provenance. Gallery: `SCAPSLAB_GALLERY_PANEL=22`, existing `--screenshot` options.

The slgr terminal-budget acceptance gate remains open; see `tools/agents/runs/slgr-report.md` in the sibling tools repository. Channel integration parity and solver convergence do not establish terminal-budget closure. CSV repeats both terminal-budget fields on every row.

## slio: project files and reproducible exports

The workspace writer now emits schema 2 `.scapslab` JSON. Versions 0/1 workspace mappings and legacy v0/v1 device YAML migrate in memory; future versions are rejected before changing the live project. Device schema remains owned by scaps_core. Each device retains its full device/numerics/working-condition mapping, action requests (`actions`, with `iv`, `cv`, `cf`, `qe` keys), batch specification and Recorder columns. AC/QE drafts persist before calculation. Results are optional: File > Include results in project or `save_project(path, include_results=False)`. UI layout and plot preferences live in `ui`; neither is needed to solve. Optical spectra, generation, filters, absorption tables and imported grading files become inline SI tables when imported/edited; material/numerical definition imports are materialized through the existing core YAML codec.

Core `TextFileIO` already writes an exclusive sibling temporary file and replaces the destination atomically. ScapsLab reuses it, returns absolute `path` and `size_bytes`, and displays them in Project status. A killed-writer test interrupts that exact writer before replacement and reloads the previous project. File > Save As / Recent files and the dirty native/panel title remain available. Every 30 seconds, an idle dirty GUI session saves a complete recovery snapshot in the existing config directory (`scapslab-recovery.scapslab`), without clearing dirty state or changing its save path. File > Recover autosave opens it with unsaved status and a new Save As destination. A successful explicit save removes recovery. Autosave waits for the current job and is not a mid-solve checkpoint.

A measured 100,000-point, ten-field scalar terminal-result fixture is **31,459,714 bytes as rows** and **9,059,990 bytes as compact columns**. The assert-based GTest packs/unpacks the actual data and counts the actual serialized strings; `tools/agents/runs/slio/storage-size.json` contains the measurement. Inline columns avoid sidecar coordination and keep a project portable as one file. Spatial profiles add their full array sizes; serialization remains in memory. This scalar measurement is not a stress test of 100,000 full spatial profiles.

File > Export results opens a nonmodal window with format, selected run/all saved runs, and destination. Run IDs combine stable device ID, technique and retained run timestamp; a new calculation replaces that technique's current snapshot. CSV is a single rectangular sheet with explicit SI suffixes, terminal/internal voltage, calculation index, solver/validity status, settings, effective conditions and provenance. Batch/Recorder values, units, parameters and per-cell statuses share that sheet. `point_json` preserves raw profiles and batch curves for lossless rereading. Python's default CSV field limit is 128 KiB; use `csv.field_size_limit(64 * 1024 * 1024)` for these structured fields. JSON retains the complete snapshots as compact columns, plus explicit voltage columns and effective conditions. QE records its effective dark background and probe request; each new QE sample stores the engine probe internal voltage in consumer sign (left minus right) and terminal voltage separately. The QE table shows both in V. Older samples without this optional field remain readable and display unavailable; failed probes store null.

PNG/SVG/PDF reuse `Prototypes/PlotLab/src/PlotLabExport.cpp` and the existing `sst_plotlab.exporter` backend, fonts, physical sizing and atomic output. Multiple curves produce numbered files; retained batch I-V members include parameter values in their legend. Invalid points are gaps. Export jobs copy saved snapshots and never call a solver, change device settings or request a compute-path modal. Write/backend failures return the existing typed job error and preserve snapshots.

```python
session = scapslab.Session()
loaded = session.load_project('device.scapslab')
saved = session.save_project('copy.scapslab', include_results=True)
runs = session.result_runs()['runs']
exported = session.export_results('selected.csv', run_ids=[runs[0]['run_id']])
all_runs = session.export_results('all.json', format='json', all=True)
```

These package methods all use the existing embedded nanobind Session and Core command/job path (`project_load`, `project_save`, `inspect`, `export_results`). Errors retain `ok=False` and `error.code/message`. No engine sources or Git metadata are changed. QE internal voltages come directly from `QEPoint::internal_voltage_V`; AC terminal/internal voltages are captured from the already-computed DC state. The workspace schema remains version 2.


### Run history and quick sweep (slhist1)

Each completed I-V, band/G-R, AC, QE calculation and batch member produces a run with a stable ID, model hash, full model diff, parameter changes, timestamp, elapsed time and solver provenance. Model edits retain previous results. Devices / Run history provides Keep/Pin, name, visibility, colour and selection; I-V and AC overlays use solid colours and parameter-change legends. Spatial history overlays use each run's own mesh. Quick sweep is available by right-clicking numeric model controls: native-unit +/- range, point count and linear/log spacing; it uses the existing one-axis batch engine and records a group with a colour map.

History uses one table row per run, with a colour swatch (click for the picker), visibility, a Pin toggle, label, technique and local time. Double-click the label to rename; Enter or leaving the edited field saves, Escape cancels. An empty name shows the automatic parameter diff. Device and Sweep groups collapse independently; Sweep headers preview the run colours. Hover the information icon for omitted spatial profiles, or the time for its date and elapsed seconds. These view controls leave schema 3 and the JSON API unchanged.

Automatic retention defaults to five unpinned runs per device. The project `ui.history_retention` setting accepts 1..10000; pinned runs remain. Devices / Run history clears the active device; Edit / Clear history clears all devices. Clear unpinned is the default; clearing pinned runs requires a GUI confirmation. Each clear is one Core Undo step (including results, metadata and selection).

`.scapslab` schema 3 stores history, run selection/visibility/colours, active device, compare selection, numeric view selections, panel visibility, docking/tabs and plot limits. Schemas 0..2 remain readable with empty history when absent. History spatial profiles larger than 64 KiB per result are omitted on disk and marked `profiles_stored: false`; scalar values and I-V curves remain. In-memory profiles are preserved until the project is closed. HDF5 remains slview1 work.

Embedded Python uses the same commands/jobs:

```python
s.history_list()
s.history_update(run_id, pinned=True, name="Reference", visible=True)
s.history_select(run_id)
s.history_clear(scope="device", device_id=device_id, include_pinned=False)
s.history_clear(scope="all", include_pinned=True)
s.execute({"operation": "undo"})
s.quick_sweep("temperature_K", delta_native_units=10, points=5, spacing="linear")
```

The JSON operation names are `history_list`, `history_update`, `history_select`, `history_clear`, `quick_sweep`. Sweep takes `parameter_id`, `delta_native_units`, `points`, `spacing`; optional `voltages_V` and `frequencies_Hz` control sampling. `batch_parameters` supplies the stable IDs and native units. Unknown IDs, invalid colours, retention and sweep ranges return typed errors without changing the workspace.
