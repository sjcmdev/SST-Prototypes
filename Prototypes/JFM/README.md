# JFM

Minimal executable project generated from `PrototypeTemplate`.

### Real-measurement tests

Python and native tests look under `example-data/real-measurement` by default.
This private dataset is gitignored. Set `SST_JFM_REAL_DATA` to an external data
root containing `dat/ivd_HZB25_T*.dat` and/or `C002/IVT .../*.dat` to test it
without copying files into the repository. Tests skip only missing measurement
files, with the missing path and this variable in the reason; existing files
still run all assertions. Synthetic and other self-contained tests always run.
For datasets stored separately, run each group with its corresponding root:

```powershell
$env:SST_JFM_REAL_DATA = 'D:/data/HZB' # contains dat/
# Run test_jfm_hzb_ground_truth.py, or SelectedHzbCharacteristicHasVisibleSemilogPoints.
$env:SST_JFM_REAL_DATA = 'D:/data/measurements' # contains C002/
# Run test_jfm_import_smoke.py and the native import/descending-sweep tests.
```

Completed fit and Monte Carlo jobs append to the project's persistent run registry
(`fit_runs` / `monte_carlo_runs`: stable ID, characteristic name, completion status).
Save/reopen retains these entries; reruns keep earlier entries. Imported completed
analysis also registers its latest fit and MC. Per-characteristic results and MC
output files retain their existing storage contract.



### PlotLab figures (J2)

Measurement, prefit, fit, tuned, Arrhenius and Monte Carlo figures own their plotted
columns and stable series/row IDs in PlotLab-Engine. JFM's existing project format
and domain calculations remain authoritative; refreshing a figure does not add undo
steps. The previous Monte Carlo Python subprocess/temporary TSV image path is removed.

Use **Export SVG/PDF/PNG...** on a figure for physical width/height in mm, DPI and
PL4 publication fonts (bundled DejaVu or an explicit TTF/OTF). Export uses the last
rendered viewport, ticks and visibility, without refitting. The immutable snapshot
runs through Core Commands and JobSystem, the same matplotlib exporter as PlotLab,
and atomic file replacement. Completion/failure appears in the export popup and
Core job history. **Save Plot** / batch MC image export submit PNG jobs as before;
plots never shown obtain their initial bounds from the existing MC options.

The embedded nanobind module exposes `sst_jfm.plot(request)` with JSON-compatible
inputs/results. `operation="snapshot"` or `"export"` takes `project` (the engine
project JSON object), `graph_id`, explicit `ranges=[xmin,xmax,ymin,ymax]`, optional
`x_ticks`/`y_ticks` (`value`, `label`, `major`) and numeric-ID-keyed `visibility`.
Publication overrides are `width_mm`, `height_mm`; exports additionally take `path`,
`format` (`svg`, `pdf`, `png`) and `dpi`. Snapshot results contain `snapshot`;
exports return a Core `job_id`. Query it with `{"operation":"status","job_id":id}`.
Failures return `ok=false` and a stable `error.code` with `error.message`.
The engine project JSON includes publication font settings. Existing JFM YAML files remain unchanged.

### Fitter and headless API

LM policy 425 is the default for single fits and Monte Carlo on CPU and CUDA.
Choose NM in Fit method to use NM-real directly. LM cap/domain/damping stops run
NM-real automatically, followed by LM precision polish; the lowest objective is
retained. LM and polish each use at most 300 iterations; NM uses the configured
budget. Reported iterations include every attempted phase.

The embedded `sst_jfm.fit(request, method="lm")` also accepts `method="nm"` and
uses Core Commands, JobSystem and the same JFM fit jobs as the GUI. The request
contains `model_id`, `temperature_K`, `voltage_V`, `current_A`, `parameters`
(`I0_A`, `ideality_factor`, `Rs_ohm`, `Rsh_ohm`, `alpha`,
`Rsh2_V_pow_alpha_per_A`, `Iph_A`), optional `fit_range_V`, `bounds` (`name: [min,max]`),
`fixed_parameters` (the same parameter field names), `max_iterations`, and `backend="cpu"|"cuda"`. Parameters use
A for I0/I_ph, ohm for Rs/Rsh, V^alpha/A for Rsh2, dimensionless A/alpha.
For Monte Carlo, add positive `samples`, `noise_percent`,
`start_perturbation_percent` and `seed`; the supplied parameters define the ideal
curve and the start. GPU unavailability falls back to CPU and reports the actual
backend. The original positive-current relative-log objective is unchanged.
Use `current_density_A_per_cm2` instead of `current_A` for density data, with
`I0_A_per_cm2`, `Rs_ohm_cm2`, `Rsh_ohm_cm2`,
`Rsh2_V_pow_alpha_cm2_per_A`, and `Iph_A_per_cm2`. The API preserves these
numeric density values and returns correspondingly named ideal/noisy density
columns; it does not assume a device area or silently change log-ratio weights.


Results are JSON compatible, with stable project/characteristic/job/sample IDs,
`fit_method` (LM, LM→NM, NM), `accepted`, `converged`, `stop_reason`,
`lm_status` (0 cap, 1 stationary, 2 domain, 3 damping) and `flat_directions`.
MC retains its existing clean-curve chi_squared and separately exposes the
minimized noisy-data `objective_chi_squared`. A flat direction is a local relative
Jacobian rank diagnostic (singular-value ratio <=1e-8), not a convergence claim;
flat fits are never counted as converged. Errors have `ok=false` and a typed
`error.code` (`jfm.fit.validation` or `jfm.fit.failed`).

New TSV exports write true stationarity in both `converged` and `solver_converged`;
`accepted` remains the finite-result flag. Older files retain their accepted-sample
interpretation when loaded. Flat fits are exported with `converged=0`.


Objective settings on `sst_jfm.fit(request)` (same Core command/job as the GUI):
`objective_mode="auto"|"isc_shift"|"signed_sigma"|"log_legacy"`,
`objective_switch_k=3`, `sigma_abs_A=0` (density requests: `sigma_abs_A_per_cm2`).
Zero floor means derive `1e-4 * max(upper median abs current, 1e-12 * max abs current)` from the input data;
the absolute floor remains active when relative noise is zero.
The floor is fixed before fitting and reported in `objective`, with `shift_A`, `sigma_rel`,
`switch_k`, `linear_points`, `log_points` (density fields carry `_A_per_cm2`).
Auto preserves the legacy objective bitwise for entirely positive data within the selected fit range; signed curves use
isc_shift. Use an explicit statistical mode for dark statistical inference.

`isc_shift` uses S=-I_data(0), interpolated from the full measured curve (no extrapolation),
never fitted Iph. For t=k*sigma, h(x)=log(x/t) above t and (x-t)/t below it;
r=max(I_data+S,t)/sigma * [h(I_model+S)-h(I_data+S)].
This is the propagated log residual on the log branch and exactly (model-data)/sigma
when both arguments are in the linear branch; the tangent continuation makes the residual
and analytic Jacobian continuous when the model crosses the threshold too.
`signed_sigma` uses (model-data)/sigma. Sigma = sigma_rel*abs(data)+sigma_abs.
MC generates additive Gaussian noise with sigma from the nominal curve, holds that sigma
and the measured shift fixed, and uses them for fitting and clean-curve parameter distance.
`objective_chi_squared` is the noisy-data fit statistic (dof=N-free parameters);
`chi_squared` is the fitted-to-nominal curve distance used by confidence classes (asymptotic
chi-square with free-parameter dof). These statistics have different dof, not different weights.
Legacy zero-noise MC reports a zero confidence sentinel. Legacy retains its historical final statistic
and relative noise for regression only: 1-log(model)/log(data) is singular at 1 current unit,
unit dependent, and cannot be interpreted as statistical chi-square.

## Diagnostics, CLI and build identity

Use **Support ? About Semiconductor Studio** for build identity and **Support ? Export diagnostics?** to preview a redacted local report. `--version` and `--export-diagnostics report.txt` work without opening a window; invalid CLI syntax returns 2. Session/run logs rotate; native crash evidence stays local. See [shared runtime documentation](../../docs/PROFESSIONAL_RUNTIME.md) for retention, crash probes, recovery notices, Python metadata and EXE/PDB manifests.


## Settings and crash recovery

Settings validate a complete candidate before applying it. Preferences use a per-user root;
`SST_PORTABLE_ROOT` explicitly selects a portable root. Settings sections provide a reset preview,
Undo and Apply & Save. Dirty projects receive a separate recovery snapshot every 30 seconds;
after interruption, **Recover project** opens a copy for review and Save As. Errors distinguish
partial completion, cancellation and timeout. See the [shared contracts](../../docs/PROFESSIONAL_RUNTIME.md#error-settings-and-recovery-contracts)
for paths, Python reset, CLI exit contract v1, retention and safe `.writing` recovery.
