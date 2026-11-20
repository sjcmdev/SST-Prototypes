# Shared HDF5 command/job adapter

The storage implementation lives in `SST-Libraries/SST-HDF5`. This thin Core
adapter exists here because command dispatch, job scheduling and cancellation
belong to Core. It adds no application behavior.

For GUI hosts, call `RegisterHdf5Commands(registry, jobs)` once with the existing
Core registry and JobSystem. Create a shared `Hdf5Request`, populate `input`
with the JSON request below, obtain its `response.get_future()`, put it under
`hdf5.request` in `CommandContext`, and execute `storage.hdf5.save` or
`storage.hdf5.read` through `CommandService`. `request->jobId` identifies the
submitted Core job; read the future after completion. The command ID selects
the operation. Observe failures/cancellation through the existing JobSystem.
Each snapshot is one Core job, and the shared storage mutex serializes all
HDF5 access across jobs/sessions, including reads.

Embedded Python follows the existing nanobind built-in-module pattern:
call `SST::RegisterHdf5PythonModule()` before starting the interpreter, then:

```python
import json, time, sst_hdf5
session = sst_hdf5.Session()
accepted = json.loads(session.start(json.dumps({
    "operation": "save", "file_path": "project.h5",
    "provenance": {"run_id": "run-001", "temperature_K": 300},
    "attributes": {"title": "Example"},
    "datasets": [{"dataset_id": "/runs/run-001/potential",
                  "shape": [2, 2], "values": [0, 0.1, 0.2, 0.3], "units": "V",
                  "attributes": {"axis_units": "nm"}}]
})))
assert accepted["ok"], accepted
while (state := json.loads(session.poll()))["running"]:
    time.sleep(0.01)
assert state["response"]["ok"], state
```

`Session` owns a one-worker Core JobSystem and registry for headless use; C++
hosts may also inject their existing JobSystem. It registers and executes the
same commands and jobs as GUI hosts. One operation can be active per session;
`start` returns `ok` and stable integer `job_id`. `poll` returns `running`,
`job_id`, and `response` (null until available). `cancel` requests Core job
cancellation and returns `ok`/`job_id`; poll for the final result. Cancellation
is checked before writing, between datasets and immediately before commit;
an individual HDF5 dataset write is not preemptible.

Read request:
```json
{"operation":"read","file_path":"project.h5",
 "dataset_id":"/runs/run-001/potential","offset":[1,0],"count":[1,2]}
```
Omit offset/count for a complete read. Success returns `ok`, `dataset_id`,
`shape` (full source), `count` (returned shape), flat `values`, `units`,
`attributes`, and `file_attributes`. Save success returns `ok` and
`dataset_count`. Optional save dataset fields are `chunks` (automatic when
empty) and `compression` (integer 0–9, default 4). `provenance` is any JSON
value stored in the file's `provenance_json` string attribute.

All Python inputs/outputs are JSON strings. Physical quantities carry explicit
`units`, including arbitrary attributes whose physical field names must include
units (e.g. `temperature_K`). JSON values must be finite numbers; the raw C++
format accepts non-finite doubles, but a JSON read of non-finite
values returns a format error. The JSON adapter necessarily materializes the
request and result; use hyperslabs to keep interactive reads bounded.

Failures return `{"ok":false,"error":{"code":"...","message":"..."}}`.
Stable codes: `hdf5.invalid_request`, `hdf5.io`, `hdf5.format`,
`hdf5.cancelled`, `hdf5.busy`, `hdf5.job_rejected`, `hdf5.job_failed`,
`hdf5.no_job_system`, `hdf5.internal`. Validation errors never replace a file.
The job is marked Failed or Cancelled as appropriate; completion of command
dispatch means submission, not successful file I/O.

## Focused MSVC checks

Install the project's dev environment (`h5py` is a dev dependency). Generate
`Core/IO/Hdf5/tests/premake5.lua` with the vendored Premake, then build
`build/hdf5/generated/vs2022/Hdf5Tests/Hdf5Tests.vcxproj` in Release using
MSBuild `-nr:false -m:1`. The executable is
`build/hdf5/bin/Release-windows-x86_64/Hdf5Tests/Hdf5Tests.exe`.
Wrap project generation, build, and each test run separately with
`bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh`.
The `--to=PATH` option relocates all focused build artifacts if required.

The focused executable tests the real Core command/job implementation and
embedded binding, validates produced files with h5py, reads h5py-produced
big-endian doubles, and kills a child process after a dataset write to prove
that the old destination remains byte-for-byte intact. It also runs every
storage C++ test, including the 10-million-double resource check.

The cached h5py 3.16.0 Windows wheel uses HDF5 2.0.0 with the same DLL basename
as the official 2.1.1 vendor. Tests run h5py in a separate Python process to
avoid loading incompatible versions into one process. Do not import this
h5py wheel into an embedded host that has loaded SST's HDF5 DLL.
