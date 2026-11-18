# CapLab M1

Generated from `PrototypeTemplate`; Core owns the window, commands, EventBus,
Undo, jobs, progress, diagnostics and embedded Python lifetime.

From the SST-Prototypes root (one build/test at a time):

```powershell
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh Vendor/Binaries/Premake/Windows/premake5.exe vs2022
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --app --configuration Debug
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py CapLab --require-embedding
```

`check.py` calls the template build/test tools and supplies the owner's direct
`.vcxproj` MSBuild flags: Debug x64, `-nr:false -m:1`, absolute `SolutionDir`.
Outputs stay in root `build/`; there are no commits or new dependencies.
Run `build/bin/Debug-windows-x86_64/CapLab/CapLab.exe` from the repository root.

Use **CapLab > Import lab AS** and select `tests/fixtures/cf_100K_7.dat` from this
prototype. Inspect f [Hz], Cp [F], Rp [Ohm], row count, and temperature before
**Confirm import**. The filename suggests 100 K; a header takes precedence.
Missing temperature stays null. Decimal comma is explicit; use whitespace or
semicolon between columns. Invalid rows fail with their line number; no rows
are silently removed. Extra numeric columns remain in the engine worksheet.
The M1 profile is exclusively lab Cp/Rp, never SCAPS C/G.

Confirmation is one Core Undo entry. The sample tree and C-f graph use engine
workbooks, worksheets, columns, stable row/object IDs and a registered plot
preset. Preview uses UI-Library; PNG export reuses PlotLab's owning snapshot
and embedded matplotlib adapter, compiled read-only from the existing host.
The export Python package is copied beside the executable.

Ctrl+S saves a self-contained `*.caplab.json` through Core atomic replacement;
Ctrl+O opens it without the original data files. Unsupported/corrupt projects
are rejected; failed or stale open leaves the document unchanged. Save uses
an immutable serialized snapshot; later edits keep dirty set. Save pending
changes before opening. Progress/cancel/errors appear in the CapLab panel
and Core job monitors. Cancellation after atomic replacement still records
successful save. A new preview replaces the previous pending preview only;
it does not modify the project until confirmation.

Regression suites use the same runner:

```powershell
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py PlotLab --require-embedding
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh python Prototypes/CapLab/check.py JFM --require-embedding
```

The template runner uses the small `prototype_example` marker to run pytest
inside the GTest embedded interpreter, where the real built-in `sst_caplab`
module is registered. The public Python consumer is `caplab`.

M2+ import profiles, analyses, material data, IS and Walter are outside M1.
Frozen fixture origin, SHA-256 and independent PFS I2 expectations are recorded
in `tests/fixtures/manifest.json`.
