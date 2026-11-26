import os
from pathlib import Path
import subprocess

def test_plotshould_native_vertical_slices():
    result=subprocess.run([os.environ['PLOTLAB_EXE'],'--worksheet-script',str(Path(__file__).with_name('plotshould_headless.py'))],cwd=os.environ['PLOTINT1_RUN'],capture_output=True,text=True,timeout=180)
    assert result.returncode==0,result.stdout+result.stderr
    assert 'plotshould1 native vertical slices passed' in result.stdout
