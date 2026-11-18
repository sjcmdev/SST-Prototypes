"""No pyplot/global GUI backend; caller supplies ranges and ticks, never auto-fit."""
from pathlib import Path
import math
import os
import tempfile

import matplotlib as mpl
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.figure import Figure
from matplotlib.font_manager import FontProperties, findfont
from matplotlib.ticker import FixedLocator, FixedFormatter
import numpy as np


def build_figure(snapshot, dpi=300):
    width, height = snapshot["width_mm"], snapshot["height_mm"]
    if not all(math.isfinite(v) and v > 0 for v in (width, height, dpi, snapshot["font_pt"])):
        raise ValueError("Size, font size and DPI must be finite and positive")
    limits = snapshot["ranges"]
    if len(limits) != 4 or not all(math.isfinite(v) for v in limits) or limits[0] >= limits[1] or limits[2] >= limits[3]:
        raise ValueError("Invalid snapshot ranges")
    font = FontProperties(family=snapshot["font_family"], size=snapshot["font_pt"])
    # A missing publication font is an error, not a silent substitution.
    findfont(font, fallback_to_default=False)
    fig = Figure(figsize=(width / 25.4, height / 25.4), dpi=dpi)
    FigureCanvasAgg(fig)
    ax = fig.add_axes((0.22, 0.22, 0.72, 0.72))
    for axis, config in ((ax.xaxis, snapshot["x_axis"]), (ax.yaxis, snapshot["y_axis"])):
        scale = "log" if config["log"] else "linear"
        (ax.set_xscale if axis is ax.xaxis else ax.set_yscale)(scale)
        if config["log"] and limits[0 if axis is ax.xaxis else 2] <= 0:
            raise ValueError("Log ranges must be positive")
        for major in (True, False):
            ticks = [t for t in config["ticks"] if t["major"] == major]
            setter = axis.set_major_locator if major else axis.set_minor_locator
            formatter = axis.set_major_formatter if major else axis.set_minor_formatter
            setter(FixedLocator([t["value"] for t in ticks]))
            formatter(FixedFormatter([t["label"] for t in ticks]))
        axis.grid(config["grid"])
        axis.label.set_fontproperties(font)
        for tick in axis.get_ticklabels(minor=False) + axis.get_ticklabels(minor=True):
            tick.set_fontproperties(font)
    ax.set_xlabel(snapshot["x_axis"]["label"], fontproperties=font)
    ax.set_ylabel(snapshot["y_axis"]["label"], fontproperties=font)
    markers = ("", "o", "s", "D", "x", "+", "*")
    for series in snapshot["series"]:
        x, y = np.array(series["x"], dtype=float), np.array(series["y"], dtype=float)
        if x.ndim != 1 or y.shape != x.shape:
            raise ValueError("Series must have equal length one-dimensional X/Y data")
        valid = np.isfinite(x) & np.isfinite(y)
        if snapshot["x_axis"]["log"]: valid &= x > 0
        if snapshot["y_axis"]["log"]: valid &= y > 0
        x, y = np.where(valid, x, np.nan), np.where(valid, y, np.nan)
        style, kind = series["style"], series["kind"]
        marker = markers[style["marker"]]
        if kind in (1, 2) and not marker: marker = "o"
        args = dict(color=style["color"], linewidth=style["line_pt"],
                    marker=marker if kind != 3 else "", markersize=2*style["marker_pt"],
                    markerfacecolor=style["fill"], markeredgecolor=style["outline"],
                    linestyle="-" if kind in (0, 2) else "none",
                    label=series["label"] if style["legend"] else "_nolegend_", clip_on=True)
        if series["y_error"]:
            error = np.asarray(series["y_error"], dtype=float)
            if error.shape != y.shape or np.any(error < 0) or np.any(np.isinf(error)):
                raise ValueError("Invalid yErr")
            ax.errorbar(x, y, yerr=error, capsize=style["cap_pt"]/2, **args)
        elif kind == 3:
            raise ValueError("ErrorBars requires yErr")
        else:
            ax.plot(x, y, **args)
    # Set AFTER ticks/series; disable autoscale, including on later draw/save.
    ax.set_xlim(limits[:2]); ax.set_ylim(limits[2:]); ax.set_autoscale_on(False)
    legend = snapshot["legend"]
    if legend["visible"] and ax.get_legend_handles_labels()[0]:
        ax.legend(prop=font, loc="upper right", **({"bbox_to_anchor": (1, 1.28)} if legend["outside"] else {}))
    return fig


def export_graph(snapshot, destination, format, dpi=300):
    """Return the rendered limits, useful to verify the embedded C++ boundary."""
    format = format.lower()
    if format not in ("svg", "pdf", "png"):
        raise ValueError("Supported export formats: SVG, PDF, PNG")
    destination = Path(destination)
    # Context isolates publication settings from any earlier embedded script.
    with mpl.rc_context({"svg.fonttype": "path", "pdf.fonttype": 42, "text.usetex": False,
                         "savefig.bbox": None, "path.simplify": False}):
        fig = build_figure(snapshot, dpi)
        if format == "png":
            # Agg truncates fractional pixels. Round the requested physical size.
            pixels = [math.floor(v*dpi/25.4 + 0.5) for v in (snapshot["width_mm"], snapshot["height_mm"])]
            fig.set_size_inches([(v + 1e-7)/dpi for v in pixels])
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(prefix=f".{destination.name}.", suffix=f".{format}",
                                             dir=destination.parent, delete=False) as stream:
                temporary = Path(stream.name)
                fig.savefig(stream, format=format, dpi=dpi, bbox_inches=None)
                stream.flush(); os.fsync(stream.fileno())
            os.replace(temporary, destination)
            limits = (*fig.axes[0].get_xlim(), *fig.axes[0].get_ylim())
        finally:
            if temporary is not None: temporary.unlink(missing_ok=True)
            fig.clear()
    return limits
