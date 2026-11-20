"""Shared exporter through the real nanobind -> command -> job path."""
import csv
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[4]/"Core/ScientificRuntime/python"))
import pytest
from caplab import execute
from test_m8_workflow import pipeline, run


def test_selected_graph_and_batch_all_formats(tmp_path):
    import sst_plot_export.exporter as renderer
    p = pipeline()
    graphs = p["analyses"][0]["graphs"][:2]
    for format in ("png", "svg", "pdf"):
        result = run(p, "export", path=str(tmp_path/("single."+format)), graph_id=graphs[0], ranges=[1, 10, -1, 1])
        assert result["export"]["graph_id"] == graphs[0]
        assert result["export"]["ranges"] == [1, 10, -1, 1]
        result = run(p, "export", path=str(tmp_path/("batch."+format)), dpi=120,
                     graphs=[dict(graph_id=g, ranges=[1, 10, -1, 1]) for g in graphs])
        assert result["project"] == p
        assert [v["graph_id"] for v in result["export"]["graphs"]] == graphs
        for item in result["export"]["graphs"]:
            path = Path(item["path"])
            assert path.name == f"batch-{item['graph_id']}.{format}"
            assert path.stat().st_size > 500
    with (tmp_path/"single.png").open("rb") as stream:
        assert stream.read(8) == b"\x89PNG\r\n\x1a\n"
    assert (tmp_path/"single.pdf").read_bytes().startswith(b"%PDF")
    assert "<svg" in (tmp_path/"single.svg").read_text()


def test_batch_render_and_commit_failure_restore_all_files(tmp_path, monkeypatch):
    import sst_plot_export.exporter as renderer
    import sst_plot_export.files as files
    p = pipeline()
    graphs = p["analyses"][0]["graphs"][:2]
    paths = [tmp_path/f"batch-{g}.svg" for g in graphs]
    for path in paths: path.write_bytes(b"original")
    request = dict(operation="export", path=str(tmp_path/"batch.svg"),
                   graphs=[dict(graph_id=g,ranges=[1,10,-1,1]) for g in graphs])
    original = renderer.build_figure
    calls = 0
    def fail_render(*args):
        nonlocal calls
        calls += 1
        if calls == 2: raise ValueError("injected second render failure")
        return original(*args)
    monkeypatch.setattr(renderer,"build_figure",fail_render)
    result=execute(p,request)
    assert not result["ok"] and result["error"]["code"] == "export_failed"
    assert all(path.read_bytes() == b"original" for path in paths)
    assert set(tmp_path.iterdir()) == set(paths)
    monkeypatch.setattr(renderer,"build_figure",original)
    replace = files.os.replace
    def fail_commit(source, destination):
        if Path(destination)==paths[1]: raise OSError("injected second commit failure")
        return replace(source,destination)
    monkeypatch.setattr(files.os,"replace",fail_commit)
    result=execute(p,request)
    assert not result["ok"] and result["error"]["code"] == "export_failed"
    assert all(path.read_bytes() == b"original" for path in paths)
    assert set(tmp_path.iterdir()) == set(paths)


def test_batch_validates_graphs_ranges_and_dpi(tmp_path):
    p=pipeline(); graph=p["analyses"][0]["graphs"][0]
    for extra in [dict(graphs=[]), dict(graphs=[dict(graph_id=graph)]),
                  dict(graphs=[dict(graph_id=0,ranges=[1,10,1,10])]),
                  dict(graphs=[dict(graph_id=graph,ranges=[1,10,1,10])]*2),
                  dict(graph_id=graph,ranges=[1,10,1,10],dpi=0)]:
        result=execute(p,dict(operation="export",path=str(tmp_path/"bad.svg"),**extra))
        assert not result["ok"] and result["error"]["code"] in ("invalid_request","export_failed")
        assert not (tmp_path/"bad.svg").exists()


def test_lab_result_tables_preserve_units_status_and_ids(tmp_path):
    p=pipeline()
    result=run(p,"export",path=str(tmp_path/"lab.csv"))
    assert result["export"]["results_path"] == str(tmp_path/"lab.results.csv")
    with (tmp_path/"lab.results.csv").open(encoding="utf-8",newline="") as stream:
        rows=list(csv.DictReader(stream))
    vb=next(row for row in rows if row["quantity"]=="V_b_V")
    assert float(vb["value"]) == pytest.approx(.8)
    assert vb["unit"]=="V" and vb["analysis_id"]==str(p["analyses"][0]["id"])
    assert vb["status"]=="valid"
    assert any(row["quantity"]=="density_cm3_eV" and row["unit"]=="cm^-3 eV^-1" for row in rows)
    assert any(row["quantity"]=="E_a_eV" and row["unit"]=="eV" for row in rows)
    assert any(row["quantity"]=="C0" and row["unit"]=="F" for row in rows)

    from zipfile import ZipFile
    from xml.etree import ElementTree as ET
    run(p, "export", path=str(tmp_path/"lab.xlsx"))
    ns = {"s": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
    with ZipFile(tmp_path/"lab.xlsx") as book:
        sheets = ET.fromstring(book.read("xl/workbook.xml")).find("s:sheets", ns)
        assert sheets[-1].get("name") == "Results"
        sheet = ET.fromstring(book.read(f"xl/worksheets/sheet{len(sheets)}.xml"))
        values = [[c.findtext("s:is/s:t", c.findtext("s:v", "", ns), ns) for c in row]
                  for row in sheet.find("s:sheetData", ns)]
        vb_xlsx = next(dict(zip(values[0], row)) for row in values[1:] if "V_b_V" in row)
        assert float(vb_xlsx["value"]) == pytest.approx(.8) and vb_xlsx["unit"] == "V"


def test_csv_result_sidecar_failure_is_atomic(tmp_path, monkeypatch):
    import sst_plot_export.files as files
    from caplab.exporter import export_json
    import json
    path=tmp_path/"data.csv"; result=tmp_path/"data.results.csv"
    path.write_bytes(b"metadata before"); result.write_bytes(b"results before")
    replace=files.os.replace
    def fail(source,destination):
        if Path(destination)==result: raise OSError("injected sidecar failure")
        return replace(source,destination)
    monkeypatch.setattr(files.os,"replace",fail)
    with pytest.raises(OSError,match="sidecar"):
        export_json(json.dumps(dict(measurement_data=[],analyses=[])),str(path))
    assert path.read_bytes()==b"metadata before" and result.read_bytes()==b"results before"
    assert set(tmp_path.iterdir())=={path,result}


def test_rollback_continues_and_retains_original_if_restore_fails(tmp_path, monkeypatch):
    import sst_plot_export.files as files
    paths = [tmp_path / f"graph-{i}.svg" for i in range(3)]
    staged = [tmp_path / f"stage-{i}" for i in range(3)]
    for path in paths: path.write_bytes(b"original")
    for path in staged: path.write_bytes(b"new")
    replace = files.os.replace
    def fail(source, destination):
        if Path(destination) == paths[2] or (Path(destination) == paths[1] and Path(source) not in staged):
            raise OSError("injected commit / restore failure")
        return replace(source, destination)
    monkeypatch.setattr(files.os, "replace", fail)
    with pytest.raises(RuntimeError, match="Restore failed") as error:
        files.commit_files(list(zip(staged, paths)))
    assert paths[0].read_bytes() == paths[2].read_bytes() == b"original"
    backups = list(tmp_path.glob(".sst-export-backup-*"))
    assert len(backups) == 1 and backups[0].read_bytes() == b"original"
    assert str(backups[0]) in str(error.value)
