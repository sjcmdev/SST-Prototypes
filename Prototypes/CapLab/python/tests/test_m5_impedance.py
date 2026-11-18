"""M5 exercises real nanobind -> Core command/job -> document, without GUI."""
import json
import math
import pytest
import sst_caplab
from caplab import execute, import_preview, project_from_preview


def project():
    preview = import_preview("1234.5 100 0\n7.25 100 -10\n94001 100 -1", "is.dat", options={
        "profile": "lab_is_z", "imaginary_sign": "ImZ", "temperature_K": 270,
        "bias_V": -.15, "area_m2": 1e-6})
    return project_from_preview(preview)


def create(p, topology="R0-p(R1,C1)"):
    r = execute(p, {"operation": "edit_circuit", "edit": {"action": "create",
        "sample_id": p["samples"][0]["id"], "name": "Randles", "topology": topology}})
    assert r["ok"], r
    return r["project"]


@pytest.mark.parametrize("topology", ["R0", "C0", "R0-C0", "R0-p(R1,C1)"])
def test_analytic_headless_command_job_roundtrip(topology):
    p = create(project(), topology)
    circuit = p["circuits"][0]
    for e in circuit["schematic"]["elements"]:
        e["value"] = 20e-6 if e["kind"] == 1 else 500 if e["name"] == "R1" else 20
        e["x"] += 23
    p = execute(p, {"operation": "edit_circuit", "edit": {"action": "replace",
        "circuit_id": circuit["id"], "schematic": circuit["schematic"]}})["project"]
    r = execute(p, {"operation": "simulate", "settings": {"circuit_id": circuit["id"],
        "measurement_id": p["measurements"][0]["id"]}})
    assert r["ok"], r
    out = r["project"]["analyses"][-1]
    assert out["status"] == "valid", out["diagnostics"]
    assert out["temperature_K"] == 270 and out["bias_V"] == -.15
    m = json.loads(sst_caplab.measurement(json.dumps(p), p["measurements"][0]["id"]))
    assert out["frequency_Hz"] == m["columns"][m["names"].index("f")]
    for f, re, im in zip(out["frequency_Hz"], out["model_ReZ_Ohm"], out["model_ImZ_Ohm"]):
        yc = complex(0, 2*math.pi*f*20e-6)
        expected = {"R0": 20, "C0": 1/yc, "R0-C0": 20+1/yc,
                    "R0-p(R1,C1)": 20+1/(1/500+yc)}[topology]
        assert abs(complex(re, im)-expected)/abs(expected) < 1e-8
    assert len(out["graphs"]) == 7
    assert out["circuit"]["schematic"] == circuit["schematic"]
    assert json.loads(sst_caplab.roundtrip(json.dumps(r["project"]))) == r["project"]
    json.dumps(r, allow_nan=False)


def test_errors_are_typed_and_bad_edits_do_not_change_input():
    p = create(project(), "R0")
    original = json.dumps(p, sort_keys=True)
    r = execute(p, {"operation": "edit_circuit", "edit": {"action": "replace", "circuit_id": 999,
        "schematic": p["circuits"][0]["schematic"]}})
    assert not r["ok"] and r["error"]["code"] == "invalid_request"
    assert json.dumps(p, sort_keys=True) == original
    r = execute(p, {"operation": "simulate", "settings": {"circuit_id": 999,
        "measurement_id": p["measurements"][0]["id"]}})
    assert not r["ok"] and r["error"]["code"] == "simulation_failed"
    assert r["project"] == p


def test_circuit_edits_and_deletion_invalidate_without_rebinding():
    p = create(project(), "R0")
    cid = p["circuits"][0]["id"]
    p = execute(p, {"operation": "simulate", "settings": {"circuit_id": cid,
        "measurement_id": p["measurements"][0]["id"]}})["project"]
    schematic = p["circuits"][0]["schematic"]
    schematic["elements"][0]["value"] = 200
    p = execute(p, {"operation": "edit_circuit", "edit": {"action": "replace",
        "circuit_id": cid, "schematic": schematic}})["project"]
    assert p["analyses"][-1]["status"] == "stale"
    p = execute(p, {"operation": "edit_circuit", "edit": {"action": "delete", "circuit_id": cid}})["project"]
    assert p["analyses"][-1]["status"] == "missing_input"
    assert p["analyses"][-1]["circuit"]["id"] == cid
