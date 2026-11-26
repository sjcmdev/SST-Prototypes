import os
from pathlib import Path
import subprocess

def test_real_native_import_analysis_maps_templates_and_every_export_format():
    executable=Path(os.environ['PLOTLAB_EXE'])
    result=subprocess.run([str(executable),'--worksheet-script',str(Path(__file__).with_name('workflow_headless.py'))],cwd=os.environ['PLOTINT1_RUN'],capture_output=True,text=True,timeout=180)
    assert result.returncode==0,result.stdout+result.stderr
    assert 'plotint1 native workflow passed' in result.stdout
