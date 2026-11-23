"""Run in the embedded interpreter: PlotLab --worksheet-script <this file>."""
from sst_plotlab import WorksheetSession

if "fixture" not in globals():
    def obj(id, parent, type, name, **extra):
        return dict(id=id, parent=parent, type=type, name=name, revision=0,
                    order=id, **extra)
    fixture = dict(format="sst.plotlab", version=1, id=1, root=2, nextId=10,
                   revision=0, nextObjectRevision=1, objects=[
        obj(2, 0, 0, "Project"), obj(3, 2, 1, "Book"),
        obj(4, 3, 2, "Sheet", rows=[7, 8, 9], source={}),
        obj(5, 4, 3, "X", role=1, units="s", longName="Time", comments="",
            errorOf=0, columnType=0, values=[1, 2, 3]),
        obj(6, 4, 3, "Y", role=2, units="V", longName="Signal", comments="",
            errorOf=0, columnType=0, values=[2, 4, 6])])

session = WorksheetSession()
def command(action, **fields):
    response = session.command(dict(action=action, **fields))
    assert response["ok"], response
    return response["result"]
command("load_project", project=fixture)
objects = command("snapshot")["objects"]
sheet = next(o["id"] for o in objects if o["type"] == 2)
columns = [o["id"] for o in objects if o["parent"] == sheet]
x, y = columns[:2]

first_graph = session.plot_columns(sheet, [y], kind=2)
assert first_graph["ok"], first_graph
second_graph = session.plot_columns(sheet, [y], kind=8)
assert second_graph["ok"] and second_graph["graph_id"] != first_graph["graph_id"]
added = session.plot_columns(sheet, [y], graph_id=first_graph["graph_id"])
assert added["ok"] and added["graph_id"] == first_graph["graph_id"]
command("undo")
command("undo")
command("undo")
bad = session.plot_columns(sheet, [0])
assert not bad["ok"] and bad["error"]["code"] == "plot.columns"
def snapshot_object(id):
    return next(o for o in command("snapshot")["objects"] if o["id"] == id)
def content():
    return [{k: v for k, v in o.items() if k != "revision"}
            for o in command("snapshot")["objects"]]
def undo_redo(action, **fields):
    before = content()
    command(action, **fields)
    after = content()
    command("undo")
    assert content() == before, action
    command("redo")
    assert content() == after, action

for role in ["X", "Y", "Z", "xErr", "yErr", "Label", "Disregard", ""]:
    undo_redo("column_settings", column_id=y, role=role)
undo_redo("column_settings", column_id=y, role="Y", long_name="Current",
          units="A", comments="Measured")
assert command("paired_x", column_id=y)["column_id"] == x
for header in ["Long Name", "Units", "Comments", "F(x)", "Sparklines"]:
    undo_redo("header_visibility", worksheet_id=sheet, header=header, visible=False)
    assert header in snapshot_object(sheet)["hiddenHeaders"]
    command("header_visibility", worksheet_id=sheet, header=header, visible=True)
undo_redo("parameter_add", worksheet_id=sheet, name="T")
parameter = snapshot_object(sheet)["parameters"][0]["id"]
undo_redo("parameter_value", column_id=y, parameter_id=parameter, value="300 K")
assert snapshot_object(y)["parameters"] == [dict(id=parameter, value="300 K")]
undo_redo("parameter_visibility", worksheet_id=sheet, parameter_id=parameter, visible=False)
assert snapshot_object(sheet)["parameters"][0]["visible"] is False
undo_redo("parameter_remove", worksheet_id=sheet, parameter_id=parameter)
assert snapshot_object(y)["parameters"] == []
command("undo")
assert snapshot_object(y)["parameters"][0]["value"] == "300 K"

original = snapshot_object(y)["values"]
for mode in ["general", "decimal", "significant", "scientific", "engineering",
             "text", "date_time", "categorical"]:
    undo_redo("column_settings", column_id=y, display=dict(mode=mode, digits=3))
    assert command("render_cell", column_id=y, row_index=0)["text"]
    assert command("copy_cell", column_id=y, row_index=0)["text"] == str(original[0]).removesuffix(".0")
    assert snapshot_object(y)["values"] == original
saved = command("snapshot")
command("load_project", project=saved)
assert snapshot_object(y)["display"]["mode"] == 7
assert snapshot_object(y)["parameters"][0]["value"] == "300 K"
for request in [dict(action="column_settings", column_id=y, role="bad"),
                dict(action="column_settings", column_id=y, display=dict(mode="decimal", digits=-1)),
                dict(action="parameter_add", worksheet_id=sheet, name=""),
                dict(action="parameter_value", column_id=y, parameter_id=999999, value="x"),
                dict(action="header_visibility", worksheet_id=sheet, header="bad", visible=False),
                dict(action="render_cell", column_id=y, row_index=-1)]:
    response = session.command(request)
    assert not response["ok"] and response["error"]["type"] == "validation", response
print("Worksheet headless: all commands, undo/redo, formats and validation passed")
