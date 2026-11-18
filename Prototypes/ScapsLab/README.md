# ScapsLab v1

Core application for the read-only sibling `scaps_core` checkout. The old viewer remains independent. No solver sources are copied. `ScapsCore` is a static library compiled with MSVC `/fp:precise`, sharing SST's yaml-cpp target.

Generate with `bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh Vendor/Binaries/Premake/Windows/premake5.exe --scaps-core=../scaps_core vs2022`. Build the generated `ScapsLab`, `ScapsLabHeadless`, and `ScapsLabTests` vcxproj files sequentially through the same lock, with `-p:Configuration=Release -p:Platform=x64 -nr:false -m:1`. Run `ScapsLabTests.exe` through the lock. Build outputs are under `build/bin/Release-windows-x86_64/`.

Launch ScapsLab from its output directory. The build reuses PrototypeTemplate's install assets and overlays ScapsLab's config. The initial example is read from the checkout; its default save destination is the local `device_project.yaml`. Project/Layers/Contacts/Solver/Run/Results and four AC plots are Core docking windows. First start (or `--reset-layout`) docks Project/Layers left, Results and AC plots in center tabs, Run/Solver right. Existing layouts are retained. Results gives Bands/Carriers most of the height, with I-V in a separate tab; carrier ticks use powers of ten. Large/small numeric inputs use scientific notation. Conservation is relative to |J|, with absolute error in the tooltip (undefined at J=0). Numeric edits commit with Enter. Loading and closing protect unsaved edits.

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
                 if row['status'] == 'Converged' and all(row[key] for key in columns))
# Import numeric.getvalue(); mapping {'f': 0, 'C': 1, 'G': 2} (CV: 'V': 0).
```

Gallery: set `SCAPSLAB_GALLERY_PANEL=default` for the docked layout, or 0..9 for Project, Layers, Contacts, Solver, Run, Results, C-V, Mott-Schottky, C-f, G-omega-f. Launch with `--reset-layout --screenshot <absolute.png> --exit-after-screenshot`. Gallery solves I-V and both AC sweeps through Core. PNGs: `../tools/agents/runs/gallery/scapslab-v1/`. A portable runtime may be staged in `%TEMP%` when the build directory disallows runtime settings writes.

Validation: `ScapsLabTests` checks C/G against `solve_cv`/`solve_cf` on `device_project.yaml` at relative tolerance 1e-12, mesh/conventions, CSV, invalid requests, snapshot preservation/cancellation, and embedded Python AC/export commands. Existing I-V and YAML roundtrip tests remain.

Next: Rs/Rsh editor; illumination/QE; defects editor; heterojunction interface panel; temperature sweep; adaptive-mesh visualization.
