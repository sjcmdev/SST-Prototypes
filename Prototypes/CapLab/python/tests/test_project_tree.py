"""Project-tree JSON parity through the real embedded Core command path."""
from copy import deepcopy
from pathlib import Path
from caplab import execute, import_preview, project_from_preview
from test_m8_workflow import pipeline


def run(p, action, **fields):
    result = execute(p, dict(operation="tree", edit=dict(action=action, **fields)))
    assert result["ok"], result.get("error")
    return result["project"]


def project():
    return project_from_preview(import_preview("100 3e-12 1e6\n1000 2e-12 1e6\n10000 1e-12 1e6",
        "AS.dat", options=dict(profile="lab_as_cp_rp", temperature_K=300, bias_V=0, area_m2=1e-6)))


def test_tree_sample_metadata_move_merge_delete_and_selection():
    p = project()
    sid, mid = p["samples"][0]["id"], p["measurements"][0]["id"]
    p = run(p, "rename_sample", sample_id=sid, name="Series")
    p = run(p, "sample_metadata", sample_id=sid, area_m2=2e-6, material="CIGS", notes="A")
    p = run(p, "duplicate_sample", sample_id=sid, name="Copy")
    target = p["samples"][-1]["id"]
    assert p["samples"][-1]["metadata"]["notes"] == "A"
    assert p["measurements"][-1]["id"] != mid
    p = run(p, "move_measurements", measurement_ids=[mid], target_sample_id=target)
    assert p["measurements"][0]["sample_id"] == target
    selected = execute(p, dict(operation="select", selection=dict(kind="measurement", id=mid)))
    assert selected["ok"] and selected["selection"]["sample_id"] == target
    p = run(p, "delete_sample", sample_id=target)
    assert not p["measurements"]
    assert not execute(p, dict(operation="select", selection=dict(kind="measurement", id=mid)))["ok"]


def test_duplicate_full_analysis_pipeline_remaps_all_ids_and_snapshots():
    p = pipeline()
    original = deepcopy(p)
    sid = p["samples"][0]["id"]
    p = run(p, "duplicate_sample", sample_id=sid, name="Pipeline copy")
    assert len(p["analyses"]) == 2 * len(original["analyses"])
    assert len({a["id"] for a in p["analyses"]}) == len(p["analyses"])
    for a, b in zip(original["analyses"], p["analyses"][len(original["analyses"]):]):
        assert a["type"] == b["type"] and a["status"] == b["status"]
        assert set(a["graphs"]).isdisjoint(b["graphs"])
    assert p["defects"][-1]["sample_id"] != sid
    victim = p["analyses"][0]["id"]
    p = run(p, "delete_analysis", analysis_id=victim)
    assert all(a["id"] != victim for a in p["analyses"])
    assert any(a["status"] == "missing_input" for a in p["analyses"])


def test_import_into_existing_sample_keeps_explicit_temperature(tmp_path):
    p = project()
    sid = p["samples"][0]["id"]
    path = tmp_path / "another_200K.dat"
    path.write_text("100 1e-12 1e6\n1000 2e-12 1e6")
    result = execute(p, dict(operation="import", path=str(path), target_sample_id=sid,
        options=dict(profile="lab_as_cp_rp", temperature_K=200)))
    assert result["ok"], result.get("error")
    assert len(result["project"]["samples"]) == 1
    assert result["project"]["measurements"][-1]["sample_id"] == sid
    assert result["project"]["measurements"][-1]["metadata"]["temperature_K"] == 200


def test_unknown_ids_and_invalid_metadata_are_typed_failures():
    p = project()
    for edit in [dict(action="delete_sample", sample_id=999999),
                 dict(action="sample_metadata", sample_id=p["samples"][0]["id"], area_m2=-1),
                 dict(action="move_measurements", measurement_ids=[999999], target_sample_id=p["samples"][0]["id"])]:
        result = execute(p, dict(operation="tree", edit=edit))
        assert not result["ok"] and result["error"]["code"] == "caplab.tree"
        assert result["project"] == p

def test_edited_project_roundtrip(tmp_path):
    import json
    p = pipeline()
    sid = p["samples"][0]["id"]
    p = run(p, "duplicate_sample", sample_id=sid, name="Temperature series copy")
    p = run(p, "sample_metadata", sample_id=sid, material="CIGS", notes="Series T / f / bias")
    path = tmp_path / "tree.caplab.json"
    saved = execute(p, dict(operation="save", path=str(path)))
    assert saved["ok"], saved.get("error")
    opened = execute(p, dict(operation="open", path=str(path)))
    assert opened["ok"] and opened["project"] == p
    assert json.loads(path.read_text(encoding="utf-8")) == p

def test_analysis_preview_requires_explicit_acceptance():
    p = project()
    preview = execute(p, dict(operation="analyze", settings=dict(type="AS", measurement_id=p["measurements"][0]["id"], preview=True)))
    assert preview["ok"] and preview["project"] == p and preview["draft"] is not None
    accepted = execute(p, dict(operation="accept_preview", draft=preview["draft"]))
    assert accepted["ok"] and len(accepted["project"]["analyses"]) == 1
    changed = run(p, "sample_metadata", sample_id=p["samples"][0]["id"], notes="changed")
    assert not execute(changed, dict(operation="accept_preview", draft=preview["draft"]))["ok"]
