# PL4 - font and export experiment

Production continues to use the native matplotlib SVG/PDF backends. This spike
compares those backends with Cairo surfaces using **the same semantic Figure
adapter**. It measures a Cairo replacement for the vector backend, not an
independent second layout engine. No Cairo dependency enters the product.

Run from `SST-Prototypes` (Python 3.13 environment containing matplotlib):

```powershell
bash D:/SJCM/01_SemiconductorStudio/tools/agents/build-lock.sh .venv/Scripts/python.exe Prototypes/PlotLab/experiments/export_parity.py --cairo-python-path C:/Users/Franek/AppData/Local/uv/cache/archive-v0/4ZbeAbEsw3XYK74kw6gts --cairo-dll-dir D:/Aplications/Inscape/install/bin
```

The optional paths select already installed pycairo 1.28.0 and Inkscape Cairo
1.18.4. On a machine with importable Cairo these arguments are unnecessary.
Windows private font registration lasts only for this process. Report records
the actual platform/CPU, runtime versions and font family for reproduction.
This is a Python/headless benchmark, so C++ Debug/Release does not affect it.

Three 80×60 mm fixtures cover a data gap, two series with yErr and logarithmic
ranges/ticks. Fonts are bundled DejaVu Sans 8 pt, Serif 10 pt and Sans Mono 8 pt.
Each file has one warm-up and three measured writes; time includes Figure
construction and file output. Native writes additionally include production
atomic replacement, so the timing comparison is end-to-end, not isolated Cairo
rasterizer speed. `results/report.json` contains per-file median ms, bytes,
PDF FontName entries and embedding/outlining checks. SVG/PDF artifacts remain
beside their input snapshots.

Parity asserts equal series count, fixed ranges/ticks and pt. Bounding boxes of
axes, both labels, all major tick labels and legend are measured with each vector
renderer at 72 DPI, converted to mm and compared componentwise (threshold 0.5 mm).
PDF must contain the selected font name and FontFile; SVG must contain outlines.
The report flags a font mismatch if Cairo silently substitutes a different PDF font.

This **does not certify live ImPlot/export layout parity**: ImPlot computes its
own margins in a resizable window. Annotations are not implemented in the PL3
snapshot and are not tested. ImPlot draw-list capture was omitted: converting
triangles plus atlas glyph textures to publication vectors requires a separate
serializer and font mapping, so it is not a cheap comparison path.

## Segoe inventory

Five files currently exist (the owner context mentioned three; no git command
was used to inspect tracking):

| Location under SST-Prototypes | Current use | DejaVu replacement |
|---|---|---|
| `PrototypeTemplate/install/app/assets/fonts/segoeui.ttf` | Discoverable shell asset; no explicit config reference | Possible; shell metrics change |
| `Prototypes/Smoke/install/app/assets/fonts/segoeui.ttf` | Explicit `ui_settings.yaml` shell font | Possible; update config and smoke-check layout |
| `Prototypes/JFM/install/app/assets/fonts/segoeui.ttf` | Explicit `ui_settings.yaml` shell font | Possible; update config and check JFM layout |
| `Prototypes/PlotLab/install/app/assets/fonts/segoeui.ttf` | Discoverable shell asset; graph no longer depends on it | Graph already uses bundled DejaVu; shell asset can be replaced later |
| `Prototypes/CapLab/install/app/assets/fonts/segoeui.ttf` | Discoverable shell asset; concurrent CapLab work not inspected | Possible; owner of CapLab should verify its layout |

No Segoe file was removed or changed. PL4 bundles three DejaVu faces and their
complete `LICENSE_DEJAVU`; shell migration is outside this task's allowed paths.

## Font portability

Graph font changes use the existing engine operation/Core undo flow. Project
format v3 stores family, pt and custom file path; v1/v2 default to Sans 8 pt.
Custom .ttf/.otf files are validated with FreeType before ImGui receives them.
They remain external files: reopening elsewhere requires the file at that path.
Missing files or family mismatch fail visibly and do not replace an old export.
SVG outlines do not require the recipient to install a font; PDF embeds/subsets.


## Results (2026-11-18)

Recommendation: keep native matplotlib in production. Cairo (matplotlib canvas)
was faster here, but embedded ArialMT instead of the selected DejaVu fonts, even
after private Windows font registration. This runtime needs additional font
integration before it can qualify. Neither backend rasterized the whole figure.

Ranges of median times across three fixtures, decimal kB; cells list SVG / PDF.
Python headless on Intel i7-13700KF, Windows 11, Python 3.13.2, matplotlib 3.11.2,
pycairo 1.28.0 and Cairo 1.18.4.

| Backend | Parity | Time [ms] | Size [kB] | Fonts | Verdict |
|---|---|---|---|---|---|
| native matplotlib | Reference; series/yErr/ranges/ticks 3/3 | 19.6-24.1 / 38.5-50.4 | 19.4-29.3 / 9.4-12.7 | SVG outlines; PDF selected DejaVu embedded | Keep |
| Cairo via matplotlib | Semantics 3/3; positions FAIL 3/3 | 13.3-17.1 / 14.4-18.1 | 35.9-54.8 / 16.2-19.8 | SVG outlines; PDF ArialMT, mismatch 3/3 | Reject in this runtime |
| ImPlot capture | Not measured | - | - | Texture atlas glyphs | No cheap vector path |

Maximum bounding-box deviations: linear-gap **0.804 mm**, error-two-series
**1.690 mm**, log **1.990 mm** (threshold 0.5 mm). Axes differ by 0 mm; differences
are in text and legend. Series and yErr segments have identical checksums.
This does not measure live UI/export parity: native is the reference, not a
backend certified to match ImPlot within 0.5 mm.

Open decision: should projects package custom font bytes? Recommend yes in a
separate publication step after deciding how to handle user font distribution.
Currently v3 stores a file path; missing files produce diagnostics.
