Run from the SST-Prototypes root:

```powershell
.venv/Scripts/python.exe scripts/snap_apps.py --build --test
```

The optional Premake entry point adds `CoreScreenshotTests` and materializes the
current `PrototypeTemplate` into `build/gallery-template` as `GalleryTemplate`.
Builds use `build/`; every build/test uses `tools/agents/build-lock.sh`.
Gallery launches use isolated configuration copies in `build/gallery-runs`.
For the smaller apps, the gallery seeds an on-screen workspace layout and
collapses Core tool panels in those copies. JFM retains its existing layout.
The default PNG destination is `tools/agents/runs/gallery/shots` alongside
`report.json`, including dimensions, file size, variance and open-command dispatch.
Dispatch confirms command acceptance, not completion of asynchronous data loading.

All apps receive these Core options:

* `--screenshot <path.png>`: capture the main window's default back framebuffer
  after rendering, flip OpenGL rows, and write PNG through `app.screenshot` and
  the Core PNG job. The destination directory must exist. Write failure returns
  a nonzero exit code.
* `--screenshot-after-frames <N>`: positive frame count, default **30**.
* `--exit-after-screenshot`: queue a normal window-close event after PNG writing;
  existing unsaved-data close guards still apply.
* `--open <path>`: execute a registered `app.open` command with a `Path` in the
  `path` context field. Existing `caplab.open` and `jfm.project.open` commands are
supported directly. An app without a path-aware handler logs a warning.

PlotLab's current `plotlab.open` ignores command context and opens a file dialog;
its CSV import also requires GUI interaction. Core does not dispatch that command
at startup. Completing PlotLab project/CSV startup opening requires an app handler
in `Prototypes/PlotLab`, outside this task's Core/scripts-only change scope.

Only the main framebuffer is captured. Detached ImGui multi-viewport windows
outside the main window are excluded, as are native window borders.

The embedded nanobind module exposes the same screenshot command/job:

```python
import json, sst_application
result = json.loads(sst_application.execute(json.dumps({
    "command_id": "app.screenshot", "path_png": "shot.png"
})))
# Startup opening uses the same registered app command/job as the CLI and GUI:
opened = json.loads(sst_application.execute(json.dumps({
    "command_id": "app.open", "path": "project.caplab"
})))
```

Call it on the render thread after rendering. Without a current GL context
(including a headless Python call), it returns a JSON error with the stable code
`app.screenshot.no_gl_context`. Other errors include `app.request.invalid`,
`app.screenshot.invalid_path`, `app.screenshot.readback_failed`, and
`app.screenshot.write_failed`. Paths are strings; dimensions in reports use `_px`.

CapLab fixture creation uses `caplab.project_from_preview` in its existing
embedded test host. No owner projects or installation settings are edited.
