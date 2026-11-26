import os
from pathlib import Path
import subprocess


def test_native_should_vertical_slices():
    result = subprocess.run([os.environ["PLOTLAB_EXE"], "--worksheet-script",
                             str(Path(__file__).with_name("should_headless.py"))],
                            cwd=os.environ["PLOTINT1_RUN"], capture_output=True,
                            text=True, timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "plotshould2 native acceptance passed:" in result.stdout
