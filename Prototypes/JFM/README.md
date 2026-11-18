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
