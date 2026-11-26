import copy
import csv
import math
import os
import json
from pathlib import Path

import numpy as np
import pytest
from PIL import Image
from sst_plotlab.exporter import build_figure, export_graph, export_data
from sst_plot_export.exporter import export_plots
from sst_plot_export.files import commit_files


def test_cpp_typed_map_matches_export_geometry():
    directory=os.environ.get("PLOTGAP_ARTIFACT_DIR")
    if not directory: pytest.skip("Set PLOTGAP_ARTIFACT_DIR to the native PlotTypes test artifacts")
    snapshot=json.loads((Path(directory)/"snapshot.json").read_text(encoding="utf-8"))
    assert snapshot["series"][0]["x"]==[0,1,2,0,1,2]
    assert snapshot["x_axis"]["unit"]=="um" and snapshot["z_axis"]["unit"]=="counts"
    lines=snapshot["series"][0]["contours"];assert lines
    for line in lines:
        for x,y in (line["a"],line["b"]): assert 1+x+3*y==pytest.approx(line["level"])
    fig=build_figure(snapshot)
    np.testing.assert_allclose(fig.axes[0].collections[1].get_segments(),[[line["a"],line["b"]] for line in lines])
    with Image.open(Path(directory)/"map.jpg") as image:
        assert image.size==(315,236)
        assert image.getpixel((0,0))==pytest.approx((51,102,153),abs=2)


def map_snapshot():
    axis = lambda label, unit: dict(label=label, unit=unit, log=False, grid=False, ticks=[])
    return dict(width_mm=80, height_mm=60, font_family="DejaVu Sans", font_pt=8,
                ranges=[9, 15, 85, 105], x_axis=axis("X", "um"), y_axis=axis("Y", "K"), z_axis=axis("Intensity", "counts"),
                legend=dict(visible=False, outside=False), color_scale=False,
                series=[dict(kind=7, label="Map", x=[10,12,14,10,12,14], y=[100,100,100,90,90,90],
                             z=[1,10,None,100,1000,10000], mask=[0,0,0,0,1,0], grid_columns=3, y_error=[])])


def test_map_orientation_extent_log_policy_and_units():
    snapshot = map_snapshot(); snapshot["z_axis"]["log"] = True
    fig = build_figure(snapshot); ax = fig.axes[0]; mesh = ax.collections[0]
    assert mesh.get_coordinates()[0, :, 0].tolist() == pytest.approx([9,11,13,15])
    assert mesh.get_coordinates()[:, 0, 1].tolist() == pytest.approx([105,95,85])
    assert mesh.get_array().filled(np.nan).flatten().tolist() == pytest.approx([1,10,np.nan,100,np.nan,10000], nan_ok=True)
    assert mesh.norm(100) == pytest.approx(.5)
    assert ax.get_xlabel() == "X (um)" and ax.get_ylabel() == "Y (K)"
    snapshot["series"][0]["z"][1] = 0
    assert build_figure(snapshot).axes[0].collections[0].get_array().mask[0,1]
    snapshot["nonpositive_color"] = 1
    with pytest.raises(ValueError, match="positive Z"): build_figure(snapshot)
    snapshot["nonpositive_color"] = 0; snapshot["color_range"] = [0, 100]
    with pytest.raises(ValueError, match="positive"): build_figure(snapshot)


def test_shared_contour_segments_and_labels():
    snapshot=map_snapshot(); snapshot["contour_style"]=dict(levels=[2.5],color=[1,0,0,1],line_pt=1.5,labels=True)
    snapshot["series"][0]["contours"]=[dict(level=2.5,a=[10,95],b=[14,95])]
    fig=build_figure(snapshot);ax=fig.axes[0]
    np.testing.assert_allclose(ax.collections[1].get_segments()[0],[[10,95],[14,95]])
    assert ax.collections[1].get_linewidths().tolist() == pytest.approx([1.5])
    assert ax.texts[0].get_text() == "2.5"
    del snapshot["series"][0]["contours"]
    with pytest.raises(ValueError,match="shared typed"):build_figure(snapshot)


@pytest.mark.parametrize("format", ["jpeg", "jpg", "png", "svg", "pdf"])
def test_publication_formats_physical_size_background_and_limits(tmp_path,format):
    snapshot=map_snapshot();target=tmp_path/f"map.{format}"
    assert export_graph(snapshot,target,format,300,background=(.2,.4,.6)) == (9,15,85,105)
    if format in ("jpeg","jpg","png"):
        with Image.open(target) as image:
            assert image.size == (945,709)
            if format in ("jpeg","jpg"):
                assert image.mode == "RGB"
                assert image.getpixel((0,0)) == pytest.approx((51,102,153),abs=2)
    if format in ("jpeg","jpg"):
        before=target.read_bytes()
        with pytest.raises(ValueError,match="explicit opaque"):export_graph(snapshot,target,format)
        assert target.read_bytes()==before


def table_snapshot():
    return dict(columns=[dict(id=1,name="Napięcie, µ",unit="V",values=[.12345678901234567,None,-2.5],mask=[0,0,1]),
                         dict(id=2,name="Signal",unit="A",values=[1e-200,math.nan,1e200],mask=[0,0,0])],
                rows=[2,0,1],mask_policy="preserve",missing_policy="empty",source_paths=[])


@pytest.mark.parametrize("format",["csv","tsv","dat"])
def test_data_precision_unicode_units_order_missing_and_mask_roundtrip(tmp_path,format):
    snapshot=table_snapshot();target=tmp_path/f"data.{format}";export_data(snapshot,target,format)
    with target.open(encoding="utf-8",newline="") as stream: rows=list(csv.reader(stream,delimiter="," if format=="csv" else "\t"))
    assert rows[0]==["Napięcie, µ","Napięcie, µ__masked","Signal","Signal__masked"]
    assert rows[1]==["V","","A",""]
    assert rows[2]==["-2.5","1","1e+200","0"]
    assert float(rows[3][0])==snapshot["columns"][0]["values"][0]
    assert float(rows[3][2])==1e-200
    assert rows[4]==["","0","","0"]
    snapshot["mask_policy"]="missing";snapshot["missing_policy"]="nan";export_data(snapshot,target,format)
    with target.open(encoding="utf-8",newline="") as stream: rows=list(csv.reader(stream,delimiter="," if format=="csv" else "\t"))
    assert rows[2]==["NaN","1e+200"]
    snapshot["mask_policy"]="omit_rows";export_data(snapshot,target,format)
    assert len(target.read_text(encoding="utf-8").splitlines())==4


def test_data_cancel_failure_and_source_protection(tmp_path):
    snapshot=table_snapshot();target=tmp_path/"data.csv";target.write_text("previous",encoding="utf-8")
    with pytest.raises(RuntimeError,match="cancelled"):export_data(snapshot,target,"csv",lambda:True)
    assert target.read_text()=="previous"
    snapshot["columns"][0]["values"][0]=math.inf
    with pytest.raises(ValueError,match="Infinite"):export_data(snapshot,target,"csv")
    assert target.read_text()=="previous"
    snapshot["source_paths"]=[str(target)]
    with pytest.raises(ValueError,match="source"):export_data(snapshot,target,"csv")
    assert [p.name for p in tmp_path.iterdir()]==["data.csv"]


def test_atomic_batch_rollback_and_completed_item_survives_cancel(tmp_path,monkeypatch):
    import sst_plot_export.files as files
    first,second=tmp_path/"first",tmp_path/"second";first.write_bytes(b"old1");second.write_bytes(b"old2")
    a,b=tmp_path/"a",tmp_path/"b";a.write_bytes(b"new1");b.write_bytes(b"new2")
    replace=files.os.replace
    def fail_second(source,destination):
        if Path(source)==b:raise OSError("injected commit failure")
        replace(source,destination)
    monkeypatch.setattr(files.os,"replace",fail_second)
    with pytest.raises(OSError,match="injected"):commit_files([(a,first),(b,second)])
    assert first.read_bytes()==b"old1" and second.read_bytes()==b"old2"
    snapshot=map_snapshot();export_graph(snapshot,first,"png")
    completed=first.read_bytes()
    with pytest.raises(RuntimeError,match="cancelled"):
        export_plots([dict(snapshot=snapshot,destination=second,format="png",dpi=100)],lambda:True)
    assert first.read_bytes()==completed and second.read_bytes()==b"old2"
