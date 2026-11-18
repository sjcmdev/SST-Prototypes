# ScapsLab v0

Core application for the read-only sibling `scaps_core` checkout. The old viewer remains independent. No solver sources are copied. `ScapsCore` is a static library compiled with MSVC `/fp:precise`, sharing SST's yaml-cpp target.

Generate with `bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh Vendor/Binaries/Premake/Windows/premake5.exe --scaps-core=../scaps_core vs2022`. Build the generated `ScapsLab`, `ScapsLabHeadless`, and `ScapsLabTests` vcxproj files sequentially through the same lock, with `-p:Configuration=Release -p:Platform=x64 -nr:false -m:1`. Run `ScapsLabTests.exe` through the lock. Build outputs are under `build/bin/Release-windows-x86_64/`.

Launch ScapsLab from its output directory. The build reuses PrototypeTemplate's install assets and overlays ScapsLab's config. The initial example is read from the checkout; its default save destination is the local `device_project.yaml`. Project/Layers/Contacts/Solver/Run/Results are Core docking windows. Numeric edits commit with Enter. Loading and closing protect unsaved edits.

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

`execute(request)` waits without opening a GUI. `start` submits the same Core command/job as the panel; `poll` publishes completed snapshots. Successful responses contain `ok`, `document`, `result`, and `error`. Failures contain stable `error.code` and `error.message`; codes include `invalid_request`, `command_rejected`, `operation_failed`, and `cancelled`. Keep a session on its creating thread.

Operations:
- `load`, `save`: `path` (YAML).
- `inspect`: current document/results.
- `edit`: recursive object `patch`, or explicit replacement `document`. Arrays replace as a whole; preserve layer IDs. Replacement supports removing optional fields such as `flat_band`.
- `layer`: `action` = `add`, `remove`, `move`; remove/move use `layer_id`, move also uses zero-based `index`. Add duplicates the final layer's material/doping/defects and assigns a new ID.
- `run`, `cancel`: no additional fields. Temperature, voltage and solver controls come from the document; field names carry SI units. Results contain `bias_id`, consumer `voltage_V` (left minus right), signed `J_A_m2`, `conservation_error_A_m2`, convergence, mesh and profiles.

Cancellation is cooperative between bias points and after mesh preparation; scaps_core has no cancellation callback inside mesh/Newton. Nonzero external networks replay continuation prefixes for exact core behavior and may be slower. Arora uses the core's sourced `caughey_thomas` parameters; selecting it without those parameters returns a validation error. No material parameters are invented.

Gallery: set `SCAPSLAB_GALLERY_PANEL` to 0..5 and launch with `--reset-layout --screenshot <absolute.png> --exit-after-screenshot`. It solves the example through Core and maximizes that panel. PNGs are in `../tools/agents/runs/gallery/scapslab-v0/` relative to the repository.

Next: AC panels; Rs/Rsh editor; illumination/QE; defects editor; heterojunction interface panel; temperature sweep; adaptive-mesh visualization.
