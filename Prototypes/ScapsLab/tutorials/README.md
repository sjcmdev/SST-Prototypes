# ScapsLab tutorials

Tutorials > First device / Light I-V and QE / History and Sweep / Load grading profile / Nt(x) defect profile / G(x) from file / Spectrum and absorption files / Compare runs, then Sweep / Export profile are listed in Tutorials > Lessons... (title, description, duration, Start). Start runs the lesson interactively (learn mode): the coach highlights the control and waits for you; it never clicks for you. Buttons: Back, Skip (Next when the step is already satisfied), Show me (performs that one step), Exit; the header shows "Step 3 / 9". The automated verify run lives under Help > Developer > Verify tutorials and in the `--tutorial-verify` CLI. Lessons add a tutorial device to the current project; they do not replace or save the user's devices. Automatic recalculation is suspended during a lesson and restored on completion or Exit.

Progress is saved beside ScapsLab's per-user layout in `scapslab-tutorials.json`. The Tutorials menu displays progress. A new visit restarts the lesson; the Python `resume=True` option resumes the recorded learn step when the matching project state is available. Verify runs do not change user progress.

Headless CLI (no window or display required):

```text
ScapsLab.exe --tutorial-verify first-device --tutorial-report first-device-report.json
ScapsLab.exe --tutorial-verify light-iv-qe --tutorial-report light-iv-qe-report.json
ScapsLab.exe --tutorial-verify history-sweep --tutorial-report history-sweep-report.json
```

The default report is `tutorial-report.json`. Exit codes: 0 PASS, 1 tutorial failure, 2 invalid CLI or report I/O failure. Reports contain each step's PASS/FAIL/SKIP and typed error code. CI runs all three real-solver lessons in `ScapsLabTests` alongside engine tests and an offscreen OpenGL screenshot test. Build/test/GPU invocations in this workspace must use `tools/agents/build-lock.sh` as required by the owner.

Embedded Python (the existing nanobind Session and Core command/job path):

```python
import scapslab
session = scapslab.Session()
lessons = session.tutorials()
report = session.tutorial_start('light-iv-qe', mode='verify')
assert report['ok'], report
```

`start({'operation': 'tutorial_start', 'tutorial_id': ..., 'mode': ...})` and `poll()` also support asynchronous use. `tutorial_state()` returns the report and saved progress; `tutorial_stop()` stops future steps. Tutorial commands modify the current session just like GUI commands. `mode='learn'` observes user/API actions without executing the lesson's commands.

## Lesson inventory

Every feature except the crash-recovery dialog has a lesson: first-device, light-iv-qe, history-sweep, load-grading-profile, nt-defect-profile (Gauss / erfc / file), generation-from-file (.gen), spectrum-absorption-files (.spe, .abs), compare-runs-sweep, export-profile (CSV / .grd), filter-optical-files (.ftr, .opt), material-file (.material), table-undo-redo, menus-tour (File / Edit / View / Simulate / Tools / Tutorials).

The crash-recovery dialog has no lesson on purpose: it only appears after an unclean exit with an autosave present, and a lesson cannot trigger that safely.

`ScapsLabTests` (ScapsLabTutorialUi) renders every step of every lesson offscreen and checks that the target resolves to a visible widget, that the coach window does not cover it and that the arrow tip stays outside it. Set `SCAPSLAB_TUTORIAL_SHOTS=<dir>` to also write one PNG per step to `<dir>/<lesson>/<step>.png`.
