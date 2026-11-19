"""Security and numeric contract of the restricted before-formula interpreter."""
import json
import pytest
from sst_plotlab.formula_script import run, declared_json, inputs_json


def test_project_data_nan_and_full_precision_stable_id():
    id = 2**53 + 1
    script = f"scale = mean(col({id}))\noffset = -2 + sqrt(9)"
    assert json.loads(run(script, json.dumps({str(id): [1, None, 3]}))) == dict(scale=2, offset=1)
    assert json.loads(inputs_json(script)) == [id]
    assert json.loads(declared_json(script)) == ["scale", "offset"]


@pytest.mark.parametrize("script", [
    "import os", "x = open(1)", "x = globals()", "x = eval(1)",
    "x = __import__(1)", "x = (1).__class__", "while True: pass",
    "x = [1 for i in col(5)]", "x = 2**10000000", "x = col(5)*1000000000000",
    "x = col(18446744073709551616)", "x = col(99)", "x = col(5+1)", "x = col(-1)", "_x = 1", "pi = 1",
    "x = 1/0", "x = sqrt(-1)", "x = col(5)", "x = 1\n" * 5000,
], ids=lambda script: script[:40].replace("\n", ";"))
def test_script_cannot_escape_scope_or_create_unbounded_objects(script):
    with pytest.raises((ValueError, ZeroDivisionError)):
        run(script, json.dumps({"5": [1, 2, 3]}))
