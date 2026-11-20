# ImPlot3D vendor integration

ImPlot3D v0.5, MIT, upstream license retained in `LICENSE`.
Host: ImGui 1.92.7 WIP, ImPlot 0.18 WIP. No compatibility API shim.

Local patch in `implot3d_items.cpp`, `RendererSurfaceFill::Render`:
skip a quad with any NaN/infinite corner before colour lookup/projection.
This makes a masked or NaN grid vertex an actual hole. Covered by the UI-Library
headless renderer tests (finite draw buffers and reduced geometry).
Marker fill/outline and line strips also reject nonfinite vertices before
projection; line joins stop at missing vertices rather than spanning the gap.

Related ImPlot patch in `Vendor/implot/implot_items.cpp`:
heatmap getters give nonfinite cells transparent colour before colour-table access.
Both row-major and column-major paths are covered; finite cells are unchanged.

`Core/PlotExport/python/sst_plot_export/colormaps.py` copies the MIT-licensed
ImPlot built-in colour keys for matching matplotlib exports (including discrete
palettes); update it if the vendored built-ins change.
