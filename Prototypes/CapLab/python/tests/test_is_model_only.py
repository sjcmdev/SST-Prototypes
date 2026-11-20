"""Model-only JSON -> nanobind -> Core command/job; no measurement required."""
import json
import pytest
import sst_caplab
from caplab import execute, project_from_preview
from test_m5_impedance import create, project


def empty_sample():
    return project_from_preview({"historical": [], "samples": [{"name": "Model only", "area_m2": None,
                                              "measurements": [], "metadata": {}}]})


def simulate(p, **grid):
    return execute(p, {"operation": "simulate", "settings": {
        "circuit_id": p["circuits"][0]["id"], "temperature_K": 310, "bias_V": .25, **grid}})


@pytest.mark.parametrize("grid,expected", [
    ({"frequency_Hz": [1000, .3, 25]}, [1000, .3, 25]),
    ({"frequency_sweep": {"start_Hz": 1, "stop_Hz": 100, "points_per_decade": 2}},
     [1, 10**.5, 10, 10**1.5, 100]),
])
def test_no_measurement_save_open_and_staleness(grid, expected):
    p = create(empty_sample(), "R0")
    r = simulate(p, **grid)
    assert r["ok"], r
    out = r["project"]["analyses"][-1]
    assert not r["project"]["measurements"] and not r["project"]["sweeps"]
    assert out["inputs"] == [] and out["row_ids"] == []
    assert out["frequency_Hz"] == pytest.approx(expected)
    assert out["model_ReZ_Ohm"] == pytest.approx([p["circuits"][0]["schematic"]["elements"][0]["value"]]*len(expected))
    assert out["temperature_K"] == 310 and out["bias_V"] == .25
    assert out["measured_ReZ_Ohm"] == out["residual_ReZ_Ohm"] == []
    assert len(out["graphs"]) == 5 and out["status"] == "valid"
    saved = json.dumps(r["project"], allow_nan=False)
    assert json.loads(sst_caplab.roundtrip(saved)) == r["project"]
    removed = execute(r["project"], {"operation": "edit_circuit", "edit": {
        "action": "delete", "circuit_id": p["circuits"][0]["id"]}})
    assert removed["ok"] and removed["project"]["analyses"][-1]["status"] == "missing_input"


def test_same_sample_overlay_keeps_original_grid_and_conditions():
    p = create(project(), "R0")
    r = simulate(p, frequency_Hz=[1, 10, 100])
    assert r["ok"], r
    out = r["project"]["analyses"][-1]
    assert len(out["inputs"]) == 1
    assert out["inputs"][0]["temperature_K"] == 270
    assert out["frequency_Hz"] == [1, 10, 100] and out["temperature_K"] == 310
    # Plots are stored as real engine columns, not an interpolated measurement.
    assert out["inputs"][0]["columns"][0] != out["frequency_Hz"]


@pytest.mark.parametrize("grid", [{}, {"frequency_Hz": []}, {"frequency_Hz": [1, 1]},
    {"frequency_Hz": [0]}, {"frequency_Hz": list(range(1, 1203))},
    {"frequency_sweep": {"start_Hz": 1, "stop_Hz": 1e6, "points_per_decade": 201}},
    {"frequency_sweep": {"start_Hz": 10, "stop_Hz": 1, "points_per_decade": 10}},
    {"frequency_sweep": {"start_Hz": 1, "stop_Hz": 10, "points_per_decade": 1.5}},
    {"frequency_Hz": [1], "frequency_sweep": {"start_Hz": 1, "stop_Hz": 10, "points_per_decade": 1}},
])
def test_invalid_grid_is_typed_and_atomic(grid):
    p = create(empty_sample(), "R0")
    r = simulate(p, **grid)
    assert not r["ok"] and r["error"]["code"] == "simulation_failed", r
    assert r["project"] == p


def test_fit_still_requires_a_measurement():
    p = create(empty_sample(), "R0")
    r = execute(p, {"operation": "fit", "settings": {"circuit_id": p["circuits"][0]["id"]}})
    assert not r["ok"] and r["error"]["code"] == "fit_failed"


def test_create_circuit_and_sample_without_import():
    p = project_from_preview({"samples": [], "historical": []})
    r = execute(p, {"operation": "edit_circuit", "edit": {"action": "create",
        "sample_name": "Simulation", "name": "Resistor", "topology": "R0"}})
    assert r["ok"], r
    p = r["project"]
    assert not p["measurements"] and not p["sweeps"]
    assert p["samples"][0]["id"] == p["circuits"][0]["sample_id"]
    r = simulate(p, frequency_Hz=[1000])
    assert r["ok"], r
    p = r["project"]
    assert p["analyses"][-1]["inputs"] == []
    assert p["analyses"][-1]["row_ids"] == []
    assert json.loads(sst_caplab.roundtrip(json.dumps(p))) == p
    json.dumps(r, allow_nan=False)
