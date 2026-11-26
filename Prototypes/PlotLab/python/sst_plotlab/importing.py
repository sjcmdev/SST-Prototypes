"""Import hook and preset file edges. Hook execution has the worksheet runtime's
cooperative deadline/cancellation, and runs with the user's Python permissions.
It is not a sandbox. Publication is always a separate native Core command.
Native column/post_import arrays own writable buffers; validation/naming use
read-only views with immutable native owners, safe to retain after the callback.
"""
from __future__ import annotations

import inspect
import json
import os
from pathlib import Path
import tempfile

import numpy as np

STAGES = {
    'pre_parse': 'hook(data: bytes, context, settings) -> bytes | str',
    'column': 'hook(column: {name, units, role, numeric, values}, context, settings) -> column',
    'post_import': 'hook(table: {columns, source}, context, settings) -> table',
    'validation': 'hook(table, context, settings) -> None (raise to reject)',
    'naming': 'hook(table, name: str, context, settings) -> str',
}


def run_hook(source, stage, args, context, numbers, strings, timeout_s, cancelled):
    from .runtime import Budget
    if stage not in STAGES:
        raise ValueError('Unknown import stage')
    settings = {'numbers': dict(numbers), 'strings': dict(strings)}
    with Budget(timeout_s, cancelled):
        env = {'np': np, '__name__': '__plotlab_import_hook__'}
        exec(compile(source, f'<import:{stage}>', 'exec'), env)
        hook = env.get('hook')
        if not callable(hook):
            raise ValueError(f'{stage}: define {STAGES[stage]}')
        for value in args:
            columns = value.get('columns', [value]) if isinstance(value, dict) else []
            for column in columns:
                if column.get('numeric'):
                    column['values'] = np.asarray(column['values'], dtype=float)
        arguments = (*args, dict(context), settings)
        inspect.signature(hook).bind(*arguments)
        result = hook(*arguments)
        if stage == 'pre_parse':
            if not isinstance(result, (str, bytes)):
                raise TypeError('pre_parse must return bytes or UTF-8 str')
            return result.encode('utf-8') if isinstance(result, str) else result
        if stage in ('column', 'post_import') and not isinstance(result, dict):
            raise TypeError(f'{stage} must return a typed dict')
        if stage == 'validation' and result is not None:
            raise TypeError('validation must return None or raise an error')
        if stage == 'naming' and not isinstance(result, str):
            raise TypeError('naming must return str')
        columns = result.get('columns', [result]) if isinstance(result, dict) else []
        for column in columns:
            if column.get('numeric'):
                values = np.asarray(column['values'], dtype=np.float64)
                if values.ndim != 1:
                    raise ValueError('Numeric hook arrays must be one dimensional')
                column['values'] = np.ascontiguousarray(values)
            elif isinstance(column.get('values'), np.ndarray):
                column['values'] = column['values'].tolist()
        return result


def save_presets(path, presets, ui=None, recipes=None):
    """Atomic JSON file edge; never writes source data or executes hooks."""
    path = Path(path)
    if not path.name.endswith(('.imports.json', '.plotimport.json')):
        raise ValueError('Use a .plotimport.json preset file')
    if path.name.endswith('.imports.json') and not ui and not recipes:
        raise ValueError('Project sidecars are reserved; use a .plotimport.json preset file')
    payload = {'format': 'sst.plotlab.imports', 'version': 1, 'presets': presets}
    if ui:
        payload['ui'] = ui
    if recipes:
        payload['recipes'] = recipes
    text = json.dumps(payload, ensure_ascii=False, allow_nan=False, indent=2)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'Duplicate import preset field: {key}')
        result[key] = value
    return result


def _finite_constant(value):
    raise ValueError(f'Nonfinite import preset constant: {value}')


def load_bundle(path):
    """Read definitions only; a loaded recipe requires explicit native trust."""
    with Path(path).open(encoding='utf-8') as stream:
        payload = json.load(stream, object_pairs_hook=_unique_object, parse_constant=_finite_constant)
    if not isinstance(payload, dict) or set(payload) - {'format', 'version', 'presets', 'ui', 'recipes'}:
        raise ValueError('Invalid import preset schema')
    if payload.get('format') != 'sst.plotlab.imports' or type(payload.get('version')) is not int or payload['version'] != 1:
        raise ValueError('Unsupported import preset schema')
    presets = payload['presets']
    if not isinstance(presets, dict) or (not presets and not payload.get('ui') and not payload.get('recipes')):
        raise ValueError('Expected named import presets')
    return payload


def load_presets(path):
    return load_bundle(path)['presets']
