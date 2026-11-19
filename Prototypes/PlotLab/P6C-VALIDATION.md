# PlotLab P6c validation ? 2026-11-19

Implemented stable column/worksheet multi-key sort, per-column non-destructive filters, coloured cell/row masks, find/replace, transpose, stack/unstack, split/join, extraction, sampling/group averaging, four normalization modes, linear interpolation, and linked column/row statistics. GUI and embedded `sst_plotlab.WorksheetSession.command()` share Core jobs and undo commands. Existing EditorWindow integration is retained.

## Builds and tests

All results below were run in this job. Debug x64, MSBuild 17.14.40/v143, C++latest, dynamic Debug CRT. Every generation/build/test/GPU command used `tools/agents/build-lock.sh`; MSBuild used `-nr:false -m:1`.

Built in the repository directories:

- `D:/SJCM/01_SemiconductorStudio/SST-Libraries/build`
- `D:/SJCM/01_SemiconductorStudio/SST-Prototypes/build`

No temporary build fallback was required. The complete libraries solution was built first. PlotLab and PlotLabTests then built against those libraries and the existing unchanged Core/dependency outputs with `BuildProjectReferences=false`.

| Suite | Before | Final passed | Final failed |
|---|---:|---:|---:|
| PlotLab-EngineTests | 47 | 55 | 0 |
| PlotLabTests | 32 | 38 | 0 |
| PlotLab pytest | 43 existing tests | 44 | 0 |
| SST-LibrariesTests aggregate | not baselined | 123 | 0 |
| UI-LibraryTests | not baselined | 31 | 0 |

The aggregate includes the 55 engine and 31 UI tests; these are not additional distinct tests. Baseline pytest also demonstrated that the newly added embedded data-operation test failed against the old executable (`Unknown worksheet action`), while all 43 existing tests passed. The first app baseline invocation used the wrong working directory and was rerun successfully from SST-Prototypes; the successful XML is `build/p6c-app-before.xml`.

Final XML/logs:

- SST-Libraries `build/p6c-{PlotLab-EngineTests,SST-LibrariesTests,UI-LibraryTests}-after.{xml,log}`
- SST-Prototypes `build/p6c-app-after.{xml,log}`, `build/p6c-pytest-after.{xml,log}`
- Build logs: libraries `build/p6c-build.log`; prototypes `build/p6c-{PlotLab,PlotLabTests}-build.log`.

Coverage includes stable nested ordering, column-sort confirmation, all filter kinds, unchanged raw source values, live filter refresh, filtered graph gaps, mask colours/row identities, exact data-byte undo/redo including signed zero and NaN payloads, project roundtrips, operation validation, malformed persisted view indices, normalization values/units, masked statistics/analysis inputs, linked row-statistics resizing, explicit formula filter exclusion, actual nanobind/Core headless operations, stale worker rejection, shared Core pool lifetime, and release of published job snapshots.

The existing formula-history budget remains intact: ten formula edits over 1,000,000 rows retain **9840 bytes < 10000 bytes**. Logical data/metadata and raw value bytes round-trip through undo/redo; project commit/revision and ID reservation counters intentionally remain monotonic.

## 1,000,000-row acceptance measurement

Final Debug headless Core job run, two numeric columns, reverse-ordered X, stable ascending sort; inclusive numeric range filter retaining 500,001 rows. Times are local measurements, not controlled Release performance claims.

| Operation | Owner-thread submission | Worker preparation + validation | Total through publication |
|---|---:|---:|---:|
| Sort | 0.3302 ms | 14873.7923 ms | 14886.8 ms |
| Filter | 0.2376 ms | 783.8474 ms | 787.376 ms |

Sorting, filter evaluation, diff construction and model validation run in Core workers. The owner thread publishes prepared immutable buffers via the undoable command. A changed project or changed identity reservations reject the result. GUI uses the application's Core JobSystem; isolated headless documents lazily own one worker. Completed history entries release large snapshots. Retry requires resubmitting the worksheet command for a fresh snapshot.

## Visual verification

Actual `PlotLab.exe --open .../demo.csv --screenshot ... --screenshot-after-frames 60 --exit-after-screenshot --reset-layout`, Debug OpenGL, 1920?1080. Both final PNGs were opened and inspected:

- [Sort](../../build/p6c-sort.png)
- [Filter](../../build/p6c-filter.png)

New controls use LabelledControls with labels on the left; physical bounds are draggable controls in column units. New selectors scope IDs by full stable IDs, including duplicate display names. Sort/filter controls and worksheet header labels are not clipped. No visible ID-conflict highlight or assertion occurred. Logs: `build/p6c-{sort,filter}.log`. Isolated config directories: `build/p6c-capture-{sort,filter}`. Initial capture attempts from a nested tool working directory could not write PNGs; rerunning from the repository tool context with a nested process working directory succeeded without escalation.

## Boundaries

- Join appends rows by matching column position/type/units; it is not a relational key join. Find/replace matches whole typed cell values.
- Transpose/stack require homogeneous column types. Unstack uses text keys and pads ragged groups with missing numeric values/empty strings.
- Frozen/ranged producers retain the existing detach guard for worksheet row changes. Column-only sorting requires explicit row-correspondence confirmation.
- Masks appear as coloured cells and cross markers in graph previews. Statistics omit masks; `analysis_column` supplies masked-as-null numeric input for fit adapters. PlotLab currently has no registered fit engine.
- Derived transformation sheets are independent with persisted provenance. Statistics are linked formula producers and row outputs track source row identities/extent.
- No Release/Dist, other platforms/DPI, or peak RAM measurements were performed. Existing u8path deprecation warnings were not suppressed. No dependencies, git commands or commits were added.
