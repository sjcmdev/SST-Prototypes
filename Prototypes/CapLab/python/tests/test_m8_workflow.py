"""M8 PFS P1/P2/E1: real commands/jobs, independent SI fixtures and file readers."""
import copy
import csv
import hashlib
import json
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZipFile

import pytest
from caplab import execute, import_preview, project_from_preview
from caplab.importers import parse_measurement
from caplab.exporter import export_json
from test_m3_analysis import cv, raw, options, dl_options
from test_m7_walter import options as walter_options

FIX = Path(__file__).resolve().parents[2]/"tests/fixtures"
FROZEN = json.loads((FIX/"m8/synthetic.json").read_text(encoding="utf-8"))


def run(p, operation, **request):
    r = execute(p, dict(operation=operation, **request))
    assert r["ok"], json.dumps(r.get("error"), indent=2)
    return r


def pipeline():
    preview = import_preview("100 3e-12 1e6\n1000 2e-12 1e6\n10000 1e-12 1e6", "synthetic-AS.dat",
        options=dict(profile="lab_as_cp_rp", temperature_K=300, bias_V=0, area_m2=1e-6))
    cm, dm = cv(), raw()
    assert cm["columns"] == [FROZEN["cv"]["V_V"], FROZEN["cv"]["C_F"]]
    assert dm["columns"][1] == FROZEN["dlcp"]["deltaV_V"]
    assert dm["columns"][2] == pytest.approx(FROZEN["dlcp"]["C_F"], rel=1e-15)
    for m, profile in [(cm, "lab_cv"), (dm, "lab_dlcp_raw")]:
        text = "\n".join(" ".join(str(c[i]) for c in m["columns"]) for i in range(len(m["columns"][0])))
        preview["samples"][0]["measurements"].append(parse_measurement(text, profile+".dat", profile, ".",
            dict(temperature_K=300, frequency_Hz=1000, amplitude_definition="peak", constant_maximum_voltage=True)))
    p = project_from_preview(preview)
    as_id, cv_id, dl_id = [m["id"] for m in p["measurements"]]
    for mid, settings in [(cv_id, options()), (dl_id, dl_options()), (as_id, dict(type="AS"))]:
        p = run(p, "analyze", settings=dict(settings, measurement_id=mid))["project"]
    assert p["analyses"][0]["V_b_V"] == pytest.approx(.8, rel=1e-6)
    assert p["analyses"][0]["N_m3"] == pytest.approx(1e21, rel=1e-6)
    p = run(p, "edit_defect", edit=dict(action="create", sample_id=p["samples"][0]["id"], name="approved points"))["project"]
    did = p["defects"][0]["id"]
    for t, f in [(200,100),(250,1000),(300,10000)]:
        p = run(p, "edit_defect", edit=dict(action="add", defect_id=did, point=dict(
            measurement_id=None, row_id=None, temperature_K=t, frequency_Hz=f, provenance="M8 independent manual points")))["project"]
    p = run(p, "analyze", settings=dict(type="Arrhenius", defect_id=did, model="omega-T2"))["project"]
    settings = walter_options()
    settings.update(measurement_id=as_id, cv_fit_id=p["analyses"][0]["id"], arrhenius_fit_id=p["analyses"][-1]["id"], xd_source="CV")
    for field in ("V_b_V", "E_F_eV", "zeta_s_1_K_2"):
        del settings[field]
    p = run(p, "walter", settings=settings)["project"]
    assert [a["type"] for a in p["analyses"]] == ["CV", "DLCPPolynomial", "AS", "Arrhenius", "Walter"]
    assert all(a["status"] == "valid" for a in p["analyses"])
    return p


def test_p1_complete_pipeline_save_open_and_exports(tmp_path):
    p = pipeline()
    path = tmp_path/"P1.caplab.json"
    assert run(p, "save", path=str(path))["project"] == p
    assert run(p, "open", path=str(path))["project"] == p
    for format in ("csv", "xlsx"):
        result = run(p, "export", path=str(tmp_path/("P1."+format)))
        assert result["project"] == p
        assert result["export"]["format"] == format
    with (tmp_path/"P1.csv").open(encoding="utf-8", newline="") as stream:
        rows = dict(list(csv.reader(stream))[1:])
    assert json.loads(rows["/analyses/0/V_b_V"]) == pytest.approx(.8, rel=1e-6)
    assert json.loads(rows["/measurement_data/0/units/1"]) == "F"
    assert "/analyses/4/source_analyses/0/model" in rows
    with ZipFile(tmp_path/"P1.xlsx") as book:
        ns = {"s":"http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
        metadata = ET.fromstring(book.read("xl/worksheets/sheet1.xml"))
        actual = dict([[c.find("s:is/s:t", ns).text for c in row] for row in metadata.find("s:sheetData", ns)][1:])
        assert actual == rows
        sheet = ET.fromstring(book.read("xl/worksheets/sheet2.xml"))
        header = sheet.find("s:sheetData", ns)[0]
        assert [c.find("s:is/s:t",ns).text for c in header] == ["row_id","source_row","f [Hz]","C [F]","Rp [Ohm]"]
        raw_row = sheet.find("s:sheetData", ns)[1]
        assert [float(c.find("s:v",ns).text) for c in list(raw_row)[2:]] == [100,3e-12,1e6]
        assert len(ET.fromstring(book.read("xl/workbook.xml")).find("s:sheets",ns)) == 5


def test_p2_deletion_area_point_undo_redo_and_no_rebinding():
    p = pipeline()
    mid = p["measurements"][0]["id"]
    q = run(p, "delete_measurement", measurement_id=mid)["project"]
    assert q["analyses"][2]["status"] == "missing_input"
    assert q["analyses"][-1]["status"] == "missing_input"
    assert mid in [m["id"] for m in q["analyses"][-1]["inputs"]]
    q = run(p, "area", sample_id=p["samples"][0]["id"], area_m2=2e-6)["project"]
    assert [a["status"] for a in q["analyses"]] == ["stale", "stale", "stale", "valid", "stale"]
    d = p["defects"][0]
    q = run(p, "edit_defect", edit=dict(action="remove", defect_id=d["id"], point_id=d["points"][0]["id"]))["project"]
    assert q["analyses"][-2]["status"] == "stale" and q["analyses"][-1]["status"] == "stale"


def test_is_fit_accept_save_open(tmp_path):
    from test_m6_fit import fixture
    p, settings = fixture()
    import sst_caplab
    measurement=json.loads(sst_caplab.measurement(json.dumps(p),settings["measurement_id"]))
    assert measurement["columns"] == [FROZEN["randles"][k] for k in ("frequency_Hz","ReZ_Ohm","ImZ_Ohm")]
    p = run(p,"simulate",settings=settings)["project"]
    draft = run(p,"fit",settings=settings)["draft"]
    p = run(p,"accept_fit",draft=draft)["project"]
    for v in p["analyses"][-1]["parameters"]:
        assert v["fitted"] == pytest.approx({"R0":20,"R1":500,"C1":20e-6}[v["element"]],rel=1e-3)
    path = str(tmp_path/"IS.caplab.json")
    run(p,"save",path=path)
    assert run(p,"open",path=path)["project"] == p


@pytest.mark.parametrize("format",["csv","xlsx"])
def test_export_failure_and_cancel_are_atomic(tmp_path,monkeypatch,format):
    path=tmp_path/("old."+format); path.write_bytes(b"original")
    data=json.dumps(dict(measurement_data=[],name="unicode ??d?"))
    with pytest.raises(RuntimeError,match="cancelled"):
        export_json(data,str(path),lambda: True)
    import os
    def fail(*args):
        raise OSError("injected replacement failure")
    monkeypatch.setattr(os,"replace",fail)
    with pytest.raises(OSError,match="injected"):
        export_json(data,str(path))
    assert path.read_bytes()==b"original"
    assert sorted(v.name for v in tmp_path.iterdir())==[path.name]


def test_export_dependency_and_save_errors_are_explicit(tmp_path,monkeypatch):
    from test_m7_walter import project
    p=project()
    for operation in ("save","open","export"):
        path=str(tmp_path if operation=="save" else tmp_path/operation/"missing"/("file.csv" if operation=="export" else "file.caplab.json"))
        r=execute(p,dict(operation=operation,path=path))
        assert not r["ok"] and r["error"]["code"]==operation+"_failed" and r["project"]==p
    r=execute(p,dict(operation="export",path=str(tmp_path/"x.png")))
    assert not r["ok"] and "ranges" in r["error"]["message"]


def test_frozen_m8_manifest():
    manifest=json.loads((FIX/"m8/sources.json").read_text())
    assert {s["source_family"] for s in manifest["files"]}=={"data_Cd","data_IDUB","data_scaps_ref","synthetic"}
    for s in manifest["files"]:
        assert hashlib.sha256((FIX/s["file"]).read_bytes()).hexdigest()==s["sha256"]
        assert s["expected_source"] and s["tolerance"]


def test_png_snapshot_parity_every_module_and_missing_dependency(tmp_path, monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[4]/"Core/ScientificRuntime/Python"))
    import sst_plot_export.exporter as renderer
    from test_m6_fit import fixture
    p = pipeline()
    is_project, settings = fixture()
    is_project = run(is_project,"simulate",settings=settings)["project"]
    original = renderer.export_graph
    seen=[]
    def capture(snapshot, *args):
        seen.append(copy.deepcopy(snapshot))
        return original(snapshot, *args)
    monkeypatch.setattr(renderer,"export_graph",capture)
    for project in (p, is_project):
        for a in project["analyses"]:
            for graph in a["graphs"]:
                path=tmp_path/(str(graph)+a["type"]+".png")
                r=run(project,"export",graph_id=graph,path=str(path),ranges=[1,10,-1,1])
                if a["type"] == "Circuit" and graph == a["graphs"][0]:
                    assert seen[-1].get("equal_aspect") is True
                    figure = renderer.build_figure(seen[-1])
                    figure.canvas.draw()
                    box = figure.axes[0].get_window_extent()
                    assert box.width / 9 == pytest.approx(box.height / 2, rel=1e-6)
                    figure.clear()
                assert r["export"] == seen[-1]
                assert r["export"]["ranges"] == [1,10,-1,1]
                assert path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
                # The emitted figure consumes the exact series/ranges/styles captured by the worker.
                fig=renderer.build_figure(r["export"])
                assert list(fig.axes[0].get_xlim()) == [1,10]
                assert list(fig.axes[0].get_ylim()) == [-1,1]
                fig.clear()
    # Visibility uses stable plot IDs, not series positions.
    project=p
    first=seen[0]
    graph=p["analyses"][0]["graphs"][0]
    r=run(project,"export",graph_id=graph,path=str(tmp_path/"hidden.png"),ranges=[1,10,-1,1],
          visibility=[dict(plot_id=first["series"][0]["plot_id"],visible=False)])
    assert len(r["export"]["series"]) == len(first["series"])-1
    def unavailable(*args):
        raise ImportError("PNG export requires matplotlib in the embedded Python runtime")
    monkeypatch.setattr(renderer,"export_graph",unavailable)
    r=execute(p,dict(operation="export",path=str(tmp_path/"missing.png"),ranges=[1,10,-1,1]))
    assert not r["ok"] and r["error"]["code"]=="export_failed"
    assert "matplotlib" in r["error"]["message"] and not (tmp_path/"missing.png").exists()


def test_historical_measurement_deletion_preserves_original_references():
    path=FIX/"m2/samples_Cd_diffusion.json"
    p=project_from_preview(import_preview(path.read_text(encoding="utf-8"),str(path),options=dict(profile="propho_json")))
    mid=p["measurements"][0]["id"]
    q=run(p,"delete_measurement",measurement_id=mid)["project"]
    assert q["historical"] == p["historical"]
    assert mid not in [m["id"] for m in q["measurements"]]
    import sst_caplab
    assert json.loads(sst_caplab.roundtrip(json.dumps(q))) == q
