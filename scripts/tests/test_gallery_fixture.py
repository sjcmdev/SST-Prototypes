"""Runs inside CapLabTests' embedded Python to build the gallery project."""
import json
import os
from pathlib import Path

import pytest


@pytest.mark.skipif(not os.environ.get("SST_GALLERY_PROJECT"), reason="Gallery fixture preparation only")
def test_build_gallery_project():
    from caplab import parse_lab_as, project_from_preview
    root = Path(__file__).resolve().parents[2]
    fixture = root / "Prototypes/CapLab/tests/fixtures/cf_100K_7.dat"
    project = project_from_preview(parse_lab_as(fixture.read_text(), str(fixture)), 100)
    assert project["sweeps"][0]["temperature_K"] == 100
    Path(os.environ["SST_GALLERY_PROJECT"]).write_text(json.dumps(project, indent=2), encoding="utf-8")
