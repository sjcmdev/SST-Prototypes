"""Independent lab-like CPE/Warburg spectra; repeatable noise and distant starts."""
import cmath
import json
import math
from pathlib import Path
import random
import pytest
from caplab import execute, import_preview, project_from_preview
from test_m5_impedance import create


@pytest.mark.parametrize("warburg", [False, True])
@pytest.mark.parametrize("noise", [0, .002])
@pytest.mark.parametrize("start", [.6, 1.6])
def test_cpe_warburg_fit_from_distant_starts(warburg, noise, start):
    rng = random.Random(20261120)
    truth = {"R0": 20., "R1": 800., "CPE1": 15e-6, "W1": 120.}
    exponent = .83
    rows = []
    for i in range(31):
        f = 10**(-2+i*.25)
        jw = complex(0, 2*math.pi*f)
        z = truth["R0"] + 1/(1/truth["R1"] + truth["CPE1"]*jw**exponent)
        if warburg:
            z += truth["W1"]/cmath.sqrt(jw)
        z += noise*abs(z)*complex(rng.gauss(0, 1), rng.gauss(0, 1))
        rows.append(f"{f:.17g} {z.real:.17g} {z.imag:.17g}")
    p = project_from_preview(import_preview("\n".join(rows), "synthetic-cpe-warburg.dat", options={
        "profile": "lab_is_z", "imaginary_sign": "ImZ", "temperature_K": 298.15,
        "bias_V": -.2, "area_m2": 5.5e-6}))
    p = create(p, "R0-p(R1,CPE1)" + ("-W1" if warburg else ""))
    c = p["circuits"][0]
    specs = []
    for e in c["schematic"]["elements"]:
        value = truth[e["name"]]
        e["value"] = value*start
        specs.append({"element": e["name"], "field": "value", "unit": e["value_unit"],
                      "free": True, "min": value*.1, "max": value*10})
        if e["name"] == "CPE1":
            e["secondary"] = .7 if start < 1 else .95
            specs.append({"element": e["name"], "field": "secondary", "unit": "1",
                          "free": True, "min": .5, "max": 1.})
    r = execute(p, {"operation": "fit", "settings": {
        "circuit_id": c["id"], "measurement_id": p["measurements"][0]["id"],
        "Z_floor_Ohm": 1., "parameters": specs, "max_iterations": 1600}})
    assert r["ok"], r
    assert r["project"] == p
    a = r["draft"]["project"]["analyses"][-1]
    assert a["status"] == "valid" and a["score"] < a["initial_score"]*.01
    for v in a["parameters"]:
        expected = exponent if v["field"] == "secondary" else truth[v["element"]]
        assert v["min"] <= v["fitted"] <= v["max"]
        assert v["fitted"] == pytest.approx(expected, rel=.04 if noise else .002)
    assert a["covariance_status"] == "estimated_from_jacobian"
    assert a["score"] < (.002 if noise else 1e-10)
    accepted = execute(p, {"operation": "accept_fit", "draft": r["draft"]})
    assert accepted["ok"], accepted
    json.dumps(accepted, allow_nan=False)


def test_real_lab_cp_rp_fixture_has_finite_bounded_cpe_warburg_fit():
    # Frozen PFS I2 lab data; every 60th acquisition row, without interpolation.
    source = Path(__file__).parents[2]/"tests/fixtures/cf_100K_7.dat"
    text = "\n".join(source.read_text().splitlines()[::60])
    p = project_from_preview(import_preview(text, str(source), options={
        "profile": "lab_is_cp_rp", "temperature_K": 100, "bias_V": 0, "area_m2": 5.5e-6}))
    p = create(p, "R0-p(R1,CPE1)-W1")
    c = p["circuits"][0]
    starts = {"R0": 100., "R1": 1e6, "CPE1": 2e-9, "W1": 1e4}
    specs = []
    for e in c["schematic"]["elements"]:
        value = starts[e["name"]]
        e["value"] = value
        specs.append({"element": e["name"], "field": "value", "unit": e["value_unit"],
                      "free": True, "min": value*.001, "max": value*1000})
        if e["name"] == "CPE1":
            specs.append({"element": e["name"], "field": "secondary", "unit": "1",
                          "free": True, "min": .3, "max": 1.})
    r = execute(p, {"operation": "fit", "settings": {
        "circuit_id": c["id"], "measurement_id": p["measurements"][0]["id"],
        "parameters": specs, "Z_floor_Ohm": 1., "max_iterations": 600}})
    assert r["ok"], r
    a = r["draft"]["project"]["analyses"][-1]
    assert a["status"] == "valid" and a["score"] < a["initial_score"]
    assert all(math.isfinite(x) for x in a["residual_ReZ_Ohm"]+a["residual_ImZ_Ohm"])
    assert all(v["min"] <= v["fitted"] <= v["max"] for v in a["parameters"])
    # The model is an empirical approximation here, not a claimed device identification.
    assert a["stop_status"] in {"score_tolerance", "max_iterations"}
