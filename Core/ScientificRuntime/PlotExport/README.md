# Shared plot export

Core's `ExportPlotsJson` runs inside the application's existing command/job path.
A request can retain a typed immutable snapshot through `pythonSnapshot`, a binding
callback invoked with the GIL held. JFM uses this path; `SnapshotJson` and Python
conversion run in `JFMPythonBindings.cpp` at that boundary. The existing text field
remains a compatibility adapter for file/Python callers.

The exporter acquires the GIL without Core's outer Python scope mutex, so a Python
caller can release the GIL and wait.

The `sst_plot_export` Python package renders PNG/SVG/PDF with Matplotlib's Agg
canvas. It preserves resolved ranges, ticks, visible series, styles, physical
page dimensions and font settings. Equal-aspect graphs keep equal physical
scales without changing the requested data limits.

All batch images are rendered to temporary files before destinations change.
Replacement failure restores previously replaced files. Cancellation is checked
before rendering and before commit; a completed commit is reported as success.
If restoring an original also fails, the error identifies its retained backup.

JFM and CapLab deploy this package beside their embedded Python packages.
PlotLab keeps its app copy until its paused work resumes.
