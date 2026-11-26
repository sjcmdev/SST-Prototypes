# Local diagnostics and build identity

JFM, CapLab, PlotLab and applications created from PrototypeTemplate use the same Core implementation.

## CLI

`--help` and `--version` return without bootstrapping a window, configuration or Python. Unsupported arguments, missing values, invalid log levels and incompatible actions return **2**. Runtime/file failures return **1**. Valid GUI startup and successful headless actions return **0**. Screenshot controls require `--screenshot path.png`; exact integers must be positive. CapLab keeps its positional file alias. PlotLab validates its `--worksheet-script path.py` before starting Python and accepts its existing preview/smoke flags.

`--export-diagnostics report.txt` writes a redacted local report through the same typed `app.diagnostics.export` Command → Job used by the GUI and Python. No window or Python runtime is needed. A CLI export includes build metadata and interrupted-session count; the GUI additionally includes the last 200 log records as metadata.

## Logs

The default root is `%LOCALAPPDATA%/SemiconductorStudio/<exe-name>/diagnostics`. `SST_DIAGNOSTICS_ROOT` selects an explicit writable root for automation. A configured log filename or `--log-file=path` selects its parent as the log root; the actual file is `<root>/sessions/session-<id>/application.log`. Processes do not append to a shared `SST.log`.

Each session/run file rotates at **4 MiB**, with two backups. Messages retain at most **4 KiB**, with an explicit truncation marker; the existing UTF-8-safe job-history limiter is shared by the logger. Keep **20** inactive sessions and **128** run directories per output root; active directories are locked and never deleted. Retention requires an SST ownership marker and preserves foreign directories. A killed process releases its lock so later sessions can prune it. Run retention deletes diagnostic logs only, never published results. INFO is the default; Trace/Debug are opt-in. Flush occurs on errors, run completion and shutdown. An abrupt kill may lose buffered informational log lines.

File records carry UTC, severity/category, source/thread, build SHA256, session/job/run/attempt/member IDs, operation, engine, error code and monotonic elapsed milliseconds. `LogEntry` retains typed context in LogRegistry. JobSystem propagates run identity to children and assigns a new attempt for retries. `IJob::GetLogContext()` can supply stable run/member IDs, engine/code and the existing run output directory; otherwise Core keeps run logs under the session. `ScopedLogContext` attaches context to additional worker threads; `JobContext::Log*` restores job context even when called from another thread. No arrays or per-frame progress are logged by this mechanism.

## Crash and support

**Support → About Semiconductor Studio** shows the identity compiled into Core. **Support → Export diagnostics…** previews the exact report before saving. It includes version, dependency hashes, capabilities and log severity/category/job/attempt/elapsed metadata. Free-form messages, paths, settings values, scripts, raw data and dumps are excluded by an allowlist. Logs and dumps remain private local artifacts and can contain sensitive information. There is no upload or telemetry service.

A startup marker is locked while a session is alive. The next GUI startup detects a crashed/killed process and displays **Previous session interrupted**. **Dismiss notice** acknowledges the marker and preserves existing evidence. This notice does not implement document autosave/restore (F13).

`--crash-probe` deliberately raises a native exception in this process without opening a window. `--crash-probe=terminate` and `--crash-probe=signal` exercise the C++ terminate and abort handlers. `--crash-probe=wait` announces readiness and waits for an external termination, for kill/restart tests. Run probes only with an isolated diagnostics root and no unsaved work in that process.

The native exception/terminate/signal handlers ask a separate, hidden copy of the same executable to call Windows `MiniDumpWriteDump(MiniDumpNormal)`. The parent keeps the exception context alive for at most 15 seconds. Dumps and `build.txt` stay under `diagnostics/crashes/session-<id>/`; startup keeps the latest 20 inactive crash sessions and preserves active ones. The helper validates the target executable and output root. A dump is best-effort: OS resource exhaustion, process creation restrictions, debugger interception or a hard kill may prevent capture. Hard kills still leave an interrupted-session marker. Dumps are never included in the redacted report; sharing them requires a separate deliberate action and review. Keep the matching EXE, private PDB and `.build.json` together. Do not share PDB/dumps as a default support attachment.

## Version and release records

`pyproject.toml` is the SemVer source of truth. The Core prebuild step generates `BuildInfo.generated.hpp` and `.json` from the source/dependency content, build UTC and **owner/CI-supplied** `SST_SOURCE_REVISION`. No git command is run. An absent source ID appears as `unknown`; that build is unsuitable for a tagged release. `SOURCE_DATE_EPOCH` fixes the UTC for reproducible builds. About, `--version` and `sst_application.version_info()` read the same typed `BuildInfo`; schema versions and future DLL ABI versions remain separate.

Windows executables also embed `VERSIONINFO` from the same generator; File Properties shows the product version and source/build details. Numeric Windows version components are limited to 65535; the full SemVer remains in the string fields. Release/Dist builds emit private PDBs. Every host postbuild writes `<app>.build.json` with source/dependency hashes and SHA256 of that exact EXE and PDB. The manifest excludes absolute paths. Update [CHANGELOG](../CHANGELOG.md) for API/schema changes, migration instructions and known qualification failures before publishing. This package does not choose a publisher, sign artifacts or create an installer.

Embedded Python (with the existing interpreter initialized, also headless) exposes `sst_application.version_info()` as a JSON-serializable dict and `sst_application.export_diagnostics(destination_path)` as `{ok, error?: {code, message}}`. Conversion happens in the nanobind edge. The export runs the same Core Command → Job without requiring a window; no JSON is used between C++ modules.


## Error, settings and recovery contracts

The shared terminal adapter preserves a namespaced error code, category, actionable message,
details, suggestion, retryability and job/run identity. Cancellation, timeout and partial
completion stay distinct; completed member results are retained. The Progress Monitor shows
the message and remedy first, with technical details expandable. Python errors expose the same
fields at the binding edge.

CLI exit contract **v1**: 0 completed; 2 invalid input or unsupported schema; 3 numerical failure
or partial completion; 4 file IO; 5 internal/unavailable/resource failure; 124 timeout;
130 cancellation. Scripts should accept only 0 as complete success. Python can inspect
`sst_application.exit_contract()`. Partial results must be written before returning a nonzero
exit code. This versions the shared application contract separately from project schemas.

Settings load into a candidate, reject malformed values or future schema versions, validate,
then commit. Missing fields retain typed defaults; a rejected profile cannot partly change
the current settings. Installed apps keep preferences under
`%LOCALAPPDATA%/SemiconductorStudio/<app>/config`; packaged defaults/assets remain read-only.
For explicit portable mode set `SST_PORTABLE_ROOT` to a writable folder before starting the app;
preferences then live under `<root>/<app>/config`. Existing preference files, profiles, themes
and layouts are copied only when the new destination is missing; originals are preserved.

In Settings, **System**, **Interface** and **Viewport** offer **Reset this section...**.
The confirmation describes the affected section. Reset changes the preview; Undo restores it,
and **Apply & Save YAML** persists it. Other sections and project data are preserved. Headless
Python uses the same typed reset Command → Job: `sst_application.reset_settings("interface",
persist=True)`; omit `persist` to preview without saving.

JFM, CapLab, PlotLab and generated prototypes checkpoint dirty documents every **30 seconds**.
They capture typed snapshots on the UI thread and write them in one background Core job per
app; unchanged revisions are skipped. Each document keeps five completed generations and at
most one unfinished generation in its current session. Ready markers commit a generation only
after its project files are complete. OS leases exclude active sessions from recovery.

After an interrupted session, **Recover project** offers **Restore copy**, **Discard snapshot**
with confirmation, or **Later**. Restore preserves the original project, opens an unsaved copy,
and retains the old checkpoint until a new recovery copy commits. Check the result and use
Save As. Python/code evaluation is not automatically trusted by a restored PlotLab file.

PlotLab project saves reserve an OS lease alongside `.writing`. After a killed current writer,
the next save preserves unfinished bytes in `.writing.interrupted-*` and continues. Unknown
legacy `.writing` directories are preserved: close all writers, back up/rename that directory,
then retry. These guarantees cover process interruption; a storage-device power failure still
requires an independent backup policy.
