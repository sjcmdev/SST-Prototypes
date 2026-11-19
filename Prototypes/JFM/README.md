# JFM

Minimal executable project generated from `PrototypeTemplate`.



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
