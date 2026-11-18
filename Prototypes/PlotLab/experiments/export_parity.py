"""M0 experiment: the same semantic adapter, native SVG/PDF vs Cairo surfaces.

Run from SST-Prototypes root under build-lock.sh. Cairo is experiment-only.
Optional --cairo-python-path and --cairo-dll-dir reuse an existing local runtime.
No production backend selection or C++ dependency is introduced.
"""
import argparse
import copy
import ctypes
import io
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from sst_plotlab.exporter import build_figure, export_graph, resolve_font
import matplotlib as mpl
import numpy as np
from matplotlib.backends.backend_svg import RendererSVG


def fixtures():
    style = dict(color=[.1, .3, .7, 1], fill=[.1, .3, .7, 1], outline=[0, 0, 0, 1],
                 marker=1, line_pt=1, marker_pt=2, cap_pt=4, legend=True)
    base = dict(width_mm=80, height_mm=60, font_family="DejaVu Sans", font_pt=8,
                ranges=[0, 5, -1, 11], legend=dict(visible=True, outside=False),
                x_axis=dict(label="Voltage (V)", log=False, grid=True,
                            ticks=[dict(value=x, label=str(x), major=True) for x in [0, 2, 4]]),
                y_axis=dict(label="Current (mA)", log=False, grid=True,
                            ticks=[dict(value=x, label=str(x), major=True) for x in [0, 5, 10]]),
                series=[dict(label="Signal", x=[0, 1, 2, 3, 4, 5], y=[0, 2, None, 6, 8, 10],
                             y_error=[], kind=2, style=style)])
    yield "linear-gap", base
    errors = copy.deepcopy(base)
    errors["series"][0]["y_error"] = [.1, .2, None, .4, .5, .6]
    errors["font_family"] = "DejaVu Serif"; errors["font_pt"] = 10
    second = copy.deepcopy(errors["series"][0]); second.update(label="Reference", y=[1, 3, 5, 7, 9, 11], kind=0)
    second["style"]["color"] = [.8, .2, .1, 1]
    errors["series"].append(second)
    yield "error-two-series", errors
    log = copy.deepcopy(base)
    log["font_family"] = "DejaVu Sans Mono"
    log["ranges"] = [.1, 100, .1, 100]
    for key in ("x_axis", "y_axis"):
        log[key]["log"] = True
        log[key]["ticks"] = [dict(value=x, label=str(x), major=True) for x in [.1, 1, 10, 100]]
    log["series"][0].update(x=[.1, 1, 10, 100], y=[.2, 2, 20, 80])
    yield "log", log


def positions(fig, renderer):
    """Final vector-renderer boxes in mm, measured from the lower left."""
    ax = fig.axes[0]
    artists = {"axes": ax, "x_label": ax.xaxis.label, "y_label": ax.yaxis.label}
    if ax.get_legend(): artists["legend"] = ax.get_legend()
    for axis, prefix in [(ax.xaxis, "x_tick"), (ax.yaxis, "y_tick")]:
        for i, text in enumerate(axis.get_ticklabels()): artists[f"{prefix}_{i}"] = text
    return {key: (np.array(artist.get_window_extent(renderer).extents)*25.4/fig.dpi).tolist()
            for key, artist in artists.items()}


def measure(snapshot, backend, cairo):
    fig = build_figure(snapshot, dpi=72)
    w, h = fig.get_size_inches()*72
    if backend == "cairo":
        from matplotlib.backends.backend_cairo import RendererCairo
        renderer = RendererCairo(72)
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, round(w), round(h))
        renderer.set_context(cairo.Context(surface))
    else:
        renderer = RendererSVG(w, h, io.StringIO())
    fig.draw(renderer)
    ax = fig.axes[0]
    semantic = dict(ranges=[*ax.get_xlim(), *ax.get_ylim()], x_ticks=ax.get_xticks().tolist(),
                    y_ticks=ax.get_yticks().tolist(), series=len(snapshot["series"]), artist_lines=len(ax.lines),
                    font_pt=ax.xaxis.label.get_fontsize(),
                    series_sha256=[hashlib.sha256(np.asarray(line.get_xydata(), dtype="<f8").tobytes()).hexdigest()
                                   for line in ax.lines],
                    error_sha256=[hashlib.sha256(b"".join(np.asarray(segment, dtype="<f8").tobytes()
                                                         for segment in collection.get_segments())).hexdigest()
                                  for collection in ax.collections if hasattr(collection, "get_segments")])
    boxes = positions(fig, renderer)
    fig.clear()
    return semantic, boxes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cairo-python-path")
    parser.add_argument("--cairo-dll-dir")
    parser.add_argument("--output", type=Path, default=ROOT/"experiments/results")
    args = parser.parse_args()
    if args.cairo_python_path: sys.path.insert(0, args.cairo_python_path)
    dll_handle = os.add_dll_directory(args.cairo_dll_dir) if args.cairo_dll_dir else None
    import cairo
    from matplotlib.backends.backend_cairo import FigureCanvasCairo
    args.output.mkdir(parents=True, exist_ok=True)
    cpu = platform.processor()
    if sys.platform == "win32":
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            cpu = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    report = dict(environment=dict(platform=platform.platform(), processor=cpu,
                                  python=sys.version, matplotlib=mpl.__version__, pycairo=cairo.version,
                                  cairo=cairo.cairo_version_string(), build="Python headless; C++ build not involved",
                                  cairo_python_path=args.cairo_python_path, cairo_dll_dir=args.cairo_dll_dir), fixtures=[])
    registered = []
    try:
        # Windows Cairo resolves family names through GDI, unlike native MPL's file selection.
        for name, snapshot in fixtures():
            path, family = resolve_font(snapshot["font_family"])
            if sys.platform == "win32":
                if not ctypes.windll.gdi32.AddFontResourceExW(path, 0x10, 0):
                    raise RuntimeError(f"Cairo private font registration failed: {path}")
                registered.append(path)
            snapshot["font_file"] = path
            (args.output/f"{name}.json").write_text(json.dumps(snapshot, indent=2))
            native_semantics, native_positions = measure(snapshot, "native", cairo)
            cairo_semantics, cairo_positions = measure(snapshot, "cairo", cairo)
            assert native_semantics == cairo_semantics
            errors = {key: float(np.max(np.abs(np.array(box)-cairo_positions[key])))
                      for key, box in native_positions.items()}
            result = dict(name=name, font=family, semantics=native_semantics,
                          position_error_mm=errors, max_position_error_mm=max(errors.values()),
                          within_0_5_mm=max(errors.values()) <= .5, files=[])
            for backend in ["native", "cairo"]:
                for fmt in ["svg", "pdf"]:
                    target = args.output/f"{name}-{backend}.{fmt}"
                    timings = []
                    for iteration in range(4):
                        start = time.perf_counter()
                        if backend == "native": export_graph(snapshot, target, fmt)
                        else:
                            fig = build_figure(snapshot, dpi=72)
                            canvas = FigureCanvasCairo(fig)
                            getattr(canvas, f"print_{fmt}")(str(target))
                            fig.clear()
                        if iteration: timings.append((time.perf_counter()-start)*1000)
                    data = target.read_bytes()
                    fonts = sorted(set(x.decode("ascii", "replace") for x in re.findall(rb"/FontName\s*/([^\s/]+)", data)))
                    # Native SVG identifies glyphs by PostScript name; Cairo SVG uses generic glyph IDs.
                    embedded = b"/FontFile" in data if fmt == "pdf" else b"<symbol" in data or b"DejaVu" in data or b'id="glyph' in data
                    font_matches = family.replace(" ", "").encode() in data if fmt == "pdf" else None
                    raster_images = len(re.findall(rb"<image\b" if fmt == "svg" else rb"/Subtype\s*/Image\b", data))
                    result["files"].append(dict(backend=backend, format=fmt, median_ms=statistics.median(timings),
                                                bytes=len(data), font_names=fonts, embedded_or_outlined=embedded,
                                                font_matches=font_matches, raster_images=raster_images))
            report["fixtures"].append(result)
        report["draw_list"] = "Skipped: texture atlas glyphs and triangulated geometry require a new serializer; no cheap publication capture."
        report["limits"] = "Positions compare native matplotlib and Cairo vector text metrics, not live ImPlot. No claim of UI/export 0.5 mm parity."
        (args.output/"report.json").write_text(json.dumps(report, indent=2))
        print(json.dumps(report, indent=2))
    finally:
        for path in registered: ctypes.windll.gdi32.RemoveFontResourceExW(path, 0x10, 0)


if __name__ == "__main__": main()
