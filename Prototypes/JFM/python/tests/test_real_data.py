import pytest


def test_real_data_override_and_missing_file(monkeypatch, tmp_path, real_data_file):
    monkeypatch.setenv("SST_JFM_REAL_DATA", str(tmp_path))
    path = tmp_path / "dat" / "measurement.dat"
    with pytest.raises(pytest.skip.Exception, match="SST_JFM_REAL_DATA"):
        real_data_file("dat/measurement.dat")
    path.parent.mkdir()
    path.write_text("invalid measurement", encoding="utf-8")
    assert real_data_file("dat/measurement.dat") == path


def test_real_data_default(monkeypatch, real_data_file):
    monkeypatch.delenv("SST_JFM_REAL_DATA", raising=False)
    with pytest.raises(pytest.skip.Exception, match="real-measurement"):
        real_data_file("missing-integcheck1-fixture.dat")
