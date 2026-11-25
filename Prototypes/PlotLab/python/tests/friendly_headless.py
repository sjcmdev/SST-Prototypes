"""Integration assertions against the real embedded native document."""
from pathlib import Path
import os
import numpy as np
import pandas as pd
from sst_plotlab import WorksheetSession
from sst_plotlab.worksheet import LookupError, plot
from sst_plotlab.runtime import parameters

s = WorksheetSession()
p = s.project
iv = p.sheets.new("I-V 300 K")
assert p.sheets.IV_300_K.id == iv.id
assert p.sheets[0].id == iv.id
p.books[0].long_name = "Sample A"
assert p.books["Sample A"]["I-V 300 K"].id == iv.id
iv.long_name = "Measurement"
assert p.sheets.Measurement.id == iv.id
v = iv.add_column("V", range(5), units="V", role="X")
v.long_name = "Voltage"
current = iv.add_column("I", np.arange(5.) * .1, units="A", role="Y")
assert iv["Voltage"].id == iv.V.id == iv.columns[0].id == iv.A.id
iv.columns[0] = list(range(0, 10, 2))
np.testing.assert_equal(np.asarray(v), [0, 2, 4, 6, 8])
assert not np.asarray(v).flags.writeable
iv["J"] = iv.I / .1
np.testing.assert_allclose(iv.J, range(5))
iv.J.units = "A/cm²"
iv.J.comments = "Current density"
iv.J.long_name = "Density"
assert iv.Density.comments == "Current density"
iv.add_column("P", -iv.V * iv.J, units="W/cm²", role="Y")
np.testing.assert_allclose(iv.P, [0, -2, -8, -18, -32])
assert iv.rows[3].Voltage == 6
view = iv.rows[2:4]
iv.rows[2].Voltage = 7
assert view[0]["Voltage"] == 7
iv.rows.append(V=10, I=.5)
assert len(iv.rows) == 6
assert iv.rows[-1].V == 10
iv["tmp"] = [9]
del iv["tmp"]
try:
    iv["Voltag"]
except LookupError as e:
    assert "did you mean" in str(e) and "Voltage" in str(e)
else:
    raise AssertionError("Typo must have a suggestion")
other = p.books.new("Other").sheets.new("I-V 300 K")
try:
    p.sheets["I-V 300 K"]
except LookupError as e:
    assert "Ambiguous" in str(e) and "Other/" in str(e)
else:
    raise AssertionError("Ambiguous names must fail")
other.name = "Other sheet"
iv.I.mask[1:3] = True
assert iv.I.mask[1]
np.testing.assert_allclose(iv.I.valid, [0, .3, .4, .5])
iv.I.mask[:] = False
np.testing.assert_allclose(np.sin(iv.V), np.sin(np.asarray(iv.V)))
np.testing.assert_allclose(iv.I[iv.V > 5], [.2, .3, .4, .5])
frame = iv.to_pandas()
copy = p.sheets.new("Frame", frame)
np.testing.assert_allclose(copy.Voltage, iv.V)
assert isinstance(frame, pd.DataFrame)

# All script mutations travel snapshot -> typed Core commands -> one undo step.
s._session.active_sheet = iv.id
depth = s._session.undo_depth
result = s.run("sheet['J'] = sheet.I / 0.2\nsheet.J.units = 'A/cm²'\nsheet.rows.append(V=12, I=.6)\nprint('done')")
assert result.ok, result.error
assert "done" in result.output
assert s._session.undo_depth == depth + 1
assert len(iv.rows) == 7
p.undo()
assert len(iv.rows) == 6
p.redo()
assert len(iv.rows) == 7
before = np.asarray(iv.V).copy()
depth = s._session.undo_depth
result = s.run("sheet.V[:] = [99]\nraise ValueError('rollback')")
assert not result.ok and "rollback" in result.error
np.testing.assert_equal(iv.V, before)
assert s._session.undo_depth == depth
result = s.run("while True:\n    pass", timeout_s=.03)
assert not result.ok and "time limit" in result.error
assert result.error_code == "timeout"
import json
assert json.loads(json.dumps(result))["ok"] is False
result = s.run("from sst_plotlab import sheet, sheets, project\nsheet['Imported'] = sheet.I * 2\nprint(sheets[0].id)")
assert result.ok, result.error
assert s._session.undo_depth == depth + 1
np.testing.assert_allclose(iv.Imported, np.asarray(iv.I) * 2)
assert s._session.console("iv = sheet\narea = .105", iv.id).ok
assert s._session.console("iv['FromConsole'] = iv.I / area", iv.id).ok
np.testing.assert_allclose(iv.FromConsole, np.asarray(iv.I) / .105)
assert any(c["insert"] == "add_column" for c in s.complete("iv.", console=True))

# Python formulas retain bindings through rename and structural edits.
out = iv.add_column("Derived", np.zeros(7))
out.set_values("np.asarray(Voltage) * 2 + i")
s._session.recompute()
np.testing.assert_allclose(out, before * 2 + np.arange(1, 8))
v.long_name = "Potential"
v.name = "Bias"
iv.name = "Renamed sheet"
v[:] = np.arange(7.)  # Handle remains bound to the stable ID after rename.
s._session.recompute()
np.testing.assert_allclose(out, np.arange(7.) * 2 + np.arange(1, 8))
out.set_values("result = Voltage * 3\nreturn result")
s._session.recompute()
np.testing.assert_allclose(out, np.arange(7.) * 3)
out.set_values("col(A)*2", mode="origin")
s._session.recompute()
np.testing.assert_allclose(out, np.arange(7.) * 2)
out.set_values("sheets['Renamed sheet'].Bias * 4")
s._session.recompute()
iv.name = "Final sheet"
v.name = "Final bias"
iv.columns[0] = np.arange(7.) + 1
s._session.recompute()
np.testing.assert_allclose(out, (np.arange(7.) + 1) * 4)

# Actual reads change with conditional branches and stay rename-safe.
flag = iv.add_column("Flag", np.ones(7))
branch = iv.add_column("Branch", np.zeros(7))
branch.set_values("I if Flag[0] > 0 else FromConsole")
s._session.recompute()
flag[:] = -np.ones(7)
s._session.recompute()
iv.FromConsole[:] = np.arange(7.) + 20
s._session.recompute()
np.testing.assert_allclose(branch, np.arange(7.) + 20)
out.set_values("np.asarray(Flag) * 3")
s._session.recompute()
branch.set_values("I if Flag[0] > 0 else Derived")
s._session.recompute()
flag[:] = np.ones(7)
s._session.recompute()
flag[:] = -np.ones(7) * 2
s._session.recompute()
np.testing.assert_allclose(branch, np.asarray(flag) * 3)
result = s.run("sheet.Flag[:] = [-3] * 7\nsheet['Consumed'] = sheet.Derived + 1")
assert result.ok, result.error
np.testing.assert_allclose(iv.Consumed, [-8] * 7)
flag[:] = [-4] * 7
result = s.run("sheet['ConsumedExternal'] = sheet.Derived + 1")
assert result.ok, result.error
np.testing.assert_allclose(iv.ConsumedExternal, [-11] * 7)

# A caught failed assignment must not replay rolled-back row growth.
result = s.run("try:\n    sheet.Derived[:] = range(10)\nexcept ValueError:\n    pass\nsheet.I[0] = 8")
assert result.ok, result.error
assert len(iv.rows) == 7
assert iv.I[0] == 8
text_sheet = p.sheets.new("Text frame", pd.DataFrame({"Label": ["one", "two"], "Value": [1., 2.]}))
assert text_sheet.Label.valid.tolist() == ["one", "two"]

for source, expected in [("sheets.", "Final_sheet"), ("books[", "Book1"), ("iv = sheets['Final sheet']\niv.", "add_column"), ("np.lo", "log10"), ("ret", "return")]:
    completion = s._session.complete(source, iv.id)
    assert any(expected in item[0] for item in completion), (source, completion)
assert any(signature and doc for name, signature, doc in s._session.complete("np.log", iv.id))
assert parameters("params(area=0.105, T=(200,330))") == [("area", .105, 0., 0., False), ("T", 265., 200., 330., True)]
assert s.run("params(area=.105, T=(200,330))\nprint(area, T)").ok
assert "0.4" in s.run("params(area=.105)\nprint(area)", parameters={"area": .4}).output
recipe = p.scripts.new("Parameterized", "params(area=.105)\nprint(area)")
assert "0.5" in p.run_script(recipe.id, parameters={"area": .5}).output
recipe.parameters = {"area": .7}
assert "0.7" in recipe.run().output

# Scripts and language survive the real file edge; trust is path+content based.
destination = Path(os.environ["SST_PLOTLAB_TRUST_FILE"]).parent / "friendly.plotlab"
script = s._session.save_script("Recipe", "params(area=.105)\nprint(area)")
s._session.save(str(destination))
loaded = WorksheetSession()
loaded._session.open(str(destination))
assert loaded._session.trusted
assert loaded._session.ids(7, 0) == [recipe.id, script]
assert loaded.project.scripts.Parameterized.parameters == {"area": .7}
untrusted = destination.with_name("untrusted.plotlab")
untrusted.write_bytes(destination.read_bytes())
loaded._session.open(str(untrusted))
assert not loaded._session.trusted
try:
    loaded.run("print('must not run')")
except ValueError as e:
    assert "Trust" in str(e)
else:
    raise AssertionError("Untrusted scripts must be blocked")
loaded._session.trust()
assert loaded.run("print('trusted')").ok
graph = plot(iv.columns[0], iv.I)
assert graph.id
reset = WorksheetSession()
reset.sheets.new("Before")
assert reset.console("old_sheet = sheet\narea=.2").ok
fresh = WorksheetSession()
fresh.sheets.new("After")
fresh_path = destination.with_name("fresh-console.plotlab")
fresh._session.save(str(fresh_path))
reset._session.open(str(fresh_path))
assert reset.console("sheet['AfterReset'] = [3]").ok
np.testing.assert_allclose(reset.sheet.AfterReset, [3])
print("pyws1 friendly API integration passed", flush=True)
