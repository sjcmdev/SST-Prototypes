"""M6 independent Randles data -> nanobind -> Core command/job -> draft/Undo acceptance."""
import json
import math
import random
import pytest
from caplab import execute, import_preview, project_from_preview


def fixture(noisy=False):
    rng = random.Random(6006)
    rows = []
    for i in range(25):
        f = 10**(-1+i*.25)
        z = 20 + 1/complex(1/500, 2*math.pi*f*20e-6)
        if noisy:
            z += abs(z)*complex(rng.gauss(0, .005), rng.gauss(0, .005))
        rows.append(f"{f:.17g} {z.real:.17g} {z.imag:.17g}")
    p = project_from_preview(import_preview("\n".join(rows), "randles.dat", options={
        "profile": "lab_is_z", "imaginary_sign": "ImZ", "temperature_K": 300,
        "bias_V": 0, "area_m2": 1e-6}))
    p = execute(p, {"operation": "edit_circuit", "edit": {"action": "create",
        "sample_id": p["samples"][0]["id"], "name": "Randles", "topology": "R0-p(R1,C1)"}})["project"]
    c = p["circuits"][0]
    for e in c["schematic"]["elements"]:
        e["value"] = {"R0": 35, "R1": 300, "C1": 35e-6}[e["name"]]
    settings = {"measurement_id": p["measurements"][0]["id"], "circuit_id": c["id"],
                "Z_floor_Ohm": 1, "max_iterations": 350, "parameters": [
        {"element": e["name"], "field": "value", "unit": e["value_unit"], "free": True,
         "min": 1e-6 if e["kind"] == 1 else 1, "max": 1e-3 if e["kind"] == 1 else 2000}
        for e in c["schematic"]["elements"]]}
    return p, settings


@pytest.mark.parametrize("noisy", [False, True])
def test_fit_is_draft_until_explicit_acceptance(noisy):
    p, settings = fixture(noisy)
    original = json.dumps(p, sort_keys=True)
    r = execute(p, {"operation": "fit", "settings": settings})
    assert r["ok"], r
    assert r["project"] == p
    assert json.dumps(p, sort_keys=True) == original
    a = r["draft"]["project"]["analyses"][-1]
    assert a["score"] < a["initial_score"]
    assert len(a["covariance"]) == 3 and a["weighting"] == "modulus_floor"
    assert a["covariance_status"] == "estimated_from_jacobian"
    for i, parameter in enumerate(a["parameters"]):
        assert parameter["sigma"] == pytest.approx(math.sqrt(a["covariance"][i][i]))
    assert a["Z_floor_Ohm"] == 1 and len(a["graphs"]) == 7
    if not noisy:
        for v in a["parameters"]:
            expected = {"R0": 20, "R1": 500, "C1": 20e-6}[v["element"]]
            assert abs(v["fitted"]/expected - 1) < 1e-3
    accepted = execute(p, {"operation": "accept_fit", "draft": r["draft"]})
    assert accepted["ok"], accepted
    assert accepted["project"] == r["draft"]["project"]
    json.dumps(accepted, allow_nan=False)
    changed = execute(p, {"operation": "edit_circuit", "edit": {"action": "delete", "circuit_id": settings["circuit_id"]}})["project"]
    stale = execute(changed, {"operation": "accept_fit", "draft": r["draft"]})
    assert not stale["ok"] and "Stale" in stale["error"]["message"]


@pytest.mark.parametrize("case", ["fixed", "bounds", "unit", "floor", "cancel"])
def test_invalid_fit_and_cancel_leave_accepted_parameters(case):
    p, settings = fixture()
    if case == "fixed":
        for v in settings["parameters"]:
            v["free"] = False
    elif case == "bounds":
        settings["parameters"][0]["min"] = 100
    elif case == "unit":
        settings["parameters"][2]["unit"] = "Ohm"
    elif case == "floor":
        settings["Z_floor_Ohm"] = 0
    r = execute(p, {"operation": "fit", "settings": settings, "cancel": case == "cancel"})
    assert not r["ok"], r
    assert r["project"] == p
    assert r["error"]["code"] == ("cancelled" if case == "cancel" else "fit_failed")


def test_another_python_thread_stops_running_core_fit():
    from concurrent.futures import ThreadPoolExecutor
    import time
    p, settings = fixture(True)
    settings.update(max_iterations=10000, score_tolerance=0)

    def stop_running():
        deadline = time.monotonic() + 5
        while True:
            stop = execute({}, {"operation": "cancel", "operation_id": "m6-running-cancellation"})
            if stop["ok"]:
                return stop
            assert stop["error"]["code"] == "not_running", stop
            assert time.monotonic() < deadline, stop
            time.sleep(.01)

    with ThreadPoolExecutor(max_workers=1) as pool:
        stop = pool.submit(stop_running)
        # Core owns main-thread document/event operations; only cancel crosses threads.
        result = execute(p, {"operation": "fit", "settings": settings,
                             "operation_id": "m6-running-cancellation"})
        assert stop.result(timeout=5)["ok"]
    assert not result["ok"] and result["error"]["code"] == "cancelled", result
    assert result["project"] == p
    stopped = execute({}, {"operation": "cancel", "operation_id": "m6-running-cancellation"})
    assert not stopped["ok"] and stopped["error"]["code"] == "not_running"


def test_non_cancel_operation_on_wrong_thread_has_typed_error():
    from concurrent.futures import ThreadPoolExecutor
    p, settings = fixture()
    with ThreadPoolExecutor(max_workers=1) as pool:
        result = pool.submit(execute, p, {"operation": "fit", "settings": settings}).result(timeout=5)
    assert not result["ok"] and result["error"]["code"] == "invalid_request", result
    assert "Core main thread" in result["error"]["message"]
