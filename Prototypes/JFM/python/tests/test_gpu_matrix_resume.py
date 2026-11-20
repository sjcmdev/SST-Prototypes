"""Resuming a GPU gate must not accept a partial or mismatched capture."""
import importlib.util
import json
from pathlib import Path

import pytest


@pytest.mark.parametrize('invalid', [None, 'method', 'result_mismatches', 'truncated', 'partial_json'])
def test_gpu_matrix_resume_validates_capture(tmp_path, monkeypatch, invalid):
    spec = importlib.util.spec_from_file_location('gpu_matrix', Path(__file__).parents[2]/'tests/gpu/run_matrix.py')
    matrix = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(matrix)
    output = tmp_path/'result.json'
    row = dict(case='synthetic', mode='signed_sigma', method='lm', samples=2, repeats=3,
               result_mismatches=0, current_mismatches=0)
    expected = row.copy()
    if invalid == 'method':
        row['method'] = 'nm'
    if invalid == 'result_mismatches':
        row['result_mismatches'] = 1
    output.write_text('{' if invalid == 'partial_json' else json.dumps(row))
    Path(str(output)+'.chi2.bin').write_bytes(bytes(8 if invalid == 'truncated' else 16))
    calls = []
    def run(command, check):
        calls.append(command)
        output.write_text(json.dumps(expected))
    monkeypatch.setattr(matrix.subprocess, 'run', run)
    assert matrix.run_case(['gpu'], output, True, 'synthetic', 'signed_sigma', 'lm', 2, 3) == expected
    assert calls == ([] if invalid is None else [['gpu']])
