import copy
import math
import re
import xml.etree.ElementTree as ET

from PIL import Image
import numpy as np
import pytest
from sst_plotlab.exporter import build_figure, export_graph


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


def test_column_plot_uses_bars_and_validates_errors(snapshot):
    snapshot["series"][0]["kind"] = 8
    figure = build_figure(snapshot)
    bars = figure.axes[0].patches
    assert len(bars) == 5
    assert bars[0].get_width() == pytest.approx(.8)
    assert bars[0].get_height() == 2
    snapshot["series"][0]["y_error"] = [-1] * 5
    with pytest.raises(ValueError, match="Invalid yErr"):
        build_figure(snapshot)


def test_masked_points_are_red_crosses_and_invalid_indices_fail(snapshot):
    snapshot["series"][0]["masked_rows"] = [1, 3]
    figure = build_figure(snapshot)
    crosses = figure.axes[0].collections[0]
    assert crosses.get_offsets().tolist() == [[2., 4.], [4., 8.]]
    assert crosses.get_facecolor()[0].tolist() == pytest.approx([1, .3, .3, 1])
    for invalid in ([-1], [100], [1.5], [True]):
        snapshot["series"][0]["masked_rows"] = invalid
        with pytest.raises(ValueError, match="Invalid worksheet mask row"):
            build_figure(snapshot)


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
    from sst_plotlab.exporter import resolve_font
    snapshot.update(font_family=family, font_file=resolve_font(family)[0], font_pt=11)
    target = tmp_path / f"chosen.{format}"
    export_graph(snapshot, target, format)
    assert name.encode() in target.read_bytes()
    fig = build_figure(snapshot)
    assert fig.axes[0].xaxis.label.get_fontsize() == 11
    assert fig.axes[0].xaxis.label.get_fontproperties().get_file() == snapshot["font_file"]

@pytest.mark.parametrize("invalid", ["missing", "corrupt", "mismatch"])
def test_custom_font_error_preserves_output(snapshot, tmp_path, invalid):
    from sst_plotlab.exporter import resolve_font
    font = tmp_path / "custom.otf"
    if invalid == "corrupt": font.write_bytes(b"not a font")
    if invalid == "mismatch": font = __import__("pathlib").Path(resolve_font("DejaVu Serif")[0])
    snapshot["font_file"] = str(font)
    target = tmp_path / "previous.pdf"; target.write_bytes(b"previous")
    with pytest.raises((ValueError, RuntimeError, FileNotFoundError)):
        export_graph(snapshot, target, "pdf")
    assert target.read_bytes() == b"previous"

def test_custom_font_filename_is_resolved_without_family(tmp_path):
    from sst_plotlab.exporter import resolve_font
    import shutil
    source, _ = resolve_font("DejaVu Serif")
    custom = tmp_path / "wlasny font.ttf"; shutil.copyfile(source, custom)
    path, family = resolve_font("", str(custom))
    assert family == "DejaVu Serif"
    assert path == str(custom.resolve())
