import pytest

sst_caplab = pytest.importorskip("sst_caplab")

from caplab import execute, parse_lab_as, project_from_preview


def project():
    return project_from_preview(parse_lab_as("100 8.8601e-10 134160", "cf_100K.dat"), 100)


def step(previous, operation, **fields):
    result = execute(previous["project"], {
        "operation": operation, "workspace": previous["workspace"], **fields,
    })
    assert result["ok"], result
    return result


def test_workspace_commands_preserve_projects_and_cancel_close(tmp_path):
    first = execute(project(), {"operation": "workspace"})
    assert first["ok"], first
    first_id = first["workspace"]["active_project_id"]
    second = step(first, "new")
    second_id = second["workspace"]["active_project_id"]
    assert first_id != second_id
    assert len(second["workspace"]["projects"]) == 2
    first = step(second, "activate", project_id=first_id)
    sample_id = first["project"]["samples"][0]["id"]
    edited = step(first, "area", sample_id=sample_id, area_m2=2e-6)
    closing = step(edited, "close")
    assert closing["workspace"]["close_requested"]
    cancelled = step(closing, "close_decision", decision="cancel")
    assert not cancelled["workspace"]["close_requested"]
    assert cancelled["workspace"]["projects"][0]["dirty"]
    closing = step(cancelled, "close")
    failed = execute(closing["project"], {
        "operation": "close_decision", "workspace": closing["workspace"],
        "decision": "save", "path": str(tmp_path),
    })
    assert not failed["ok"]
    assert failed["error"]["code"] == "caplab.document"
    assert failed["workspace"]["active_project_id"] == first_id
    assert failed["workspace"]["close_requested"]
    saved = step(failed, "close_decision", decision="save", path=str(tmp_path / "saved.caplab.json"))
    assert saved["workspace"]["active_project_id"] == second_id
    assert len(saved["workspace"]["projects"]) == 1
    assert saved["workspace"]["recent_projects"] == [str(tmp_path / "saved.caplab.json")]


def test_open_dirty_project_adds_document_and_deduplicates_path(tmp_path):
    path = str(tmp_path / "other.caplab.json")
    saved = execute(project(), {"operation": "save_as", "path": path})
    assert saved["ok"], saved
    new = step(saved, "new")
    active = new["workspace"]["active_project_id"]
    opened = step(new, "open", path=path)
    assert len(opened["workspace"]["projects"]) == 2
    assert opened["workspace"]["active_project_id"] != active
    assert opened["workspace"]["recent_projects"] == [path]
    invalid = execute(opened["project"], {
        "operation": "activate", "workspace": opened["workspace"], "project_id": 999,
    })
    assert not invalid["ok"]
    assert invalid["error"]["code"] == "caplab.workspace"
