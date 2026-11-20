import copy
import math
import re
import xml.etree.ElementTree as ET

from PIL import Image
import numpy as np
import pytest
from sst_plot_export.exporter import build_figure, export_graph


@pytest.fixture
def snapshot():
    return dict(width_mm=80, height_mm=60, font_family="DejaVu Sans", font_pt=8,
                ranges=[1.25, 4.75, -10, 12],
                x_axis=dict(label="Voltage (V)", log=False, grid=True,
                            ticks=[dict(value=2, label="two", major=True), dict(value=4, label="four", major=True)]),
                y_axis=dict(label="Current (mA)", log=False, grid=False,
                            ticks=[dict(value=0, label="zero", major=True)]),
                legend=dict(visible=True, outside=False),
                series=[dict(label="Signal", x=[1, 2, 3, 4, 5], y=[2, 4, None, 8, 10], y_error=[], kind=2,
                             style=dict(color=[.1, .3, .7, 1], fill=[.1, .3, .7, 1], outline=[0, 0, 0, 1],
                                        marker=1, line_pt=1.5, marker_pt=3, cap_pt=4, legend=True))])


@pytest.mark.parametrize("format", ["pdf", "svg"])
def test_physical_size(snapshot, tmp_path, format):
    target = tmp_path / f"figure.{format}"
    export_graph(snapshot, target, format)
    expected = [80*72/25.4, 60*72/25.4]
    if format == "svg":
        root = ET.parse(target).getroot()
        size = [float(root.attrib[key].removesuffix("pt")) for key in ("width", "height")]
    else:
        box = re.search(rb"/MediaBox\s*\[([^]]+)\]", target.read_bytes())
        assert box is not None
        coords = [float(x) for x in box[1].split()]
        size = [coords[2] - coords[0], coords[3] - coords[1]]
    assert size == pytest.approx(expected, abs=1e-5)


def test_png_rounded_pixels(snapshot, tmp_path):
    target = tmp_path / "figure.png"
    export_graph(snapshot, target, "png", 300)
    with Image.open(target) as image:
        assert image.size == (945, 709)
        assert image.info["dpi"] == pytest.approx((300, 300), abs=.01)


def test_svg_is_vector_and_font_outlines(snapshot, tmp_path):
    target = tmp_path / "figure.svg"
    export_graph(snapshot, target, "svg")
    root = ET.parse(target).getroot()
    assert not root.findall(".//{http://www.w3.org/2000/svg}image")
    assert len(root.findall(".//{http://www.w3.org/2000/svg}path")) > 10
    assert "DejaVuSans" in target.read_text()


def test_ranges_ticks_gaps_and_snapshot_are_preserved(snapshot, tmp_path):
    before = copy.deepcopy(snapshot)
    figure = build_figure(snapshot)
    ax = figure.axes[0]
    assert ax.get_xlim() == tuple(snapshot["ranges"][:2])
    assert ax.get_ylim() == tuple(snapshot["ranges"][2:])
    assert list(ax.get_xticks()) == [2, 4]
    assert [t.get_text() for t in ax.get_xticklabels()] == ["two", "four"]
    assert np.isnan(ax.lines[0].get_ydata()[2])
    assert ax.lines[0].get_linewidth() == 1.5
    assert ax.lines[0].get_markersize() == 6
    for format in ("svg", "pdf", "png"):
        assert export_graph(snapshot, tmp_path/f"out.{format}", format) == tuple(snapshot["ranges"])
    assert snapshot == before


def test_log_keeps_gaps_and_limits(snapshot):
    snapshot["ranges"] = [.5, 8, 1, 20]
    snapshot["x_axis"]["log"] = snapshot["y_axis"]["log"] = True
    snapshot["series"][0]["y"] = [-2, 4, 0, 8, 10]
    figure = build_figure(snapshot)
    ax = figure.axes[0]
    assert ax.get_xscale() == ax.get_yscale() == "log"
    assert np.isnan(ax.lines[0].get_ydata()[0])
    assert np.isnan(ax.lines[0].get_ydata()[2])
    assert ax.get_xlim() == (.5, 8)


def test_error_bars(snapshot, tmp_path):
    snapshot["series"][0]["y_error"] = [.1, .2, None, .3, .5]
    target = tmp_path/"errors.svg"
    export_graph(snapshot, target, "svg")
    assert target.stat().st_size > 1000


@pytest.mark.parametrize("invalid", ["font", "width", "dpi", "ranges", "format", "error"])
def test_invalid_export_preserves_destination(snapshot, tmp_path, invalid):
    target = tmp_path/"existing.svg"
    target.write_bytes(b"previous export")
    dpi, format = 300, "svg"
    if invalid == "font": snapshot["font_family"] = "SST font that does not exist"
    if invalid == "width": snapshot["width_mm"] = -1
    if invalid == "dpi": dpi = 0
    if invalid == "ranges": snapshot["ranges"] = [2, 1, 0, 1]
    if invalid == "format": format = "jpeg"
    if invalid == "error": snapshot["series"][0]["y_error"] = [-1]*5
    with pytest.raises(ValueError):
        export_graph(snapshot, target, format, dpi)
    assert target.read_bytes() == b"previous export"
    assert sorted(p.name for p in tmp_path.iterdir()) == ["existing.svg"]


def test_failed_replace_keeps_old_file_and_cleans_temp(snapshot, tmp_path, monkeypatch):
    target=tmp_path/"figure.pdf"; target.write_bytes(b"old")
    def fail(*args): raise PermissionError("replacement denied")
    monkeypatch.setattr("sst_plot_export.exporter.os.replace",fail)
    with pytest.raises(PermissionError): export_graph(snapshot,target,"pdf")
    assert target.read_bytes()==b"old"
    assert list(tmp_path.iterdir())==[target]

@pytest.mark.parametrize("family,name", [("DejaVu Serif", "DejaVuSerif"), ("DejaVu Sans Mono", "DejaVuSansMono")])
@pytest.mark.parametrize("format", ["pdf", "svg"])
def test_selected_font_is_embedded_or_outlined(snapshot, tmp_path, family, name, format):
    from sst_plot_export.exporter import resolve_font
    snapshot.update(font_family=family, font_file=resolve_font(family)[0], font_pt=11)
    target = tmp_path / f"chosen.{format}"
    export_graph(snapshot, target, format)
    assert name.encode() in target.read_bytes()
    fig = build_figure(snapshot)
    assert fig.axes[0].xaxis.label.get_fontsize() == 11
    assert fig.axes[0].xaxis.label.get_fontproperties().get_file() == snapshot["font_file"]

@pytest.mark.parametrize("invalid", ["missing", "corrupt", "mismatch"])
def test_custom_font_error_preserves_output(snapshot, tmp_path, invalid):
    from sst_plot_export.exporter import resolve_font
    font = tmp_path / "custom.otf"
    if invalid == "corrupt": font.write_bytes(b"not a font")
    if invalid == "mismatch": font = __import__("pathlib").Path(resolve_font("DejaVu Serif")[0])
    snapshot["font_file"] = str(font)
    target = tmp_path / "previous.pdf"; target.write_bytes(b"previous")
    with pytest.raises((ValueError, RuntimeError, FileNotFoundError)):
        export_graph(snapshot, target, "pdf")
    assert target.read_bytes() == b"previous"

def test_custom_font_filename_is_resolved_without_family(tmp_path):
    from sst_plot_export.exporter import resolve_font
    import shutil
    source, _ = resolve_font("DejaVu Serif")
    custom = tmp_path / "wlasny font.ttf"; shutil.copyfile(source, custom)
    path, family = resolve_font("", str(custom))
    assert family == "DejaVu Serif"
    assert path == str(custom.resolve())

@pytest.fixture
def spatial(snapshot):
    snapshot.update(width_mm=130, height_mm=100, ranges=[.5, 3.5, 5, 35],
                    z_axis=dict(label="Capacitance", unit="nF", log=False, grid=True, ticks=[dict(value=2,label="two",major=True)]),
                    colormap="Viridis", color_scale=True, color_range=[1, 9],
                    camera=dict(rotation_xyzw=[0, 0, 0, 1], axis_limits=[.5, 3.5, 5, 35, 0, 10]))
    series=snapshot["series"][0]
    series.update(x=[1,2,3]*3, y=[10]*3+[20]*3+[30]*3, z=[1,2,3,4,None,6,7,8,9],
                  mask=[0,1,0,0,0,0,0,0,0], grid_columns=3, kind=7)
    return snapshot


@pytest.mark.parametrize("kind", [4,5,6,7])
@pytest.mark.parametrize("format", ["png", "svg", "pdf"])
def test_spatial_exports_preserve_camera_grid_holes_and_units(spatial, tmp_path, kind, format):
    spatial["series"][0]["kind"]=kind
    before=copy.deepcopy(spatial)
    fig=build_figure(spatial)
    ax=fig.axes[0]
    assert ax.get_xlim()==(.5,3.5) and ax.get_ylim()==(5,35)
    if kind!=7:
        assert ax.name=="3d" and ax.get_zlim()==(0,10)
        assert ax.get_zlabel()=="Capacitance (nF)"
        assert [t.get_text() for t in ax.get_zticklabels()]==["two"]
    else:
        grid=ax.collections[0].get_array()
        assert np.ma.getmaskarray(grid).sum()==2
        assert fig.axes[1].get_ylabel()=="Capacitance (nF)"
    export_graph(spatial,tmp_path/f"spatial.{format}",format)
    assert (tmp_path/f"spatial.{format}").stat().st_size>1000
    assert spatial==before
    fig.clear()


@pytest.mark.parametrize("invalid", ["nonuniform", "unordered", "z_length", "mask_length", "infinity", "camera", "colors"])
def test_invalid_spatial_data_does_not_replace_destination(spatial, tmp_path, invalid):
    series=spatial["series"][0]
    if invalid=="nonuniform": series["x"][2]=3.1
    if invalid=="unordered": series["x"][1]=0
    if invalid=="z_length": series["z"].pop()
    if invalid=="mask_length": series["mask"].pop()
    if invalid=="infinity": series["z"][0]=float("inf")
    if invalid=="colors": spatial["color_range"]=[1,1]
    if invalid=="camera":
        series["kind"]=4
        spatial["camera"]["rotation_xyzw"]=[0]*4
    target=tmp_path/"previous.svg";target.write_bytes(b"previous")
    with pytest.raises(ValueError): export_graph(spatial,target,"svg")
    assert target.read_bytes()==b"previous"


def test_rectilinear_heatmap_uses_log_frequency_edges(spatial):
    spatial["x_axis"]["log"] = True
    spatial["ranges"] = [10, 1e5, 180, 350]
    spatial["series"][0].update(x=[100,1000,10000]*3, y=[200]*3+[230]*3+[310]*3)
    fig = build_figure(spatial)
    mesh = fig.axes[0].collections[0]
    coordinates = mesh.get_coordinates()
    assert np.log10(coordinates[0, :, 0]).tolist() == pytest.approx([1.5,2.5,3.5,4.5])
    assert coordinates[:, 0, 1].tolist() == pytest.approx([185,215,270,350])
    assert np.ma.getmaskarray(mesh.get_array()).sum() == 2
    fig.clear()


def test_all_missing_grid_and_log_axes(spatial, tmp_path):
    spatial["series"][0]["z"]=[None]*9
    spatial["series"][0]["kind"]=4
    spatial["color_range"]=None
    spatial["x_axis"]["log"]=True
    export_graph(spatial,tmp_path/"empty.svg","svg")
    spatial["series"][0]["kind"]=7
    export_graph(spatial,tmp_path/"empty_heatmap.svg","svg")


@pytest.mark.parametrize("name", ["Deep", "Dark", "Pastel", "Paired", "Viridis", "Plasma", "Hot", "Cool", "Pink", "Jet", "Twilight", "RdBu", "BrBG", "PiYG", "Spectral", "Greys"])
def test_native_colormaps_keep_the_screen_endpoints(spatial, name):
    from sst_plot_export.colormaps import COLORS
    spatial["colormap"] = name
    fig = build_figure(spatial)
    cmap = fig.axes[0].collections[0].cmap
    expected = [[((v >> shift) & 255)/255 for shift in (0, 8, 16, 24)] for v in (COLORS[name][0], COLORS[name][-1])]
    assert np.allclose(cmap([0., 1.]), expected)
    fig.clear()


@pytest.mark.parametrize("invalid", ["no_camera", "z_log_limits", "mixed", "two_grids"])
def test_spatial_export_rejects_unrenderable_views(spatial, tmp_path, invalid):
    spatial["series"][0]["kind"] = 4
    if invalid == "no_camera": spatial["camera"] = None
    if invalid == "z_log_limits": spatial["z_axis"]["log"] = True
    if invalid in ("mixed", "two_grids"):
        extra = copy.deepcopy(spatial["series"][0])
        if invalid == "mixed": extra["kind"] = 7
        spatial["series"].append(extra)
    target = tmp_path/"previous.svg"; target.write_bytes(b"previous")
    with pytest.raises(ValueError): export_graph(spatial, target, "svg")
    assert target.read_bytes() == b"previous"


@pytest.mark.parametrize("kind", [5, 6])
def test_spatial_marker_style_is_exported(spatial, kind):
    series = spatial["series"][0]
    series["kind"] = kind
    series["style"].update(marker=2, fill=[1,0,0,1], outline=[0,0,0,1], marker_pt=3)
    fig = build_figure(spatial)
    if kind == 5:
        scatter = fig.axes[0].collections[0]
        assert np.allclose(scatter.get_facecolor()[0], [1,0,0,1])
        assert np.allclose(scatter.get_sizes(), [36])
    else:
        line = fig.axes[0].lines[0]
        assert line.get_marker() == "s" and line.get_markersize() == 6
        assert line.get_markerfacecolor() == [1,0,0,1]
    fig.clear()
