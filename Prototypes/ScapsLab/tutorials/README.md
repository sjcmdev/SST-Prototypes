# ScapsLab tutorials

Tutorials > First device / Light I-V and QE / History and Sweep / Load grading profile / Nt(x) defect profile / G(x) from file / Spectrum and absorption files / Compare runs, then Sweep / Export profile offer learn, demo, and verify modes. Lessons add a tutorial device to the current project; they do not replace or save the user's devices. Learn mode highlights real controls and waits for the user. Automatic recalculation is suspended during a lesson and restored on completion or Stop.

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
