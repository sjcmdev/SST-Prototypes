import json
import os
from pathlib import Path
import subprocess

import pytest
import numpy as np

from sst_plotlab.importing import STAGES, load_presets, run_hook, save_presets


def hook(source, stage, args, *, cancelled=lambda: False):
    return run_hook(source, stage, args, {'preview': True}, {'scale': .001}, {}, .1, cancelled)


def test_five_stage_contracts_and_typed_arrays():
    assert len(STAGES) == 5
    assert hook('def hook(data, context, settings):\n return data.decode().replace("old", "new")', 'pre_parse', (b'old',)) == b'new'
    column = dict(name='I', units='mA', role=2, numeric=True, values=[2, 3])
    result = hook('def hook(column, context, settings):\n column["values"] *= settings["numbers"]["scale"]\n column["units"] = "A"\n return column', 'column', (column,))
    np.testing.assert_allclose(result['values'], [.002, .003])
    assert result['units'] == 'A'
    assert hook('def hook(table, context, settings):\n return table', 'post_import', ({'columns': [column]},))['columns']
    assert hook('def hook(table, context, settings):\n assert context["preview"]', 'validation', ({'columns': [column]},)) is None
    assert hook('def hook(table, name, context, settings):\n return name + "_derived"', 'naming', ({'columns': [column]}, 'T290')) == 'T290_derived'


@pytest.mark.parametrize('stage,args,bad', [
    ('pre_parse', (b'input',), 'return 4'),
    ('column', ({},), 'return None'),
    ('post_import', ({},), 'return []'),
    ('validation', ({},), 'return False'),
    ('naming', ({}, 'name'), 'return 4'),
])
def test_invalid_return_and_signature_are_errors(stage, args, bad):
    signature = 'table, name, context, settings' if stage == 'naming' else 'value, context, settings'
    with pytest.raises(TypeError):
        hook(f'def hook({signature}):\n {bad}', stage, args)
    with pytest.raises(TypeError):
        hook('def hook():\n pass', stage, args)


def test_cancellation_timeout_and_hook_failure():
    with pytest.raises(RuntimeError, match='Cancelled'):
        hook('def hook(value, context, settings):\n return value', 'pre_parse', (b'a',), cancelled=lambda: True)
    with pytest.raises(RuntimeError, match='time limit'):
        hook('def hook(value, context, settings):\n while True: pass', 'pre_parse', (b'a',))
    with pytest.raises(ValueError, match='bad units'):
        hook('def hook(value, context, settings):\n raise ValueError("bad units")', 'validation', ({},))


def test_presets_save_reopen_and_sources_are_untouched(tmp_path):
    source = tmp_path / 'source.csv'
    source.write_bytes(b'X,Y\n1,2\n')
    preset = {'IV': dict(name='IV', hooks={'column': {'source': 'raise RuntimeError("must not run on load")', 'numbers': {'scale': .001}}})}
    path = tmp_path / 'IV.plotimport.json'
    save_presets(path, preset)
    assert load_presets(path) == preset
    assert source.read_bytes() == b'X,Y\n1,2\n'
    with pytest.raises(ValueError):
        save_presets(source, preset)
    path.write_text('{"format":"bad","version":1,"presets":{}}')
    with pytest.raises(ValueError):
        load_presets(path)
    path.write_text('{"format":"sst.plotlab.imports","version":1,"presets":{"IV":{},"IV":{}}}')
    with pytest.raises(ValueError, match='Duplicate'):
        load_presets(path)
    path.write_text('{"format":"sst.plotlab.imports","version":1,"presets":{"IV":{"timeout_s":NaN}}}')
    with pytest.raises(ValueError, match='Nonfinite'):
        load_presets(path)
    with pytest.raises(ValueError, match='reserved'):
        save_presets(tmp_path / 'project.plotlab.imports.json', preset)


def test_native_import_public_parity():
    repo = Path(__file__).resolve().parents[4]
    executable = Path(os.environ.get('PLOTLAB_EXE', repo / 'build/bin/Release-windows-x86_64/PlotLab/PlotLab.exe'))
    cwd = Path(os.environ.get('PLOTIMPL_C_RUN', repo))
    result = subprocess.run([str(executable), '--worksheet-script', str(Path(__file__).with_name('import_headless.py'))], cwd=cwd, capture_output=True, text=True, timeout=120)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'plotimpl-c import integration passed' in result.stdout
