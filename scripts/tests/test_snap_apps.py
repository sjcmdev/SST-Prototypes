from pathlib import Path
import sys

import pytest
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from snap_apps import ROOT, embedded_environment, inspect_png, prepare_gallery_layout


def test_png_verification_rejects_solid_color_and_accepts_image(tmp_path):
    path = tmp_path / "shot.png"
    image = Image.new("RGB", (8, 8), (20, 30, 40))
    image.save(path)
    with pytest.raises(ValueError, match="Uniform screenshot"):
        inspect_png(path)
    image.putpixel((0, 0), (255, 0, 0))
    image.save(path)
    result = inspect_png(path)
    assert result["pixel_variance"] > 0
    assert result["width_px"] == result["height_px"] == 8
    assert result["size_bytes"] == path.stat().st_size


def test_gallery_layout_keeps_workspace_on_main_window(tmp_path):
    import json
    import yaml
    config = tmp_path / "install/app/config"
    (config / "layouts").mkdir(parents=True)
    (config / "ui_settings.yaml").write_text("schema_version: 1\nui:\n  font_scale: 1.5\n")
    envelope = {"state": json.dumps({"windows": [{"id": 7, "type": "caplab.workspace", "state": {"x": 9000}}]}), "workspaces": {}}
    layout = config / "layouts/imgui.ini"
    layout.write_text("[SSTWindows][Workspace]\nData=" + json.dumps(envelope))
    prepare_gallery_layout(tmp_path)
    text = layout.read_text()
    assert "Collapsed=1" in text
    result = json.loads(next(line[5:] for line in text.splitlines() if line.startswith("Data=")))
    window = json.loads(result["state"])["windows"][0]
    assert window["id"] == 7 and window["type"] == "caplab.workspace"
    assert window["state"]["x"] == 10 and window["state"]["dock"] == 0
    settings = yaml.safe_load((config / "ui_settings.yaml").read_text())
    assert settings["ui"]["font_scale"] == 1.5
    assert settings["window"]["width"] == 1280


def test_embedded_runner_exposes_shared_plotlab_package():
    import json
    paths = json.loads(embedded_environment()["SST_ROOT_PYTHON_PATHS"])
    assert str(ROOT / "Prototypes/PlotLab/python") in paths
