import hashlib
import json
from pathlib import Path

import pytest
import sst_caplab
from caplab import parse_lab_as, project_from_preview

FIXTURE = Path(__file__).resolve().parents[2] / "tests/fixtures/cf_100K_7.dat"


def test_pfs_i2_real_import_and_cpp_roundtrip():
    text = FIXTURE.read_text()
    manifest = json.loads(FIXTURE.with_name("manifest.json").read_text())
    assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == manifest["sha256"]
    preview = parse_lab_as(text, str(FIXTURE))
    assert [len(c) for c in preview["columns"]] == [40, 40, 40]
    assert [c[0] for c in preview["columns"]] == [100, 8.8601e-10, 134160]
    assert preview["temperature_K"] is None
    assert preview["suggested_temperature_K"] == 100
    project = project_from_preview(preview, 100)
    assert project["sweeps"][0]["temperature_K"] == 100
    assert project["sweeps"][0]["sample_id"] == project["samples"][0]["id"]
    assert json.loads(sst_caplab.roundtrip(json.dumps(project))) == project


@pytest.mark.parametrize("text,decimal", [
    ("# T=100 K\nf Cp Rp\n100 1e-9 200", "."),
    ("100,1e-9,200", "."),
    ("100;1,0e-9;200", ","),
    ("100\t1e-9\t200", "."),
])
def test_dialects(text, decimal):
    result = parse_lab_as(text, "", decimal)
    assert [c[0] for c in result["columns"]] == [100, 1e-9, 200]


def test_six_columns_and_source_rows_are_kept():
    preview = parse_lab_as("# header\n100 1e-9 200 4 5 6\n200 2e-9 300 7 8 9")
    assert len(preview["columns"]) == 6
    assert preview["source_rows"] == [2, 3]
    p = project_from_preview(preview)
    assert p["sweeps"][0]["temperature_K"] is None
    assert json.loads(sst_caplab.roundtrip(json.dumps(p))) == p


@pytest.mark.parametrize("text", ["", "100 1e-9", "0 1e-9 200", "100 nan 200", "100 1e-9 0",
    "100 1e-9 200\n200 2e-9 300 4", "100 1e-9 200\ninvalid row", "nan 1e-9 200",
    "# T=100 K\n# T=200 K\n100 1e-9 200", "# T=0 K\n100 1e-9 200", "# T=-100 K\n100 1e-9 200"])
def test_bad_input_is_reported_without_dropping_rows(text):
    with pytest.raises(ValueError):
        parse_lab_as(text)


def test_header_has_precedence_and_conflict_stays_visible():
    p = parse_lab_as("# Temperature=200 K\n100 1e-9 200", "cf_100K_7.dat")
    assert p["temperature_K"] == 200
    assert p["suggested_temperature_K"] == 100
    project = project_from_preview(p, 200)
    assert project["sweeps"][0]["temperature_K"] == 200


def test_cpp_rejects_invalid_metadata():
    with pytest.raises(RuntimeError):
        project_from_preview(parse_lab_as("100 1e-9 200"), -1)


def test_cooperative_cancel_and_progress():
    updates = []
    with pytest.raises(RuntimeError, match="cancelled"):
        parse_lab_as("100 1e-9 200", cancelled=lambda: True)
    parse_lab_as("100 1e-9 200\n200 2e-9 300", progress=updates.append)
    assert updates == [0.5, 1.0]
