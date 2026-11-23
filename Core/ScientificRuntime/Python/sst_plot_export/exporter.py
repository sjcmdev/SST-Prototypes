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
from matplotlib.ticker import FixedLocator, FixedFormatter, FuncFormatter
from matplotlib.lines import Line2D
from matplotlib.patches import Rectangle
from matplotlib.transforms import Affine2D
import re
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


def _spatial_coordinate(values, config):
    # Project logarithmic geometry explicitly; mplot3d's log axes distort surfaces.
    return np.log10(values) if config["log"] else values


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
        xx, yy, zz = (_spatial_coordinate(v, snapshot[key]) for v, key in
                      zip((xx, yy, zz), ("x_axis", "y_axis", "z_axis")))
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
    if not (np.all(np.isfinite(x)) and np.all(np.isfinite(y)) and
            np.all(np.diff(xx[0]) > 0) and np.all(np.diff(yy[:, 0]) > 0) and
            np.allclose(xx, xx[0], rtol=1e-12, atol=0) and np.allclose(yy, yy[:, :1], rtol=1e-12, atol=0)):
        raise ValueError("Non-uniform rows or unordered coordinates: expected a rectilinear grid")
    zz = np.where(valid, z, np.nan).reshape(rows, cols)
    color_range = snapshot.get("color_range")
    if color_range is None:
        color_valid = np.isfinite(z)
        if mask.size: color_valid &= ~mask
        if snapshot["z_axis"]["log"]: color_valid &= z > 0
        values = z[color_valid]
        lo, hi = (float(values.min()), float(values.max())) if values.size else (0, 1)
        if lo == hi:
            padding = (abs(lo) if lo else 1)*.05
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
        for i, key in enumerate(("x_axis", "y_axis", "z_axis")):
            quads[:, :, i] = _spatial_coordinate(quads[:, :, i], snapshot[key])
        ax.add_collection3d(Poly3DCollection(quads, facecolors=colors, edgecolors="none", zsort="average"))
        artist = mpl.cm.ScalarMappable(norm=norm, cmap=cmap)
    else:
        def edges(centres, logarithmic):
            v = np.log(centres) if logarithmic else centres
            mid = v[:-1]+np.diff(v)*.5
            out = np.r_[2*v[0]-mid[0], mid, 2*v[-1]-mid[-1]]
            return np.exp(out) if logarithmic else out
        xe = edges(xx[0], snapshot["x_axis"]["log"])
        ye = edges(yy[:, 0], snapshot["y_axis"]["log"])
        artist = ax.pcolormesh(xe, ye, np.ma.masked_invalid(zz), cmap=cmap, norm=norm, shading="flat")
    if snapshot.get("color_scale", True):
        bar = fig.colorbar(artist, ax=ax, pad=.14 if three else .04, fraction=.05)
        z_axis = snapshot["z_axis"]
        magnitude = max(abs(lo), abs(hi))
        exponent = max(-24, min(24, 3 * math.floor(math.log10(magnitude) / 3))) if magnitude else 0
        scale = 10.0 ** exponent
        prefix = ("y", "z", "a", "f", "p", "n", "µ", "m", "", "k", "M", "G", "T", "P", "E", "Z", "Y")[exponent // 3 + 8]
        bar.formatter = FuncFormatter(lambda value, _: f"{value / scale:.4g}")
        bar.update_ticks()
        bar.set_label(axis_label(dict(z_axis, unit=prefix + z_axis.get("unit", ""))), fontproperties=font)
        for tick in bar.ax.get_yticklabels(): tick.set_fontproperties(font)


_LINE_STYLES = ("-", (0, (6, 3)), (0, (1, 3)), (0, (6, 1.5, 1, 1.5)))


def _scale_functions(config):
    scale = config.get("spec", {}).get("scale", 1 if config["log"] else 0)
    if scale == 0: return lambda x: x, lambda x: x
    if scale in (1, 2, 3):
        function = (np.log10, np.log, np.log2)[scale-1]
        def logarithm(x):
            with np.errstate(divide="ignore", invalid="ignore"):
                return function(x)
        return logarithm, (lambda x: np.power(10., x)) if scale == 1 else np.exp if scale == 2 else (lambda x: np.power(2., x))
    if scale == 4:
        def reciprocal(x):
            with np.errstate(divide="ignore", invalid="ignore"):
                # FuncScale needs the increasing equivalent to preserve the requested axis direction.
                return np.divide(-1., x)
        return reciprocal, reciprocal
    raise ValueError("This export scale is unsupported; use Linear, Log10, Ln, Log2 or Reciprocal")


def _rich_runs(text, math_text):
    if not math_text: return [(text, 1., 0.)]
    greek = dict(alpha="α", beta="β", gamma="γ", delta="δ", epsilon="ε", theta="θ", lambda_="λ",
                 mu="μ", nu="ν", pi="π", rho="ρ", sigma="σ", tau="τ", phi="φ", omega="ω", Delta="Δ", Sigma="Σ", Omega="Ω")
    for name, value in greek.items(): text = text.replace("\\" + name.rstrip("_"), value)
    text = text.replace("$", "")
    runs, start = [], 0
    for match in re.finditer(r"([_^])(?:\{([^}]*)\}|(.))", text):
        if match.start() > start: runs.append((text[start:match.start()], 1., 0.))
        runs.append((match[2] if match[2] is not None else match[3], .7, -.35 if match[1] == "^" else .3))
        start = match.end()
    if start < len(text): runs.append((text[start:], 1., 0.))
    return runs


def _text_width(fig, text, font):
    return fig.canvas.get_renderer().get_text_width_height_descent(text, font, False)[0] * 72 / fig.dpi


def _draw_axis_geometry(fig, ax, snapshot, font):
    geometry = snapshot.get("axis_geometry")
    if geometry is None: return
    width, height = snapshot["width_mm"] * 72 / 25.4, snapshot["height_mm"] * 72 / 25.4
    ax.set_axis_off()
    for stroke in geometry["strokes"]:
        a, b = stroke["from_pt"], stroke["to_pt"]
        alpha = (.10 if stroke["minor"] else .22) if stroke["grid"] and not stroke["reference"] else 1
        line = Line2D([a[0]/width, b[0]/width], [1-a[1]/height, 1-b[1]/height],
                      transform=fig.transFigure, linewidth=stroke["width_pt"], color=(0, 0, 0, alpha),
                      linestyle=_LINE_STYLES[stroke["style"]], solid_capstyle="butt", dash_capstyle="butt",
                      clip_on=False, zorder=.5 if stroke["grid"] else 5)
        line.set_gid("sst-axis-stroke")
        ax.add_artist(line)
    for caption in geometry["captions"]:
        props = font.copy()
        if caption["font_family"] and caption["font_family"] != font.get_name():
            path, family = resolve_font(caption["font_family"])
            props = FontProperties(family=family, fname=path)
        size = caption["font_pt"]
        runs = _rich_runs(caption["text"], caption["math"])
        widths = []
        for text, scale, _ in runs:
            props.set_size(size*scale); widths.append(_text_width(fig, text, props))
        anchor = caption["anchor_pt"]
        x = anchor[0] - sum(widths)*caption["alignment"][0]
        top = anchor[1] - size*caption["alignment"][1]
        rotation = caption["rotation_deg"]
        transform = Affine2D().rotate_deg_around(anchor[0], height-anchor[1], rotation).scale(1/width, 1/height) + fig.transFigure
        for (text, scale, shift), advance in zip(runs, widths):
            props.set_size(size*scale)
            artist = fig.text(x, height-top-size*shift, text, fontproperties=props.copy(),
                             ha="left", va="top", rotation=rotation, rotation_mode="anchor", transform=transform, zorder=6)
            artist.set_gid("sst-axis-caption")
            x += advance


def _draw_legend(fig, snapshot, font):
    legend, entries = snapshot["legend"], snapshot.get("legend_entries", [])
    if not legend["visible"] or not entries: return
    props = font.copy(); size = legend.get("font_pt") or snapshot["font_pt"]; props.set_size(size)
    gap, row = 6., size+6.
    columns = legend.get("columns", 1)
    if not isinstance(columns, int) or not 1 <= columns <= 16: raise ValueError("Invalid legend column count")
    cell = max(_text_width(fig, e["label"], props)+size*3+gap for e in entries)
    box_width = cell*min(columns, len(entries))+2*gap
    box_height = row*math.ceil(len(entries)/columns)+2*gap
    width, height = snapshot["width_mm"]*72/25.4, snapshot["height_mm"]*72/25.4
    frame = snapshot["data_frame"]
    left, right = frame[0]*width, (frame[0]+frame[2])*width
    top, bottom = (1-frame[1]-frame[3])*height, (1-frame[1])*height
    position = legend.get("position", 0)
    if position == 0 and legend["outside"]: position = 4
    x, y = right-box_width-gap, top+gap
    if position == 1: x = left+gap
    if position in (2, 3): y = bottom-box_height-gap
    if position == 3: x = left+gap
    if position == 4: x, y = right+gap, top
    if position == 5: x, y = left, top-box_height-40
    if position == 6: x, y = left, bottom+40
    layout = snapshot.get("legend_geometry")
    if layout:
        x,y,box_width,box_height = layout["box_pt"]
        cell,row,size = layout["cell_pt"],layout["row_pt"],layout["font_pt"]
        props.set_size(size)
    box = Rectangle((x/width, 1-(y+box_height)/height), box_width/width, box_height/height,
                    transform=fig.transFigure, facecolor="white", edgecolor="black" if legend.get("frame", True) else "none",
                    linewidth=.5, zorder=10)
    box.set_gid("sst-legend-frame"); fig.add_artist(box)
    for i, entry in enumerate(entries):
        xx, yy = x+gap+cell*(i % columns), y+gap+row*(i//columns)
        alpha = 1 if entry["visible"] else .35
        if entry.get("kind") == 8:
            line = Rectangle(((xx+size*.5)/width, 1-(yy+size)/height), size/width, size/height,
                             transform=fig.transFigure, facecolor=entry["color"], alpha=alpha, zorder=11)
        else:
            line = Line2D([xx/width, (xx+size*2)/width], [1-(yy+size/2)/height]*2,
                          transform=fig.transFigure, color=entry["color"], linewidth=entry["line_pt"],
                          solid_capstyle="butt", dash_capstyle="butt",
                          linestyle="none" if entry.get("kind", 0) == 1 else _LINE_STYLES[entry.get("line_style", 0)], alpha=alpha, zorder=11)
        line.set_gid("sst-legend-sample"); fig.add_artist(line)
        if entry["marker"]:
            marker = Line2D([(xx+size)/width], [1-(yy+size/2)/height], transform=fig.transFigure,
                            marker=("", "o", "s", "D", "x", "+", "*")[entry["marker"]],
                            markersize=2*entry["marker_pt"], markerfacecolor=entry["fill"], markeredgecolor=entry["outline"],
                            alpha=alpha, linestyle="none", zorder=12)
            fig.add_artist(marker)
        fig.text((xx+size*2.5)/width, 1-yy/height, entry["label"], fontproperties=props,
                 ha="left", va="top", alpha=1 if entry["visible"] else .4, zorder=11)
    # Physical coordinates for deterministic geometry tests; not used to transport runtime data.
    fig._sst_legend_box_pt = (x, y, box_width, box_height)


@mpl.rc_context({"lines.scale_dashes": False})
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
    ax = fig.add_axes(snapshot.get("data_frame", (0.16, 0.20, 0.66 if any(e["kind"] in (4, 7) for e in snapshot["series"]) else 0.78, 0.72)),
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
            ax.set_zlim(_spatial_coordinate(ranges[4:], snapshot["z_axis"]))
        z_axis = snapshot["z_axis"]
        ax.set_zscale("linear")
        ax.set_zlabel(axis_label(z_axis), fontproperties=font)
        ax.grid(any(snapshot[k]["grid"] for k in ("x_axis", "y_axis", "z_axis")))
    axes = [(ax.xaxis, snapshot["x_axis"]), (ax.yaxis, snapshot["y_axis"])]
    if three: axes.append((ax.zaxis, snapshot["z_axis"]))
    for axis, config in axes:
        setter = ax.set_xscale if axis is ax.xaxis else ax.set_yscale if axis is ax.yaxis else ax.set_zscale
        if not three and "spec" in config:
            lo, hi = limits[:2] if axis is ax.xaxis else limits[2:]
            code = config["spec"]["scale"]
            if code in (1,2,3) and lo <= 0: raise ValueError("Log ranges must be positive")
            if code == 4 and lo <= 0 <= hi: raise ValueError("Reciprocal range crosses zero")
            forward, inverse = _scale_functions(config)
            setter("function", functions=(forward, inverse))
        else: setter("log" if config["log"] and not three else "linear")
        if axis is not getattr(ax, "zaxis", None) and config["log"] and limits[0 if axis is ax.xaxis else 2] <= 0:
            raise ValueError("Log ranges must be positive")
        for major in (True, False):
            ticks = [t for t in config["ticks"] if t["major"] == major]
            setter = axis.set_major_locator if major else axis.set_minor_locator
            formatter = axis.set_major_formatter if major else axis.set_minor_formatter
            positions = [t["value"] for t in ticks]
            setter(FixedLocator(_spatial_coordinate(positions, config) if three else positions))
            formatter(FixedFormatter([t["label"] for t in ticks]))
        if not three: axis.grid(config["grid"])
        axis.label.set_fontproperties(font)
        if snapshot.get("axis_geometry") is None:
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
        for values, key in ((x, "x_axis"), (y, "y_axis")):
            scale = snapshot[key].get("spec", {}).get("scale", 1 if snapshot[key]["log"] else 0)
            if scale in (1,2,3): valid &= values > 0
            if scale == 4: valid &= values != 0
        x, y = np.where(valid, x, np.nan), np.where(valid, y, np.nan)
        if series.get("masked_rows"):
            indices = np.asarray(series["masked_rows"])
            if indices.ndim != 1 or indices.dtype.kind not in "iu" or np.any(indices < 0) or np.any(indices >= x.size):
                raise ValueError("Invalid worksheet mask row")
            ax.scatter(x[indices], y[indices], color=(1, .3, .3, 1), marker="x", s=144,
                       linewidths=1, label="_nolegend_", zorder=4)
        style, kind = series["style"], series["kind"]
        if kind == 8:
            gaps = np.abs(np.diff(x))
            gaps = gaps[np.isfinite(gaps) & (gaps > 0)]
            bar_width = .8 * float(np.min(gaps)) if gaps.size else .8
            error = np.asarray(series["y_error"], dtype=float) if series["y_error"] else None
            if error is not None and (error.shape != y.shape or np.any(error < 0) or np.any(np.isinf(error))):
                raise ValueError("Invalid yErr")
            ax.bar(x, y, width=bar_width, color=style["color"], edgecolor=style["outline"],
                   linewidth=style["line_pt"], label=series["label"] if style["legend"] else "_nolegend_",
                   yerr=error, ecolor=style["color"], capsize=style["cap_pt"]/2)
            continue
        marker = markers[style["marker"]]
        if kind in (1, 2) and not marker and style.get("marker_auto", True): marker = "o"
        args = dict(color=style["color"], linewidth=style["line_pt"],
                    solid_capstyle="butt", dash_capstyle="butt",
                    marker=marker if kind != 3 else "", markersize=2*style["marker_pt"],
                    markerfacecolor=style["fill"], markeredgecolor=style["outline"],
                    linestyle=_LINE_STYLES[style.get("line_style", 0)] if kind in (0, 2) else "none",
                    label=series["label"] if style["legend"] else "_nolegend_", clip_on=True)
        if series["y_error"]:
            error = np.asarray(series["y_error"], dtype=float)
            if error.shape != y.shape or np.any(error < 0) or np.any(np.isinf(error)):
                raise ValueError("Invalid yErr")
            y_scale = snapshot["y_axis"].get("spec", {}).get("scale", 1 if snapshot["y_axis"]["log"] else 0)
            if y_scale in (1,2,3): error = np.where(y-error > 0, error, np.nan)
            if y_scale == 4: error = np.where((y-error)*(y+error) > 0, error, np.nan)
            ax.errorbar(x, y, yerr=error, capsize=style["cap_pt"]/2, **args)
        elif kind == 3:
            raise ValueError("ErrorBars requires yErr")
        else:
            ax.plot(x, y, **args)
    # Set AFTER ticks/series; disable autoscale, including on later draw/save.
    ax.set_xlim(_spatial_coordinate(limits[:2], snapshot["x_axis"]) if three else limits[:2])
    ax.set_ylim(_spatial_coordinate(limits[2:], snapshot["y_axis"]) if three else limits[2:])
    ax.set_autoscale_on(False)
    if not three:
        if snapshot["x_axis"].get("spec", {}).get("reverse"): ax.invert_xaxis()
        if snapshot["y_axis"].get("spec", {}).get("reverse"): ax.invert_yaxis()
        _draw_axis_geometry(fig, ax, snapshot, font)
    if snapshot.get("equal_aspect") and not three: ax.set_aspect("equal", adjustable="box")
    legend = snapshot["legend"]
    if snapshot.get("legend_entries"):
        _draw_legend(fig, snapshot, font)
    elif legend["visible"] and ax.get_legend_handles_labels()[0]:
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
            limits = tuple(snapshot["ranges"]) if fig.axes[0].name == "3d" else (*fig.axes[0].get_xlim(), *fig.axes[0].get_ylim())
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
