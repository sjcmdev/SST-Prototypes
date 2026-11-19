# ScapsLab multi-device projects

Core application for the read-only sibling `scaps_core` checkout. The old viewer remains independent. No solver sources are copied. `ScapsCore` is a static library compiled with MSVC `/fp:precise`, sharing SST's yaml-cpp target.

Generate with `bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh Vendor/Binaries/Premake/Windows/premake5.exe --scaps-core=D:/SJCM/01_SemiconductorStudio/scaps_core-wt/ui vs2022`. Build the generated `ScapsLab`, `ScapsLabHeadless`, and `ScapsLabTests` vcxproj files sequentially through the same lock, with `-p:Configuration=Release -p:Platform=x64 -nr:false -m:1`. Run `ScapsLabTests.exe` through the lock. Build outputs are under `build/bin/Release-windows-x86_64/`.

ScapsLab opens a workspace of independent devices. `Workflow::project.devices` owns `DeviceState`: document, I-V/band/AC snapshots, undo, dirty state and editor selection. `Current()` selects the active device. Every job captures `pendingDevice`, so changing selection cannot redirect its result. Edits invalidate only their owner's snapshots. Delete retains the actual state and undo stack for undo/redo.

Devices starts on the left above Layers. Select, rename inline, edit colour, duplicate, delete/undo delete, import YAML, drag to reorder and check devices for compare. New uses the existing device template. I-V and AC overlays use device names and colours. Run all checked submits one Core job per device in order; Devices shows device and point progress, and Cancel stops the batch. Existing editors and single-device commands operate on the active device.

`.scapslab` is JSON: readable device mappings, compact result columns, one atomic Core TextFileIO replacement. The measured 3 x 100,000 scalar I-V point fixture is 76,418,790 bytes as rows and 28,419,444 bytes as columns (`tests/storage-size.json`), so no sidecar commit protocol is needed. Spatial profiles add their actual array sizes; repeating the stored full-profile samples to 3 x 100,000 points counts to 35,394,890,712 bytes. That size was counted, not written or stress-tested; serialization remains in memory and larger profile workloads need streaming/sidecars. Device mappings retain the scaps_core schema and unknown extensions. Provenance records the canonical-document FNV-1a hash, solver source stamp and Unix time in ms. Hash mismatches are rejected on load; this identifies a model, not a security signature. Undo is runtime state and is not serialized.

File offers Open / Save / Save As / Recent files / Examples. Open and close guard unsaved changes and active jobs; the native title includes `*`. Recent files use Core TextFileIO in the existing configuration directory. `ScapsLab.exe <project.scapslab|device.yaml>` loads that file. Legacy YAML opens as one device with a `.scapslab` save destination; explicit `save` to YAML exports the active device. Examples install beside the executable, with a source-tree fallback.

Seven examples are in `examples/`: pn 270/300/350 K; diffusion/SRH; baseline/deep-defect pn; two-layer pn; C-V/C-f-ready diode; finite-S contact playground; pn compare with stored results. Descriptions identify validation sources and decks. The first six contain no results. Tests solve every device, preserve document dumps and result points through save/load, and solve again bitwise. `SCAPSLAB_WRITE_STORED_EXAMPLE=1` materializes engine defaults in fixtures and refreshes the stored-results example through normal commands.

This ui snapshot exposes neither `.def` import nor a generation-aware illuminated solve. YAML import is available. Illuminated run/AC requests fail explicitly. No thin-film/light example or calculated solar metrics are claimed. The compare table consumes `solar_metrics` (`Voc_V`, `Jsc_A_m2`, dimensionless `FF`, `efficiency`) when a future light result provides them. Engine needs: illuminated solve, public version identifier and finer cancellation inside Newton/mesh preparation.

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

Next: illumination/QE; temperature sweep; adaptive-mesh visualization.


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
