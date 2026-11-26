# __PROTOTYPE_NAME__

Generate with `python scripts/generate_prototype.py __PROTOTYPE_NAME__`, then run
`Vendor/Binaries/Premake/Windows/premake5.exe vs2022` from the repository root.
Build: `python scripts/build_prototype.py __PROTOTYPE_NAME__ --configuration Debug`.
Tests: `python scripts/test_prototype.py __PROTOTYPE_NAME__ --require-embedding`.
Use `uv sync --group dev` at the repository root first; there is one environment and lockfile.

The Document menu uses Core commands: F5 evaluates, Ctrl+O opens JSON, Ctrl+S saves.
Edits increment revision and mark the document dirty. Jobs use immutable input snapshots;
only matching evaluation/open completions replace current data. An older save updates its
path but cannot clear dirty after another edit. Open requires saving pending edits first.
Progress, cancellation and errors appear in the panel and Core job monitors. Cancellation
is cooperative; an already committed atomic file replacement is still reported as saved.
The Prototype settings tab persists scientific notation in `prototype_settings.txt`.

`SetupApplication` registers `sst___PROTOTYPE_NAME_LOWER__` **before** Core initializes
Python. Core owns interpreter/GIL lifetime. `prototype_example` calls the real nanobind
module; GTest hosts the embedded roundtrip and pytest (which cannot import a built-in
module from an unrelated Python process). Without embedding GTest reports availability;
`--require-embedding` fails, and the Python suite always requires embedding.

Layout:

```text
src/App.cpp                         composition root, commands, menu, settings
src/Domain/PrototypeBackend.hpp      domain calculation
src/Storage/PrototypeDocument.hpp    validation, JSON, path/dirty/revision
src/IO/PrototypeWorkflow.*           Core jobs and main-thread completion
src/IO/PrototypePythonBindings.*     built-in nanobind registration
src/Presentation/*PrototypePanel.*   document editor and progress
python/prototype_example/            Python consumer
python/tests/                       pytest roundtrip and validation
tests/                              backend, workflow, embedded integration
install/app/config/                 neutral shipped defaults
```

Build output stays in the root `build/` directory. The executable has adjacent `install/`
and `python/` directories; CPython runtime preparation and portable environment path
resolution remain shared tooling concerns (A1 steps 5?7). No JFM data types are used.

## Diagnostics, CLI and build identity

Use **Support ? About Semiconductor Studio** for build identity and **Support ? Export diagnostics?** to preview a redacted local report. `--version` and `--export-diagnostics report.txt` work without opening a window; invalid CLI syntax returns 2. Session/run logs rotate; native crash evidence stays local. See [shared runtime documentation](../docs/PROFESSIONAL_RUNTIME.md) for retention, crash probes, recovery notices, Python metadata and EXE/PDB manifests.


## Shared settings, errors and recovery

Generated apps inherit transactional settings, explicit user/portable storage roots, section
reset with preview and Undo, typed terminal errors and a 30-second background recovery policy.
The template captures input and window layout together; recovery opens an unsaved copy.
See [shared runtime contracts](../docs/PROFESSIONAL_RUNTIME.md#error-settings-and-recovery-contracts)
for CLI exit contract v1, Python settings reset and recovery retention.
