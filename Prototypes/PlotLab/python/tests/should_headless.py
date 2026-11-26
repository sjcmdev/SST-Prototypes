"""Native Core command/job acceptance for A06/A08/A11/A15 and file edges."""
import json
import os
from pathlib import Path
import tempfile

import numpy as np
from scipy.special import voigt_profile
from sst_plotlab import WorksheetSession
from sst_plotlab.analysis import NamedModel

root = Path(tempfile.mkdtemp(prefix="should-", dir=os.environ["PLOTINT1_RUN"]))
s = WorksheetSession()


def command(action, **fields):
    response = s.command(dict(action=action, **fields))
    assert response["ok"], response
    return response["result"]


def data(name, x, y, x_units="ns", y_units="counts"):
    sheet = s.sheets.new(name)
    xx = sheet.add_column("X", x, units=x_units, role="X")
    yy = sheet.add_column("Y", y, units=y_units)
    return sheet, dict(id=name, version=1, worksheet_id=sheet.id,
                      x_column_id=xx.id, y_column_id=yy.id)


def parameter(name, start, units, lower=-100, upper=100, **fields):
    return dict(name=name, units=units, start_parameter_units=start,
                lower_parameter_units=lower, upper_parameter_units=upper, **fields)


def accept_and_export(result, tag):
    assert result["stage"] == "preview_ready", result
    depth = s._session.undo_depth
    accepted = s.analysis.accept(result["job_id"])
    assert s._session.undo_depth == depth + 1
    sheets = accepted["worksheet_ids"]
    command("undo")
    assert all(ws.id not in sheets for ws in s.sheets)
    command("redo")
    sheet = next(ws for ws in s.sheets if ws.id == sheets[0])
    columns = list(sheet.columns)
    assert len(columns) > 1
    plotted = [columns[1].id] + [c.id for c in columns if c.name == "Baseline" or c.name.endswith(" component")]
    graph = s.plot_columns(sheet.id, plotted, x_column_id=columns[0].id)
    assert graph["ok"], graph
    for fmt in ("png", "svg", "pdf", "jpg"):
        path = root / f"{tag}.{fmt}"
        response = s.export_figure(graph["graph_id"], path, fmt, dpi=100)
        assert response["ok"], response
        header = path.read_bytes()
        assert (header.startswith(b"\x89PNG") if fmt == "png" else
                b"<svg" in header if fmt == "svg" else
                header.startswith(b"%PDF") if fmt == "pdf" else header.startswith(b"\xff\xd8"))
    return sheets


x = np.linspace(0, 8, 81)
a, recipe = data("300K", x, .3 + 4 * np.exp(-x / 1.7))
b, other = data("330K", x, .6 + 7 * np.exp(-x / 1.7))
params = [parameter("baseline", .2, "counts", -1, 2),
          parameter("amplitude", 5, "counts", 0, 10),
          parameter("tau", 1.5, "ns", .1, 4, shared=True)]
recipe["settings"] = dict(operation="exponential", parameters=params)
dataset = dict(id="330K", worksheet_id=b.id, x_column_id=other["x_column_id"],
               y_column_id=other["y_column_id"])
global_result = s.analysis.global_fit(recipe, [dataset])
assert global_result["stage"] == "preview_ready", global_result
assert len(global_result["items"]) == 3
first, second, summary = global_result["items"]
np.testing.assert_allclose(first["parameter_values_parameter_units"], [.3, 4, 1.7], atol=2e-4)
np.testing.assert_allclose(second["parameter_values_parameter_units"], [.6, 7, 1.7], atol=2e-4)
assert first["parameter_values_parameter_units"][2] == second["parameter_values_parameter_units"][2]
assert summary["model_id"] == "global" and summary["degrees_of_freedom"] == 157
assert len(summary["covariance_parameter_units_squared"]) == 25
accept_and_export(global_result, "A06-global")
s._session.save(str(root/"A06.plotlab"))

# Different grids must be copied once at the Python edge, and retained arrays must stay valid.
retained = {}
def user_line(x, offset, slope):
    retained.setdefault(len(x), []).append(x)
    return offset+slope*x
u, ur = data("User grid A", np.linspace(0, 2, 21), 2+3*np.linspace(0, 2, 21))
v, vr = data("User grid B", np.linspace(0, 3, 31), 5+3*np.linspace(0, 3, 31))
ur["settings"] = dict(operation="user", user_model_id="global.line", parameters=[
    parameter("offset", 1, "counts", -10, 10), parameter("slope", 2, "counts/ns", 0, 6, shared=True)])
model = NamedModel("global.line", 1, "ns", "counts", ("offset", "slope"), user_line)
user_result = s.analysis.global_fit(ur, [dict(id="other-grid", worksheet_id=v.id,
    x_column_id=vr["x_column_id"], y_column_id=vr["y_column_id"])], model=model)
assert user_result["stage"] == "preview_ready", user_result
assert set(retained) == {21, 31}
for arrays in retained.values():
    assert len({x.__array_interface__["data"][0] for x in arrays}) == 1
    for array in arrays:
        assert np.isfinite(array).all()
np.testing.assert_allclose(user_result["items"][1]["parameter_values_parameter_units"], [5, 3], atol=2e-5)

sheet, extrapolate = data("Endpoint extrapolation", [0, 1, 2], [1, 3, 5], "s", "V")
extrapolate["settings"] = dict(operation="extrapolate", target_x_units=[-1, 0, 1, 2, 3],
                               output_from_x_units=-1, output_to_x_units=3)
bad = s.analysis.run(extrapolate)
assert bad["stage"] == "failed" and bad["error"] == "invalid_input", bad
extrapolate["settings"]["acknowledge_extrapolation"] = True
extended = s.analysis.run(extrapolate)
assert "Warning" in extended["items"][0]["message"]
assert extended["items"][0]["fitted_y_units"] == [-1, 1, 3, 5, 7]
accept_and_export(extended, "A08-extrapolation")
s._session.save(str(root/"A08.plotlab"))

x = np.linspace(-4, 5, 121)
signal = 2 + .1*x + 3*np.exp(-.5*((x+.5)/.6)**2) + 2/(1+((x-.9)/.5)**2)
sheet, peaks = data("Overlapping bands", x, signal, "eV")
peaks["settings"] = dict(operation="multi_peak",
    peaks=[dict(id="A", shape="gaussian"), dict(id="B", shape="lorentzian")],
    anchors=[dict(x_x_units=-4, y_y_units=1.6), dict(x_x_units=5, y_y_units=2.5)],
    parameters=[parameter("A_height", 2.8, "counts", 0, 6), parameter("A_center", -.4, "eV", -2, 3),
                parameter("A_sigma", .65, "eV", .1, 2), parameter("B_height", 1.8, "counts", 0, 6),
                parameter("B_center", 1, "eV", -2, 3), parameter("B_gamma", .55, "eV", .1, 2)])
fitted = s.analysis.run(peaks)
assert fitted["stage"] == "preview_ready", fitted
r = fitted["items"][0]
np.testing.assert_allclose(r["parameter_values_parameter_units"], [3, -.5, .6, 2, .9, .5], atol=3e-4)
np.testing.assert_allclose(r["fitted_y_units"], np.array(r["baseline_y_units"]) + np.sum(r["component_y_units"], axis=0), atol=1e-12)
accept_and_export(fitted, "A11-multi-peak")
s._session.save(str(root/"A11.plotlab"))

for shape, widths in (("voigt", [1, .3]), ("split_gaussian", [.5, 1])):
    profile = (voigt_profile(x, *widths)/voigt_profile(0, *widths) if shape == "voigt"
               else np.exp(-.5*(x/np.where(x < 0, widths[0], widths[1]))**2))
    ws, model = data(shape, x, 1+3*profile, "eV")
    names = ("p_sigma", "p_gamma") if shape == "voigt" else ("p_sigma_left", "p_sigma_right")
    model["settings"] = dict(operation="multi_peak", peaks=[dict(id="p", shape=shape)],
        anchors=[dict(x_x_units=0, y_y_units=1)],
        parameters=[parameter("p_height", 2, "counts", 0, 6), parameter("p_center", 0, "eV", -1, 1, fixed=True)] +
                   [parameter(name, width, "eV", .01, 3, fixed=True) for name, width in zip(names, widths)])
    result = s.analysis.run(model)
    assert result["stage"] == "preview_ready", result
    np.testing.assert_allclose(result["items"][0]["parameter_values_parameter_units"][0], 3, atol=2e-6)
    np.testing.assert_allclose(result["items"][0]["fitted_y_units"], 1+3*profile, atol=2e-6)
    accept_and_export(result, "A11-"+shape)

stats = s.sheets.new("Repeats")
index = stats.add_column("Index", range(5), role="X")
ca = stats.add_column("A", [1, 2, np.nan, 4, 5], units="counts")
cb = stats.add_column("B", [3, 4, 6, np.nan, 7], units="counts")
command("mask", worksheet_id=stats.id, column_id=ca.id, row_from=1, row_to=2)
reports = []
for axis in ("column", "row"):
    result = command("statistics", worksheet_id=stats.id, column_ids=[ca.id, cb.id], axis=axis)
    command("recompute")
    report = next(ws for ws in s.sheets if ws.id == result["worksheet_ids"][0])
    reports.append(report)
    values = {c.name: np.asarray(c).tolist() for c in report.columns}
    if axis == "column":
        assert values["A count"] == [3] and values["B count"] == [4]
        np.testing.assert_allclose(values["A mean"], [10/3])
        np.testing.assert_allclose(values["B std"], [np.sqrt(10/3)])
    else:
        assert values["count"] == [2, 1, 1, 1, 2]
        assert values["mean"] == [2, 4, 6, 4, 6]
        assert next(c for c in report.columns if c.name == "mean").units == "counts"
    columns = list(report.columns)
    columns[0].role = "Y"
    graph = s.plot_columns(report.id, [columns[0].id])
    assert graph["ok"], graph
    for fmt in ("png", "svg", "pdf", "jpg"):
        assert s.export_figure(graph["graph_id"], root/f"A15-{axis}.{fmt}", fmt, dpi=100)["ok"]

command("filter", worksheet_id=stats.id, filters=[dict(column_id=index.id, kind="range", minimum_column_units=2, maximum_column_units=4)])
filtered = command("statistics", worksheet_id=stats.id, column_ids=[ca.id], exclude_filtered=True)
command("recompute")
filtered_sheet = next(ws for ws in s.sheets if ws.id == filtered["worksheet_ids"][0])
assert np.asarray(filtered_sheet.columns["A count"])[0] == 2

project = root / "should.plotlab"
s._session.save(str(project))
reopened = WorksheetSession()
reopened._session.open(str(project))
assert reopened.analysis_recipes == s.analysis_recipes
assert any(r["recipe"]["global_inputs"] for r in reopened.analysis_recipes)
reopened.sheets["Repeats"]["A"][0] = 3
assert reopened.command(dict(action="recompute"))["ok"]
restored = next(ws for ws in reopened.sheets if ws.id == reports[0].id)
np.testing.assert_allclose(np.asarray(restored["A mean"]), [4])
json.dumps(global_result, allow_nan=False)
(root / "evidence.json").write_text(json.dumps(dict(project=str(project), global_fit=summary,
    exports=sorted(p.name for p in root.iterdir() if p.suffix in {".png", ".svg", ".pdf", ".jpg"})), indent=2))
(Path(os.environ["PLOTINT1_RUN"])/"latest-should.txt").write_text(str(root))
print("plotshould2 native acceptance passed:", root, flush=True)
