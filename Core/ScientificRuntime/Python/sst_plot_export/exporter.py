"""No pyplot/global GUI backend; caller supplies ranges and ticks, never auto-fit."""
from pathlib import Path
import math
import os
import tempfile
from threading import RLock

import matplotlib as mpl
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.figure import Figure
from matplotlib.font_manager import FontProperties, findfont
from matplotlib.ft2font import FT2Font
from matplotlib.ticker import FixedLocator, FixedFormatter
import numpy as np
from .colormaps import COLORS


_export_lock = RLock()  # rc_context touches matplotlib globals while native draws may release the GIL.

def resolve_font(family, filename=""):
    """Resolve once for both ImGui and export; never substitute a missing font."""
    bundled = {"DejaVu Sans": "DejaVuSans.ttf", "DejaVu Serif": "DejaVuSerif.ttf",
               "DejaVu Sans Mono": "DejaVuSansMono.ttf"}
    if filename:
        path = Path(filename).expanduser().resolve(strict=True)
        if path.suffix.lower() not in (".ttf", ".otf"):
            raise ValueError("Choose a .ttf or .otf font")
    elif family in bundled:
        root = Path(mpl.get_data_path())
        # Source tree and installed app use the identical bundled font files.
        directory = root / "fonts/ttf"
        path = directory / bundled[family]
    else:
        path = Path(findfont(FontProperties(family=family), fallback_to_default=False))
    face = FT2Font(str(path))  # Validate before passing untrusted font bytes to ImGui.
    if filename and family and face.family_name != family:
        raise ValueError(f"Font family mismatch: expected {family}, got {face.family_name}")
    return str(path), face.family_name


def axis_label(config):
    unit = config.get("unit", "")
    return config["label"] + (f" ({unit})" if unit else "")


def draw_spatial(fig, ax, snapshot, series, x, y, font, three):
    kind = series["kind"]
    if kind == 7 and snapshot["z_axis"]["log"]: raise ValueError("Heatmap logarithmic colour scale is unsupported")
    if (kind != 7) != three:
        raise ValueError("Separate heatmap and 3D plots")
    z = np.asarray(series["z"], dtype=float)
    mask = np.asarray(series.get("mask", []), dtype=bool)
    if z.shape != x.shape or (mask.size and mask.shape != x.shape) or np.any(np.isinf(z)):
        raise ValueError("Invalid Z/mask dimensions or infinite Z")
    valid = np.isfinite(x) & np.isfinite(y) & np.isfinite(z)
    if mask.size: valid &= ~mask
    for values, key in ((x, "x_axis"), (y, "y_axis"), (z, "z_axis")):
        if snapshot[key]["log"] and (key != "z_axis" or three): valid &= values > 0
    if kind in (5, 6):
        style = series["style"]
        xx, yy, zz = (np.where(valid, v, np.nan) for v in (x, y, z))
        label = series["label"] if style["legend"] else "_nolegend_"
        marker = ("", "o", "s", "D", "x", "+", "*")[style["marker"]]
        if kind == 5:
            ax.scatter(xx, yy, zz, marker=marker or "o", facecolors=style["fill"], edgecolors=style["outline"],
                       linewidths=style["line_pt"], s=(2*style["marker_pt"])**2, label=label, depthshade=False)
        else:
            ax.plot(xx, yy, zz, color=style["color"], linewidth=style["line_pt"], marker=marker,
                    markersize=2*style["marker_pt"], markerfacecolor=style["fill"], markeredgecolor=style["outline"], label=label)
        return
    cols = series["grid_columns"]
    if not isinstance(cols, int) or cols < 2 or len(x) % cols or len(x)//cols < 2:
        raise ValueError("Grid requires at least 2x2 row-major cells")
    rows = len(x)//cols
    xx, yy = x.reshape(rows, cols), y.reshape(rows, cols)
    dx, dy = xx[0, 1]-xx[0, 0], yy[1, 0]-yy[0, 0]
    if not (dx > 0 and dy > 0 and np.all(np.isfinite(x)) and np.all(np.isfinite(y)) and
            np.allclose(xx, xx[0, 0]+np.arange(cols)*dx, rtol=1e-12, atol=dx*1e-8) and
            np.allclose(yy, yy[0, 0]+np.arange(rows)[:, None]*dy, rtol=1e-12, atol=dy*1e-8)):
        raise ValueError("Non-uniform grid: explicitly regrid the source before plotting")
    zz = np.where(valid, z, np.nan).reshape(rows, cols)
    color_range = snapshot.get("color_range")
    if color_range is None:
        color_valid = np.isfinite(z)
        if mask.size: color_valid &= ~mask
        if snapshot["z_axis"]["log"]: color_valid &= z > 0
        values = z[color_valid]
        lo, hi = (float(values.min()), float(values.max())) if values.size else (0, 1)
        if lo == hi:
            padding = max(1, abs(lo))*.05
            lo, hi = lo-padding, hi+padding
    else:
        if len(color_range) != 2 or not all(math.isfinite(v) for v in color_range) or color_range[0] >= color_range[1]:
            raise ValueError("Invalid color range")
        lo, hi = color_range
    name = snapshot.get("colormap", "Viridis")
    if name not in COLORS: raise ValueError("Unknown colormap")
    keys = np.array([[(v >> shift) & 255 for shift in (0, 8, 16, 24)] for v in COLORS[name]])/255
    cmap = (mpl.colors.ListedColormap(keys, name) if name in ("Deep", "Dark", "Pastel", "Paired")
            else mpl.colors.LinearSegmentedColormap.from_list(name, keys, N=256))
    norm = mpl.colors.Normalize(lo, hi)
    if kind == 4:
        # Drop the whole quad when any corner is missing, matching ImPlot3D.
        cells = valid.reshape(rows, cols)
        good = cells[:-1, :-1] & cells[1:, :-1] & cells[:-1, 1:] & cells[1:, 1:]
        from mpl_toolkits.mplot3d.art3d import Poly3DCollection
        corners = np.stack((xx, yy, zz), axis=-1)
        quads = np.stack((corners[:-1, :-1], corners[:-1, 1:], corners[1:, 1:], corners[1:, :-1]), axis=2)[good]
        colors = cmap(norm(quads[:, :, 2])).mean(axis=1)
        ax.add_collection3d(Poly3DCollection(quads, facecolors=colors, edgecolors="none", zsort="average"))
        artist = mpl.cm.ScalarMappable(norm=norm, cmap=cmap)
    else:
        xe = xx[0, 0]+(np.arange(cols+1)-.5)*dx
        ye = yy[0, 0]+(np.arange(rows+1)-.5)*dy
        artist = ax.pcolormesh(xe, ye, np.ma.masked_invalid(zz), cmap=cmap, norm=norm, shading="flat")
    if snapshot.get("color_scale", True):
        bar = fig.colorbar(artist, ax=ax, pad=.14 if three else .04, fraction=.05)
        bar.set_label(axis_label(snapshot["z_axis"]), fontproperties=font)
        for tick in bar.ax.get_yticklabels(): tick.set_fontproperties(font)


def build_figure(snapshot, dpi=300):
    width, height = snapshot["width_mm"], snapshot["height_mm"]
    if not all(math.isfinite(v) and v > 0 for v in (width, height, dpi, snapshot["font_pt"])):
        raise ValueError("Size, font size and DPI must be finite and positive")
    limits = snapshot["ranges"]
    if len(limits) != 4 or not all(math.isfinite(v) for v in limits) or limits[0] >= limits[1] or limits[2] >= limits[3]:
        raise ValueError("Invalid snapshot ranges")
    font_path, family = resolve_font(snapshot["font_family"], snapshot.get("font_file", ""))
    font = FontProperties(family=family, fname=font_path, size=snapshot["font_pt"])
    fig = Figure(figsize=(width / 25.4, height / 25.4), dpi=dpi)
    FigureCanvasAgg(fig)
    three = any(e["kind"] in (4, 5, 6) for e in snapshot["series"])
    heatmap = any(e["kind"] == 7 for e in snapshot["series"])
    if (three and any(e["kind"] not in (4, 5, 6) for e in snapshot["series"]) or
            heatmap and any(e["kind"] != 7 for e in snapshot["series"])):
        raise ValueError("Separate 2D, heatmap and 3D series into different plots")
    if sum(e["kind"] in (4, 7) for e in snapshot["series"]) > 1:
        raise ValueError("Use one surface/heatmap grid per plot")
    ax = fig.add_axes((0.16, 0.20, 0.66 if any(e["kind"] in (4, 7) for e in snapshot["series"]) else 0.78, 0.72),
                      projection="3d" if three else None)
    if three:
        ax.set_proj_type("ortho")
        ax.set_box_aspect((1, 1, 1))
        camera = snapshot.get("camera")
        if not camera: raise ValueError("3D export requires a resolved camera view")
        if camera:
            q = np.asarray(camera["rotation_xyzw"], dtype=float)
            ranges = camera["axis_limits"]
            if q.shape != (4,) or not np.all(np.isfinite(q)) or not np.isclose(q @ q, 1, atol=1e-4):
                raise ValueError("Invalid camera quaternion")
            if len(ranges) != 6 or not all(math.isfinite(v) for v in ranges) or any(ranges[i] >= ranges[i+1] for i in (0, 2, 4)):
                raise ValueError("Invalid camera limits")
            if any(snapshot[key]["log"] and ranges[2*i] <= 0 for i, key in enumerate(("x_axis", "y_axis", "z_axis"))):
                raise ValueError("Log camera limits must be positive")
            x, y, z, w = q
            right = np.array([1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)])
            view = np.array([2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)])
            elev, azim = np.arcsin(np.clip(view[2], -1, 1)), np.arctan2(view[1], view[0])
            u = np.array([-np.sin(azim), np.cos(azim), 0])
            v = np.cross(view, u)
            roll = np.arctan2(-right @ v, right @ u)
            ax.view_init(*np.degrees([elev, azim, roll]))
            limits = ranges[:4]
            ax.set_zlim(ranges[4:])
        z_axis = snapshot["z_axis"]
        ax.set_zscale("log" if z_axis["log"] else "linear")
        ax.set_zlabel(axis_label(z_axis), fontproperties=font)
        ax.grid(any(snapshot[k]["grid"] for k in ("x_axis", "y_axis", "z_axis")))
    axes = [(ax.xaxis, snapshot["x_axis"]), (ax.yaxis, snapshot["y_axis"])]
    if three: axes.append((ax.zaxis, snapshot["z_axis"]))
    for axis, config in axes:
        scale = "log" if config["log"] else "linear"
        (ax.set_xscale if axis is ax.xaxis else ax.set_yscale if axis is ax.yaxis else ax.set_zscale)(scale)
        if axis is not getattr(ax, "zaxis", None) and config["log"] and limits[0 if axis is ax.xaxis else 2] <= 0:
            raise ValueError("Log ranges must be positive")
        for major in (True, False):
            ticks = [t for t in config["ticks"] if t["major"] == major]
            setter = axis.set_major_locator if major else axis.set_minor_locator
            formatter = axis.set_major_formatter if major else axis.set_minor_formatter
            setter(FixedLocator([t["value"] for t in ticks]))
            formatter(FixedFormatter([t["label"] for t in ticks]))
        if not three: axis.grid(config["grid"])
        axis.label.set_fontproperties(font)
        for tick in axis.get_ticklabels(minor=False) + axis.get_ticklabels(minor=True):
            tick.set_fontproperties(font)
    ax.set_xlabel(axis_label(snapshot["x_axis"]), fontproperties=font)
    ax.set_ylabel(axis_label(snapshot["y_axis"]), fontproperties=font)
    markers = ("", "o", "s", "D", "x", "+", "*")
    for series in snapshot["series"]:
        x, y = np.array(series["x"], dtype=float), np.array(series["y"], dtype=float)
        if x.ndim != 1 or y.shape != x.shape:
            raise ValueError("Series must have equal length one-dimensional X/Y data")
        kind = series["kind"]
        if kind in (4, 5, 6, 7):
            draw_spatial(fig, ax, snapshot, series, x, y, font, three)
            continue
        if three:
            raise ValueError("Separate 2D and 3D series")
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
    if snapshot.get("equal_aspect") and not three: ax.set_aspect("equal", adjustable="box")
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
    with _export_lock, mpl.rc_context({"svg.fonttype": "path", "pdf.fonttype": 42, "text.usetex": False,
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


def export_plots(exports, cancelled=lambda: False):
    """Render first, then commit; a failed batch restores earlier destinations."""
    from .files import commit_files
    destinations = [Path(item["destination"]) for item in exports]
    if not exports or len({p.resolve() for p in destinations}) != len(destinations):
        raise ValueError("Select distinct graph destinations")
    staged = []
    try:
        with _export_lock:
            for item, path in zip(exports, destinations):
                if cancelled():
                    raise RuntimeError("Export cancelled")
                fd, name = tempfile.mkstemp(prefix=".sst-plot-", dir=path.parent)
                os.close(fd)
                staged.append(Path(name))
                export_graph(item["snapshot"], name, item["format"], item["dpi"])
            commit_files(list(zip(staged, destinations)), cancelled)
    finally:
        for temporary in staged:
            temporary.unlink(missing_ok=True)
