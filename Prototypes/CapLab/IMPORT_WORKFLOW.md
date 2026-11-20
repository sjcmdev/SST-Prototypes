# Import sources (clfiles1)

Import preview and OS file drop use `caplab.preview`, the shared source collector,
Core jobs and the same confirmation command. Select new samples or an existing
sample by stable ID. The preview groups source blocks by T [K], f [Hz], bias [V],
measurement type and variant. Selected sources commit atomically as one Undo entry.
Set `target_sample_name` to collect a batch in one new sample (also created in that
Undo entry), or `target_sample_id` for an existing one. Omit both for per-source
samples. An explicit area in the import options is retained on the new target.

Named formats contain PlotLab-Engine `ImportOptions`, including decimal separator,
mapping, units, parser profile, explicit ImZ sign and filename rules. Save/load/delete
uses `caplab.profile`; project JSON v1 has the additive `import_profiles` field.
The GUI also saves profiles with Core atomic IO in `caplab-import-profiles.json`.
Old projects without profiles remain readable. This checkout has no `Import.cpp`
or named import-filter store cited in the gap note; the implementation extends
the actual shared `Extensions.hpp`/`Extensions.cpp` options contract.

Filename/folder rules use `{stem_0}`, `{folder_1}`, `{parent_folder_0}` or JFM's
shared custom variables (source, token index, substring start/length). Rules are
candidates for sample name, treatment, temperature_K, frequency_Hz and bias_V.
Headers win by default. `use_metadata_candidates` explicitly selects rule values;
explicit per-field options select individual values. Conflicting values require
`confirm_metadata_conflicts` before commit. Preview retains both values and their
provenance; missing metadata stays null. JFM retains its existing precedence and
schema 1–5 behavior and now uses the shared token/slice helper.

`reimport(measurement_id, path?)` rereads the source using saved parser options and
returns `source_draft` with old/new metadata, hashes, row counts and changed-cell
count. `accept_source(draft)` checks the base project and commits through Core Undo.
Measurement/column IDs remain stable; dependent analyses become stale. A changed
source-row map or X grid allocates new row IDs and disables approved defect points
until reassigned. Historical row links become unresolved. Schema/variant changes
require importing a new measurement so existing graph bindings cannot change meaning.
SCAPS blocks retain their block index; historical multi-sample bundles are not
silently replaced by a single measurement.

`relink(measurement_id, path)` previews a moved file, verifies the original SHA-256
and updates references to that source in provenance, including analysis snapshots.
Relink hashes the file without parsing it or reinterpreting filename/folder rules;
renaming the source or its extension does not change measurement metadata.
It preserves raw/SI values, IDs, units, hashes and numerical result validity. A
different hash requires re-import. Source-change jobs support cancellation; failed
or stale drafts leave the project unchanged. Python uses `caplab.execute` with the
same JSON request/response and typed error envelope as existing operations.

Only evidenced formats are supported: repository lab Cp/Rp, C/G, explicit ReZ/ImZ,
CV, raw DLCP, nine-column ProPho DL/CV, SCAPS .cf/.cv, ProPho manifests/projects.
No instrument-specific adapter was invented from column/unit detection.

**Owner question:** which instrument models and native IS export formats should
be supported? Representative source files and their ImZ convention are still needed.
I1–I5/D1 remain regression fixtures; Unicode and moved sources have dedicated tests.
