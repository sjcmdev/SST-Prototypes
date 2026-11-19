from pathlib import Path
import json
from sst_plotlab import WorksheetSession
root = Path("Prototypes/PlotLab")
project = json.loads((root / "examples/p6a-worksheet.plotlab").read_text())
project["objects"] = [o for o in project["objects"] if o["type"] < 3 or o["id"] in (5,6,7,8,9)]
project["recompute"] = []
project["objects"][0]["name"] = "P6b - Set Column Values"
project["objects"][2]["name"] = "Derived columns"
project["objects"][2]["parameters"] = project["objects"][2]["parameters"][:1]
for o in project["objects"]:
    if o["type"] == 3:
        o.pop("formula",None)
        o["errorOf"] = 0
        o["columnType"] = 0
        o["values"] = list(range(1,13)) if o["id"] == 5 else [0]*12
        o["name"] = chr(65+o["id"]-5)
        o["longName"] = {5:"Input",6:"Scaled",7:"Derivative",8:"Integral",9:"Smoothed"}[o["id"]]
        o["display"] = dict(mode=1,digits=2)
        o["units"] = "a.u."
        o["role"] = 1 if o["id"]==5 else 2
        o["parameters"] = [{"id":50,"value":"300"}]
        o["comments"] = "Measured" if o["id"]==5 else "Calculated"
session = WorksheetSession()
def command(action, **fields):
    response = session.command(dict(action=action, **fields))
    assert response["ok"],response
    return response["result"]
command("load_project",project=project)
for column,expression in [(6,"scale*col(A)+i"),(7,"diff(col(B),1)"),(8,"integ(col(B))"),(9,"smooth(col(B),1)")]:
    command("set_column_values",column_id=column,expression=expression,
            before_script="scale = 2" if column==6 else "")
command("recompute")
(root / "examples/p6b-formulas.plotlab").write_text(json.dumps(command("snapshot"),indent=2))
