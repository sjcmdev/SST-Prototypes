"""Shared collector and GUI command/job batch import through nanobind execute."""
from caplab import execute, parse_lab_as, project_from_preview


def project():
    return project_from_preview(parse_lab_as("100 1e-9 1e6\n1000 2e-9 1e6", "initial.dat"), 300)


def test_unicode_folder_preview_import_and_explicit_error_selection(tmp_path):
    root = tmp_path / "źródła_測定"
    root.mkdir()
    (root / "sub").mkdir()
    for path in [root / "a.DAT", root / "sub" / "b.dat"]:
        path.write_text("100 1e-9 1e6\n1000 2e-9 1e6", encoding="utf-8")
    (root / "bad.dat").write_text("invalid", encoding="utf-8")
    (root / "ignore.bin").write_text("ignored", encoding="utf-8")
    p = project()
    request = dict(paths=[str(root)], options=dict(profile="lab_as_cp_rp", temperature_K=300))
    flat = execute(p, dict(operation="preview", **request))
    assert flat["ok"], flat
    assert flat["project"] == p
    assert len(flat["sources"]) == 2
    tree = execute(p, dict(operation="preview", recursive=True, **request))
    assert tree["ok"], tree
    assert len(tree["sources"]) == 3
    assert sum(s["error"] is not None for s in tree["sources"]) == 1
    assert all(s["root"] == str(root) for s in tree["sources"])
    assert all(s["preview"]["source"] == s["path"] for s in tree["sources"] if s["error"] is None)
    ids = [s["source_id"] for s in tree["sources"] if s["error"] is None]
    failed = execute(p, dict(operation="import", recursive=True, **request))
    assert not failed["ok"] and failed["project"] == p
    imported = execute(p, dict(operation="import", recursive=True, source_ids=ids, **request))
    assert imported["ok"], imported
    assert len(imported["project"]["samples"]) == len(p["samples"]) + 2


def test_missing_source_cancel_and_multi_file_deduplication(tmp_path):
    a, b = tmp_path / "a.dat", tmp_path / "b.dat"
    for path in [a, b]:
        path.write_text("100 1e-9 1e6", encoding="utf-8")
    p = project()
    request = dict(operation="preview", paths=[str(b), str(a), str(b)])
    preview = execute(p, request)
    assert preview["ok"], preview
    assert [s["source_id"] for s in preview["sources"]] == [str(a), str(b)]
    missing = execute(p, dict(operation="preview", paths=[str(a), str(tmp_path / "missing.dat")]))
    assert missing["ok"], missing
    assert missing["sources"][1]["error"]["code"] == "import.source_unreadable"
    invalid = execute(p, dict(operation="preview", paths=[""]))
    assert not invalid["ok"] and invalid["error"]["code"] == "invalid_request"
    for paths in (str(a), {"source": str(a)}, [1]):
        invalid = execute(p, dict(operation="preview", paths=paths))
        assert not invalid["ok"] and invalid["error"]["code"] == "invalid_request"
    cancelled = execute(p, dict(request, cancel=True))
    assert not cancelled["ok"] and cancelled["project"] == p
