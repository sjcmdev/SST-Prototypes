"""AS temperature maps use the GUI command/job path and preserve measured inputs."""
import copy
import json
import math
import pytest
from caplab import execute, import_preview, project_from_preview
from test_compare_browser import children
from test_m8_workflow import run


def series_project(grids=None, variant="C_G"):
    grids = grids or [[100, 1000, 10000]] * 3
    profile = {"C_G": "converted_as_c_g", "Cp_Rp": "lab_as_cp_rp", "ReZ_ImZ": "lab_is_z"}[variant]
    measurements = []
    for temperature, frequencies in zip([200, 230, 310], grids):
        lines = []
        for f in frequencies:
            c = (temperature / 100 + math.log10(f)) * 1e-9
            g = (temperature / 200 + 2 * math.log10(f)) * 1e-9 * 2 * math.pi * f
            a, b = c, g
            if variant == "Cp_Rp":
                b = 1 / g
            elif variant == "ReZ_ImZ":
                z = 1 / complex(g, 2 * math.pi * f * c)
                a, b = z.real, z.imag
            lines.append(f"{f} {a} {b}")
        options = dict(profile=profile, temperature_K=temperature, bias_V=0)
        if variant == "ReZ_ImZ":
            options["imaginary_sign"] = "ImZ"
        preview = import_preview("\n".join(lines), f"AS_{temperature}K.dat", options=options)
        measurements.append(preview["samples"][0]["measurements"][0])
    preview["samples"][0]["measurements"] = measurements
    return project_from_preview(preview)


def build(p, quantity="C", **request):
    return run(p, "plot", plot_type=f"AS {quantity}(f,T)",
               measurement_ids=[m["id"] for m in p["measurements"]], **request)


def values(p, graph):
    layer = children(p, graph)[0]
    plots = children(p, layer["id"])
    objects = {o["id"]: o for o in p["engine"]["objects"]}
    return layer, [(o, [objects[c]["values"] for c in o["columns"][1:4]]) for o in plots]


@pytest.mark.parametrize("variant", ["C_G", "Cp_Rp", "ReZ_ImZ"])
@pytest.mark.parametrize("quantity", ["C", "G/omega"])
@pytest.mark.parametrize("view,kind", [("lines", 6), ("surface", 4), ("heatmap", 7)])
def test_known_map_all_views_and_conversions(variant, quantity, view, kind):
    p = series_project(variant=variant)
    before = copy.deepcopy(p)
    result = build(p, quantity, view_kind=view)
    q = result["project"]
    layer, curves = values(q, result["graph_id"])
    assert layer["spec"]["x"]["scale"] == 1
    assert layer["spec"]["x"]["unit"] == "Hz" and layer["spec"]["y"]["unit"] == "K"
    assert layer["spec"]["z"]["unit"] == "F"
    assert len(curves) == (3 if view == "lines" else 1)
    for curve, (f, t, z) in curves:
        assert curve["kind"] == kind
        expected = [(tt / 100 + math.log10(ff)) * 1e-9 if quantity == "C" else
                    (tt / 200 + 2 * math.log10(ff)) * 1e-9 for ff, tt in zip(f, t)]
        assert z == pytest.approx(expected, rel=1e-12, abs=0)
    assert p == before
    source_ids = {o["id"] for o in before["engine"]["objects"]}
    assert [o for o in q["engine"]["objects"] if o["id"] in source_ids] == before["engine"]["objects"]
    json.dumps(q, allow_nan=False)


@pytest.mark.parametrize("view", ["surface", "heatmap", "lines"])
def test_unequal_grids_remain_measured_3d_lines(view):
    p = series_project([[100, 1000, 10000], [200, 2000], [100, 300, 10000]])
    result = build(p, view_kind=view)
    record = result["project"]["comparisons"][-1]
    assert record["grid_policy"] == "measured" and record["view_kind"] == "lines"
    if view != "lines":
        assert any("Unequal" in d for d in record["diagnostics"])
    _, curves = values(result["project"], result["graph_id"])
    assert [v[0] for _, v in curves] == [[100, 1000, 10000], [200, 2000], [100, 300, 10000]]


def test_explicit_log_interpolation_no_extrapolation_and_no_missing_bridges():
    p = series_project([[100, 10000], [1000, 10000], [100, 1000, 10000]])
    m = p["measurements"][-1]
    next(o for o in p["engine"]["objects"] if o["id"] == m["columns"][1])["values"][1] = "nan"
    m["metadata"]["historical_source"] = "clplots2 missing-cell fixture"
    m["metadata"].setdefault("quality", []).append(dict(column=1,
        source_row=m["metadata"]["source_rows"][1], reason="historical_nonfinite", value=None))
    target = [10, 100, math.sqrt(100 * 1000), 1000, math.sqrt(1000 * 10000), 10000, 100000]
    result = build(p, view_kind="heatmap", grid_policy="interpolate_log_f", frequency_Hz=target)
    _, curves = values(result["project"], result["graph_id"])
    z = curves[0][1][2]
    assert z[0] == z[6] == "nan"
    assert z[1:6] == pytest.approx([4e-9, 4.5e-9, 5e-9, 5.5e-9, 6e-9], rel=1e-12, abs=0)
    assert z[7:10] == ["nan"] * 3
    assert z[14:21] == ["nan", pytest.approx(5.1e-9, rel=1e-12, abs=0), "nan", "nan", "nan", pytest.approx(7.1e-9, rel=1e-12, abs=0), "nan"]
    assert result["project"]["comparisons"][-1]["frequency_Hz"] == target


@pytest.mark.parametrize("options", [dict(grid_policy="silent"), dict(frequency_Hz=[100, 1000]),
    dict(grid_policy="interpolate_log_f"), dict(grid_policy="interpolate_log_f", frequency_Hz=[100, 100]),
    dict(grid_policy="interpolate_log_f", frequency_Hz=[0, 100]), dict(view_kind="contour")])
def test_invalid_policy_is_typed_and_atomic(options):
    p = series_project()
    result = execute(p, dict(operation="plot", plot_type="AS C(f,T)",
                            measurement_ids=[m["id"] for m in p["measurements"]], **options))
    assert not result["ok"] and result["error"]["code"] == "plot_failed"
    assert result["project"] == p


@pytest.mark.parametrize("field,value", [("temperature_K", None), ("temperature_K", 200), ("bias_V", 1)])
def test_incompatible_temperature_series_is_rejected(field, value):
    p = series_project()
    p["measurements"][1]["metadata"][field] = value
    result = execute(p, dict(operation="plot", plot_type="AS C(f,T)",
                            measurement_ids=[m["id"] for m in p["measurements"]]))
    assert not result["ok"] and result["project"] == p


@pytest.mark.parametrize("view", ["surface", "heatmap", "lines"])
@pytest.mark.parametrize("format", ["png", "svg", "pdf"])
def test_camera_colour_save_open_export_matches_view(tmp_path, view, format):
    p = series_project()
    result = build(p, view_kind=view)
    p, graph = result["project"], result["graph_id"]
    camera = dict(rotation_xyzw=[-.513269, -.212596, -.318184, .76819],
                  axis_limits=[50, 20000, 180, 330, 0, 1e-8])
    saved = dict(ranges=camera["axis_limits"][:4])
    if view != "heatmap":
        saved["camera"] = camera
    p = run(p, "plot", action="view", graph_id=graph, view=saved,
            colormap="Plasma", color_range_F=[0, 1e-8])["project"]
    path = tmp_path / "map.caplab.json"
    run(p, "save", path=str(path))
    reopened = run(p, "open", path=str(path))["project"]
    assert reopened == p
    target = tmp_path / f"AS_3d_plot.{format}"
    snapshot = run(reopened, "export", path=str(target))["export"]
    assert snapshot["ranges"] == saved["ranges"]
    assert snapshot["color_range"] == [0, 1e-8] and snapshot["colormap"] == "Plasma"
    assert snapshot["x_axis"]["log"] and snapshot["z_axis"]["unit"] == "F"
    if view != "heatmap":
        assert snapshot["camera"] == camera
    assert target.stat().st_size > 1000


def test_cancel_leaves_no_map():
    p = series_project()
    failed = execute(p, dict(operation="plot", plot_type="AS C(f,T)", cancel=True,
                             measurement_ids=[m["id"] for m in p["measurements"]]))
    assert not failed["ok"] and failed["error"]["code"] == "cancelled"
    assert failed["project"] == p


def test_sorted_frequency_view_and_duplicate_frequency_rejection():
    p = series_project()
    measurement = p["measurements"][0]
    for column in measurement["columns"]:
        next(o for o in p["engine"]["objects"] if o["id"] == column)["values"].reverse()
    result = build(p, view_kind="surface")
    _, curves = values(result["project"], result["graph_id"])
    assert curves[0][1][0][:3] == [100, 1000, 10000]
    f = next(o for o in p["engine"]["objects"] if o["id"] == measurement["columns"][0])
    f["values"][1] = f["values"][0]
    before = copy.deepcopy(p)
    failed = execute(p, dict(operation="plot", plot_type="AS C(f,T)",
                             measurement_ids=[m["id"] for m in p["measurements"]]))
    assert not failed["ok"] and "Duplicate" in failed["error"]["message"]
    assert failed["error"]["code"] == "invalid_request" and p == before


def test_nonpositive_parallel_resistance_is_a_gap_in_conductance_map():
    p = series_project(variant="Cp_Rp")
    resistance = p["measurements"][0]["columns"][2]
    next(o for o in p["engine"]["objects"] if o["id"] == resistance)["values"][1] = 0
    meta = p["measurements"][0]["metadata"]
    meta.setdefault("quality", []).append(dict(column=2, source_row=meta["source_rows"][1],
                                              reason="nonpositive_Rp", value=0))
    result = build(p, "G/omega", view_kind="surface")
    _, curves = values(result["project"], result["graph_id"])
    assert curves[0][1][2][1] == "nan"
    assert any("gaps" in d for d in result["project"]["comparisons"][-1]["diagnostics"])
