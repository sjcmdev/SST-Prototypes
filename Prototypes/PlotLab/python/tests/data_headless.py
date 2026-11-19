"""Actual nanobind/Core command path; run with --worksheet-script."""
from sst_plotlab import WorksheetSession

def obj(id, parent, type, name, **extra):
    return dict(id=id, parent=parent, type=type, name=name, revision=0, order=id, **extra)
fixture = dict(format="sst.plotlab", version=1, id=1, root=2, nextId=10,
               revision=0, nextObjectRevision=1, objects=[
    obj(2, 0, 0, "Project"), obj(3, 2, 1, "Book"),
    obj(4, 3, 2, "Sheet", rows=[7, 8, 9], source={}),
    obj(5, 4, 3, "X", role=1, units="s", longName="Time", comments="", errorOf=0, columnType=0, values=[1, 2, 3]),
    obj(6, 4, 3, "Y", role=2, units="V", longName="Signal", comments="", errorOf=0, columnType=0, values=[2, 4, 6])])
session = WorksheetSession()
def command(action, **fields):
    response = session.command(dict(action=action, **fields))
    assert response["ok"], response
    return response["result"]
def content():
    objects = command("snapshot")["objects"]
    for obj in objects: obj.pop("revision", None)
    return objects
command("load_project", project=fixture)
requests = [
    dict(action="sort", keys=[dict(column_id=5, descending=True)]),
    dict(action="filter", filters=[dict(column_id=5, kind="range", minimum_column_units=2, maximum_column_units=3)]),
    dict(action="mask", column_id=6, row_from=0, row_to=1),
    dict(action="find_replace", column_ids=[6], find=4, replace=9),
    dict(action="transpose"), dict(action="stack"), dict(action="split", at_row=1),
    dict(action="join", other_worksheet_id=4),
    dict(action="extract", filters=[dict(column_id=5, kind="top_n", count=2)]),
    dict(action="reduce", every_n=2),dict(action="reduce", every_n=2, mode="group_average"),
    dict(action="normalize", column_id=6, mode="max"),
    dict(action="interpolate", column_id=6, x_column_id=5, new_x_column_units=[1,1.5,3]),
]
for request in requests:
    before = content()
    result = command(worksheet_id=4, **request)
    assert result["state"] == "completed"
    after = content()
    command("undo"); assert content() == before, request
    command("redo"); assert content() == after, request
    command("undo")
command("stack", worksheet_id=4)
snapshot = command("snapshot")["objects"]
stacked = next(obj for obj in snapshot if obj["type"]==2 and obj["id"]!=4)
columns = [obj["id"] for obj in snapshot if obj["parent"]==stacked["id"]]
command("unstack", worksheet_id=stacked["id"], key_column_id=columns[0], value_column_id=columns[1])
command("mask", worksheet_id=4, column_id=6, row_from=1, row_to=2)
assert command("analysis_column", column_id=6)["values_column_units"] == [2, None, 6]
command("statistics", worksheet_id=4)
command("recompute")
assert not session.command(dict(action="reduce",worksheet_id=4,every_n=0))["ok"]
print("P6c embedded operations passed", flush=True)
