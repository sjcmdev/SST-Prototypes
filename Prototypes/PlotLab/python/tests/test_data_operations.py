"""Exercise the embedded nanobind session rather than a Python reimplementation."""
import os
from pathlib import Path
import subprocess


def test_embedded_data_operations():
    repo = Path(__file__).resolve().parents[4]
    executable = Path(os.environ.get("PLOTLAB_EXE", repo / "build/bin/Debug-windows-x86_64/PlotLab/PlotLab.exe"))
    script = Path(__file__).with_name("data_headless.py")
    result = subprocess.run([str(executable), "--worksheet-script", str(script)], cwd=repo,
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "P6c embedded operations passed" in result.stdout
