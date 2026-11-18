# Origin 2024 reference gallery — org1b

**Status: 26/26 figures rendered, 36 size variants visually inspected; 3/3 local tests PASS. Getter and physical-size limitations are documented below.**

The fixtures contain 16 real HZB I-V(T) sweeps (181–330 K), synthetic C-V/Mott–Schottky and Arrhenius data, custom ticks, and a second I-V dataset with current ×1.2. `fixtures/provenance.json` records source paths, SHA-256 hashes and formulas. Current units A remain an explicit assumption because the original project unit fields are empty. The third original source column has unknown units.

## Regenerate

From the repository root:

```powershell
uv run --no-project --with originpro --with pillow python -I Prototypes/PlotLab/experiments/origin-reference/generate_gallery.py
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh uv run --no-project --with pillow --with pyyaml python -I Prototypes/PlotLab/experiments/origin-reference/validate_gallery.py
```

The generator uses one owned Origin instance per run and calls `op.exit()` in `finally`. It creates a new project, embeds CSV data, replaces named exports, and saves `origin-reference.opju`. Both page-size variants are retained as separate graph pages. Figure 17 saves `templates/iv-reference.otpu`; figure 19 opens that template against the second dataset. `templates/temperature-native.otpu` retains native mapping by user parameter T. The included `templates/temperature-seed.otpu` is an input captured from Origin 2024; no GUI interaction is required for regeneration. The generator's packed mapping value was obtained from Origin 2024's UI (By Plots → Column Label Row T → Color Mapping).

Each standard stem has `exports/<stem>.png`, `.svg`, `.emf`, and `ground_truth/<stem>.json`. Figures 17–26 additionally have files suffixed `-160x120`. PNG is exported at 600 dpi; SVG and EMF remain original Origin vectors. `manifest.json` is the machine-readable index.

## Figure index

| ID / stem | Features | 80×60 mm | 160×120 mm |
|---|---|---|---|
| 01-log10 | Log10; 10^n labels | [PNG](exports/01-log10.png) · [SVG](exports/01-log10.svg) · [EMF](exports/01-log10.emf) · [JSON](ground_truth/01-log10.json) | — |
| 02-engineering | Engineering prefixes | [PNG](exports/02-engineering.png) · [SVG](exports/02-engineering.svg) · [EMF](exports/02-engineering.emf) · [JSON](ground_truth/02-engineering.json) | — |
| 03-reciprocal | Reciprocal scale | [PNG](exports/03-reciprocal.png) · [SVG](exports/03-reciprocal.svg) · [EMF](exports/03-reciprocal.emf) · [JSON](ground_truth/03-reciprocal.json) | — |
| 04-linked-top | Bottom T, top 1000/T linked by formula | [PNG](exports/04-linked-top.png) · [SVG](exports/04-linked-top.svg) · [EMF](exports/04-linked-top.emf) · [JSON](ground_truth/04-linked-top.json) | — |
| 05-tick-in | Tick direction in | [PNG](exports/05-tick-in.png) · [SVG](exports/05-tick-in.svg) · [EMF](exports/05-tick-in.emf) · [JSON](ground_truth/05-tick-in.json) | — |
| 06-tick-out | Tick direction out | [PNG](exports/06-tick-out.png) · [SVG](exports/06-tick-out.svg) · [EMF](exports/06-tick-out.emf) · [JSON](ground_truth/06-tick-out.json) | — |
| 07-tick-both | Tick direction in & out | [PNG](exports/07-tick-both.png) · [SVG](exports/07-tick-both.svg) · [EMF](exports/07-tick-both.emf) · [JSON](ground_truth/07-tick-both.json) | — |
| 08-minor-1 | One minor tick | [PNG](exports/08-minor-1.png) · [SVG](exports/08-minor-1.svg) · [EMF](exports/08-minor-1.emf) · [JSON](ground_truth/08-minor-1.json) | — |
| 09-minor-4 | Four minor ticks | [PNG](exports/09-minor-4.png) · [SVG](exports/09-minor-4.svg) · [EMF](exports/09-minor-4.emf) · [JSON](ground_truth/09-minor-4.json) | — |
| 10-minor-9 | Nine minor ticks | [PNG](exports/10-minor-9.png) · [SVG](exports/10-minor-9.svg) · [EMF](exports/10-minor-9.emf) · [JSON](ground_truth/10-minor-9.json) | — |
| 11-box | Independent side lines/ticks/labels, box | [PNG](exports/11-box.png) · [SVG](exports/11-box.svg) · [EMF](exports/11-box.emf) · [JSON](ground_truth/11-box.json) | — |
| 12-L-only | Bottom/left only | [PNG](exports/12-L-only.png) · [SVG](exports/12-L-only.svg) · [EMF](exports/12-L-only.emf) · [JSON](ground_truth/12-L-only.json) | — |
| 13-zero | Axes at zero | [PNG](exports/13-zero.png) · [SVG](exports/13-zero.svg) · [EMF](exports/13-zero.emf) · [JSON](ground_truth/13-zero.json) | — |
| 14-reversed | Reversed axis | [PNG](exports/14-reversed.png) · [SVG](exports/14-reversed.svg) · [EMF](exports/14-reversed.emf) · [JSON](ground_truth/14-reversed.json) | — |
| 15-custom-ticks | Positions and labels from columns | [PNG](exports/15-custom-ticks.png) · [SVG](exports/15-custom-ticks.svg) · [EMF](exports/15-custom-ticks.emf) · [JSON](ground_truth/15-custom-ticks.json) | — |
| 16-grid | Distinct major/minor grid styles | [PNG](exports/16-grid.png) · [SVG](exports/16-grid.svg) · [EMF](exports/16-grid.emf) · [JSON](ground_truth/16-grid.json) | — |
| 17-group-list | 16 curves, incremental colors/symbols/lines | [PNG](exports/17-group-list.png) · [SVG](exports/17-group-list.svg) · [EMF](exports/17-group-list.emf) · [JSON](ground_truth/17-group-list.json) | [PNG](exports/17-group-list-160x120.png) · [SVG](exports/17-group-list-160x120.svg) · [EMF](exports/17-group-list-160x120.emf) · [JSON](ground_truth/17-group-list-160x120.json) |
| 18-group-T | User parameter T, colormap, colorbar | [PNG](exports/18-group-T.png) · [SVG](exports/18-group-T.svg) · [EMF](exports/18-group-T.emf) · [JSON](ground_truth/18-group-T.json) | [PNG](exports/18-group-T-160x120.png) · [SVG](exports/18-group-T-160x120.svg) · [EMF](exports/18-group-T-160x120.emf) · [JSON](ground_truth/18-group-T-160x120.json) |
| 19-template | Save .otpu; replay on second dataset | [PNG](exports/19-template.png) · [SVG](exports/19-template.svg) · [EMF](exports/19-template.emf) · [JSON](ground_truth/19-template.json) | [PNG](exports/19-template-160x120.png) · [SVG](exports/19-template-160x120.svg) · [EMF](exports/19-template-160x120.emf) · [JSON](ground_truth/19-template-160x120.json) |
| 20-grid-2x2 | Four layers, shared X | [PNG](exports/20-grid-2x2.png) · [SVG](exports/20-grid-2x2.svg) · [EMF](exports/20-grid-2x2.emf) · [JSON](ground_truth/20-grid-2x2.json) | [PNG](exports/20-grid-2x2-160x120.png) · [SVG](exports/20-grid-2x2-160x120.svg) · [EMF](exports/20-grid-2x2-160x120.emf) · [JSON](ground_truth/20-grid-2x2-160x120.json) |
| 21-stack | Stacked layers, shared X | [PNG](exports/21-stack.png) · [SVG](exports/21-stack.svg) · [EMF](exports/21-stack.emf) · [JSON](ground_truth/21-stack.json) | [PNG](exports/21-stack-160x120.png) · [SVG](exports/21-stack-160x120.svg) · [EMF](exports/21-stack-160x120.emf) · [JSON](ground_truth/21-stack-160x120.json) |
| 22-inset | Zoom inset and region | [PNG](exports/22-inset.png) · [SVG](exports/22-inset.svg) · [EMF](exports/22-inset.emf) · [JSON](ground_truth/22-inset.json) | [PNG](exports/22-inset-160x120.png) · [SVG](exports/22-inset-160x120.svg) · [EMF](exports/22-inset-160x120.emf) · [JSON](ground_truth/22-inset-160x120.json) |
| 23-break-one | One axis break | [PNG](exports/23-break-one.png) · [SVG](exports/23-break-one.svg) · [EMF](exports/23-break-one.emf) · [JSON](ground_truth/23-break-one.json) | [PNG](exports/23-break-one-160x120.png) · [SVG](exports/23-break-one-160x120.svg) · [EMF](exports/23-break-one-160x120.emf) · [JSON](ground_truth/23-break-one-160x120.json) |
| 24-break-two | Two axis breaks | [PNG](exports/24-break-two.png) · [SVG](exports/24-break-two.svg) · [EMF](exports/24-break-two.emf) · [JSON](ground_truth/24-break-two.json) | [PNG](exports/24-break-two-160x120.png) · [SVG](exports/24-break-two-160x120.svg) · [EMF](exports/24-break-two-160x120.emf) · [JSON](ground_truth/24-break-two-160x120.json) |
| 25-twin-Y | Twin Y, C and 1/C² | [PNG](exports/25-twin-Y.png) · [SVG](exports/25-twin-Y.svg) · [EMF](exports/25-twin-Y.emf) · [JSON](ground_truth/25-twin-Y.json) | [PNG](exports/25-twin-Y-160x120.png) · [SVG](exports/25-twin-Y-160x120.svg) · [EMF](exports/25-twin-Y-160x120.emf) · [JSON](ground_truth/25-twin-Y-160x120.json) |
| 26-rich-text | Sub/superscripts, Greek, units, dynamic legend from long name/units/user parameter | [PNG](exports/26-rich-text.png) · [SVG](exports/26-rich-text.svg) · [EMF](exports/26-rich-text.emf) · [JSON](ground_truth/26-rich-text.json) | [PNG](exports/26-rich-text-160x120.png) · [SVG](exports/26-rich-text-160x120.svg) · [EMF](exports/26-rich-text-160x120.emf) · [JSON](ground_truth/26-rich-text-160x120.json) |

## Ground truth contract and limitations

JSON contains actual post-layout LabTalk/originpro getters, separately for every page size: page dimensions, layer bounds, four axis sides, ranges/scales, tick masks/counts/lengths, label fonts, grid properties, linking formulas, breaks, plot/group properties and legend source. Missing numeric getters have null values and entries in `unavailable_getters`. Unavailable vectors/text use `{ "value": null, "reason": "..." }`.

Major/minor tick vectors and per-side rendered tick strings were not reconstructed from ranges or input columns. Origin's SVG exports use glyph paths, so the original PNG/SVG is the visual authority. Legend expansion uses Origin's own string substitution; rich-text control codes are retained. An encoded color-mapping value is not advertised as a resolved RGB measurement.

Origin stores `page.width` and `page.height` in integer printer dots. The requested 80×60 and 160×120 mm therefore have a sub-dot internal quantization error, recorded in JSON. Export width is explicitly 8 or 16 cm; SVG declares exact target dimensions. Native EMF Keep Size initially produced approximately 70×52 mm for an 80×60 mm page on this display. The generator measures that first header and re-exports through Origin with a calibrated page ratio, without editing EMF headers. Residual EMF physical-size error in this run is at most 0.21 mm; actual frame dimensions and the page ratio are in JSON. PNG raster dimensions are checked against mm × 600 / 25.4 with a maximum two-pixel tolerance for Origin rounding.

Relevant official references: [Axis object](https://docs.originlab.com/labtalk/ref/layer-axis-obj/), [page dimensions](https://docs.originlab.com/quick-help/specify-graph-size/), [export tree settings](https://docs.originlab.com/x-function/ref/details-of-treenodes-in-export-graph/), [template saving](https://docs.originlab.com/x-function/ref/template_saveas/), [legend substitutions](https://docs.originlab.com/labtalk/ref/legend-substitution-notation/).

Validation compiles the Python sources into this experiment's `build/` directory and runs all local `test_*.py` tests. Fixture-test temporary files are also restricted to `build/`. No product C++ files or bindings are changed: this is an offline reference experiment, not a new public product operation. See [REPORT.md](REPORT.md) for the final acceptance result and observed Origin rules.


Visual QA: all 36 PNG variants were inspected, including the final exports. Nine contact sheets can be recreated with `contact_sheet.py` and are stored in `build/`. Overbars above text persist in this Origin installation's exports, including a minimal independent probe; they are retained as an environment artifact, not a PlotLab formatting requirement.


