import os
from pathlib import Path
import subprocess


def test_friendly_native_api():
    repo = Path(__file__).resolve().parents[4]
    executable = Path(os.environ.get("PLOTLAB_EXE", repo / "build/bin/Debug-windows-x86_64/PlotLab/PlotLab.exe"))
    result = subprocess.run([str(executable), "--worksheet-script", str(Path(__file__).with_name("friendly_headless.py"))],
                            cwd=repo, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "pyws1 friendly API integration passed" in result.stdout
