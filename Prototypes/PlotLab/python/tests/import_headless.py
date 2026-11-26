"""Executed inside PlotLab's real nanobind host, not a Python mock."""
import copy
import json
import os
from pathlib import Path
import tempfile
import hashlib
import io
import numpy as np

from sst_plotlab import WorksheetSession
from sst_plotlab.importing import load_bundle, save_presets

session = WorksheetSession()
root = Path(tempfile.mkdtemp(prefix='plotimpl-c-headless-'))
assert not session.import_preview([root / 'missing.csv'], {'mapping': [{'source': 'X', 'unit': 'V'}]})['ok']
recipe = dict(name='IV', version='1', delimiter=';', decimal=',', header=1, units_row=1,
              mapping=[dict(source='Voltage', name='V', role=1, units='V'), dict(source='Current', name='I', role=2, units='A'),
                       dict(source='Error', name='sigma', role=4, units='A', required=False)],
              hooks={
                  'pre_parse': dict(source='def hook(data, context, settings):\n assert isinstance(data, bytes)\n return data'),
                  'column': dict(source='def hook(column, context, settings):\n if column["name"] == "I":\n  column["values"] *= settings["numbers"]["scale"]\n  column["units"] = "A"\n return column', numbers={'scale': .001}),
                  'post_import': dict(source='def hook(table, context, settings):\n assert table["columns"][1]["units"] == "A"\n return table'),
                  'validation': dict(source='def hook(table, context, settings):\n import builtins\n values = table["columns"][0]["values"]\n assert not values.flags.writeable\n builtins._plotimpl_c_view = values\n assert table["columns"][0]["name"] == "V"'),
                  'naming': dict(source='def hook(table, name, context, settings):\n import builtins\n values = table["columns"][0]["values"]\n assert not values.flags.writeable\n assert np.shares_memory(values, builtins._plotimpl_c_view)\n return name + "_derived"')})
paths = [root / 'T290.csv', root / 'power100.csv', root / 'invalid.csv']
for path in paths[:2]:
    path.write_text('Voltage;Current\n[V];[mA]\n0,1;2,3\n0,2;--\n', encoding='utf-8')
paths[2].write_text('Frequency;Capacitance\n[Hz];[F]\n1;2\n')
original = paths[0].read_bytes()
preview = session.import_preview(paths, recipe)
assert preview['ok'] and preview['items'][0]['diagnostics'][0]['code'] == 'trust', preview
session.trust_import_recipe(recipe)
preview = session.import_preview(paths, recipe)
assert preview['items'][0]['ready'], preview
assert len(session.books) == 0
assert preview['items'][0]['executed_hooks'] == ['pre_parse', 'column', 'post_import', 'validation', 'naming']
assert preview['items'][0]['transformed']['columns'][1]['values'] == [.0023, None], preview
json.dumps(preview, allow_nan=False)
committed = session.import_commit()
assert committed['items'][0]['worksheet_id'] and committed['items'][1]['worksheet_id']
assert not committed['items'][2]['worksheet_id']
assert len(session.books) == 2
assert session._session.undo_depth == 2
first_id = committed['items'][0]['worksheet_id']
sheet = session.project.by_id(first_id)
assert sheet.columns[1][0] == .0023
session._session.undo()
assert len(session.books) == 1
session._session.redo()
assert len(session.books) == 2
session.import_preview(paths, recipe)
retry = session.import_commit()
assert retry['items'][0]['worksheet_id'] == first_id and retry['items'][0]['reused']
assert session._session.undo_depth == 2
changed = copy.deepcopy(recipe)
changed['hooks']['column']['numbers']['scale'] = .01
untrusted = session.import_preview(paths, changed)
assert untrusted['items'][0]['diagnostics'][0]['code'] == 'trust'
session.trust_import_recipe(changed)
collision = session.import_preview(paths, changed)
assert collision['items'][0]['diagnostics'][0]['code'] == 'collision', collision
preset_file = root / 'IV.plotimport.json'
session.save_import_presets(preset_file, {'IV': recipe})
other = WorksheetSession()
loaded = other.load_import_presets(preset_file)
assert loaded['IV']['hooks']['column']['numbers']['scale'] == .001
untrusted = other.import_preview(paths, loaded['IV'])
assert untrusted['items'][0]['diagnostics'][0]['code'] == 'trust'
assert paths[0].read_bytes() == original
history_before = session.import_recipes
assert len(history_before) == 1
original_fingerprint = next(iter(history_before))
assert history_before[original_fingerprint]['hooks']['column']['numbers']['scale'] == .001
session.save_import_presets(root / 'changed.plotimport.json', {'IV': changed})
assert session.import_recipes == history_before
project_file = root / 'session.plotlab'
session._session.save(str(project_file))
sidecar = Path(str(project_file) + '.imports.json')
bundle = load_bundle(sidecar)
assert bundle['presets']['IV']['hooks']['column']['numbers']['scale'] == .01
assert bundle['recipes'][original_fingerprint]['hooks']['column']['numbers']['scale'] == .001
draft = copy.deepcopy(bundle['presets']['IV'])
draft['hooks']['pre_parse']['source'] = 'raise RuntimeError("must not execute on reopen")'
ui = dict(recipe=draft, paths=[str(root / 'source.dat')], open=True)
save_presets(sidecar, bundle['presets'], ui, bundle['recipes'])
reopened = WorksheetSession()
reopened._session.open(str(project_file))
assert len(reopened.books) == 2
assert reopened.import_recipes == history_before
assert reopened.import_status()['state'] == 'idle'
saved_again = root / 'reopened.plotlab'
reopened._session.save(str(saved_again))
assert load_bundle(Path(str(saved_again) + '.imports.json'))['ui'] == ui
sentinel = reopened.sheets.new('Keep existing')
undo_before_bad_open = reopened._session.undo_depth
bad_project = root / 'invalid-sidecar.plotlab'
bad_project.write_bytes(project_file.read_bytes())
Path(str(bad_project) + '.imports.json').write_text('{"format":"invalid"}')
try:
    reopened._session.open(str(bad_project))
except Exception:
    pass
else:
    raise AssertionError('Invalid preset sidecar must reject Open')
assert reopened.project.by_id(sentinel.id).name == 'Keep existing'
assert reopened._session.undo_depth == undo_before_bad_open
tampered = copy.deepcopy(bundle)
tampered['recipes'][original_fingerprint]['hooks']['column']['numbers']['scale'] = 99
Path(str(bad_project) + '.imports.json').write_text(json.dumps(tampered, allow_nan=False))
try:
    reopened._session.open(str(bad_project))
except Exception:
    pass
else:
    raise AssertionError('Mismatched recipe fingerprint must reject Open')
assert reopened.project.by_id(sentinel.id).name == 'Keep existing'
assert reopened._session.undo_depth == undo_before_bad_open
assert reopened.import_preview(paths, recipe)['items'][0]['diagnostics'][0]['code'] == 'trust'
script_input = root / 'script.csv'
script_input.write_text('X,Y\n1,2\n3,4\n')
script_recipe = dict(name='Script import', mapping=[dict(source='X', name='X', role=1), dict(source='Y', name='Y', role=2)])
script = f'''from sst_plotlab import WorksheetSession
active = WorksheetSession(active=True)
r = {script_recipe!r}
p = active.import_preview([{str(script_input)!r}], r)
assert p['items'][0]['ready'], p
assert active.import_commit()['items'][0]['worksheet_id']
'''
script_result = session.run(script, timeout_s=10)
assert script_result.ok, script_result
script_project = root / 'script-import.plotlab'
session._session.save(str(script_project))
assert 'Script import' in load_bundle(Path(str(script_project) + '.imports.json'))['presets']
assert any(recipe['name'] == 'Script import' for recipe in session.import_recipes.values())
nonrepeatable_file = root / 'nonrepeatable.csv'
nonrepeatable_file.write_text('X,Y\n1,2\n')
nonrepeatable = dict(script_recipe, name='Nonrepeatable', hooks={'column': dict(source='''def hook(column, context, settings):
 import builtins
 if column['name'] == 'Y':
  builtins._plotimpl_c_counter = getattr(builtins, '_plotimpl_c_counter', 0) + 1
  column['values'] += builtins._plotimpl_c_counter
 return column
''')})
counter_session = WorksheetSession()
counter_session.trust_import_recipe(nonrepeatable)
assert counter_session.import_preview([nonrepeatable_file], nonrepeatable)['items'][0]['ready']
counter_result = counter_session.import_commit()
counter_sheet = counter_session.project.by_id(counter_result['items'][0]['worksheet_id'])
accepted_value = counter_sheet.columns[1][0]
changed_preview = counter_session.import_preview([nonrepeatable_file], nonrepeatable)
assert changed_preview['items'][0]['diagnostics'][0]['code'] == 'collision', changed_preview
assert counter_session.import_commit()['items'][0]['worksheet_id'] == 0
assert counter_sheet.columns[1][0] == accepted_value
empty_recipe = dict(script_recipe, name='Empty output', hooks={'column': dict(source='''def hook(column, context, settings):
 if column['name'] == 'Y': column['values'] = []
 return column
''')})
counter_session.trust_import_recipe(empty_recipe)
empty_plan = counter_session.import_preview([nonrepeatable_file], empty_recipe)
assert not empty_plan['items'][0]['ready'] and empty_plan['items'][0]['diagnostics'][0]['code'] == 'schema', empty_plan
assert counter_session.import_commit()['items'][0]['worksheet_id'] == 0
assert counter_sheet.columns[1][0] == accepted_value
dlcp_file, as_file = root / 'DLCP.csv', root / 'AS.csv'
dlcp_file.write_text('AC amplitude;Capacitance\n[V];[F]\n0.01;2e-9\n')
as_file.write_text('Frequency;Capacitance\n[Hz];[F]\n1000;2e-9\n')
dlcp_recipe = dict(name='DLCP schema', delimiter=';', decimal='.', header=1, units_row=1,
                   mapping=[dict(source='AC amplitude', name='V_ac', units='V', role=1),
                            dict(source='Capacitance', name='C', units='F', role=2)])
semantic = WorksheetSession()
semantic_plan = semantic.import_preview([dlcp_file, as_file], dlcp_recipe)
assert semantic_plan['items'][0]['ready'] and not semantic_plan['items'][1]['ready'], semantic_plan
assert semantic_plan['items'][1]['diagnostics'][0]['code'] == 'schema'
semantic_result = semantic.import_commit()
assert semantic_result['items'][0]['worksheet_id'] and semantic_result['items'][1]['worksheet_id'] == 0
assert len(semantic.books) == 1
if os.environ.get('PLOTIMPL_C_RUN'):
    evidence = Path(os.environ['PLOTIMPL_C_RUN'])
    fixtures = evidence / 'fixtures'
    fingerprints = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.iterdir() if p.is_file()}
    raw_recipe = dict(name='Raw DAT', delimiter='\t', decimal=',', header=2, units_row=2,
                      mapping=[dict(source='C1', name='RawX', role=1), dict(source='C2', name='RawY', role=2),
                               dict(source='C3', name='RawExtra', role=0, required=False)])
    raw = WorksheetSession()
    raw_inputs = [fixtures / 'R072-two.dat', fixtures / 'R072-three.dat', fixtures / 'C003-comma.dat']
    plan = raw.import_preview(raw_inputs, raw_recipe)
    assert all(item['ready'] for item in plan['items']), plan
    result = raw.import_commit()
    assert all(item['worksheet_id'] for item in result['items']), result
    for source, preview_item, item in zip(raw_inputs, plan['items'], result['items']):
        sheet = raw.project.by_id(item['worksheet_id'])
        assert list(sheet.columns[0][:5]) == preview_item['transformed']['columns'][0]['values']
        expected = np.loadtxt(io.StringIO(source.read_text().replace(',', '.')), delimiter='\t')
        for column in range(expected.shape[1]):
            np.testing.assert_array_equal(sheet.columns[column], expected[:, column])
    for line_count, original_name in [(2, 'R072-two.dat'), (17, 'C003-comma.dat')]:
        path = root / f'preamble{line_count}.dat'
        path.write_bytes(b'instrument metadata\n' * line_count + (fixtures / original_name).read_bytes())
        configured = dict(raw_recipe, preamble_lines=line_count)
        plan = raw.import_preview([path], configured)
        assert plan['items'][0]['ready'], plan
        assert raw.import_commit()['items'][0]['worksheet_id']
    iv_recipe = dict(name='IV headers', delimiter='\t', decimal='.', header=1, units_row=1,
                     mapping=[dict(source='U', name='Voltage', units='V', role=1),
                              dict(source='I', name='Current', units='A', role=2),
                              dict(source='j', name='Density', units='mA/cm2', role=0, required=False)])
    mixed = raw.import_preview([fixtures / 'C002-header.dat', fixtures / 'AS.dat'], iv_recipe)
    assert mixed['items'][0]['ready'] and not mixed['items'][1]['ready'], mixed
    committed = raw.import_commit()
    assert committed['items'][0]['worksheet_id'] and not committed['items'][1]['worksheet_id']
    assert {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.iterdir() if p.is_file()} == fingerprints
    (evidence / 'tmpl2-import-evidence.json').write_text(json.dumps(dict(fixture_sha256=fingerprints, status='passed',
        cases=['decimal comma / tab', '2 to 3 optional columns', '2-line preamble', '17-line preamble', 'IV vs AS schema mismatch', 'preview equals published full numeric columns']), indent=2))
print('plotimpl-c import integration passed', flush=True)
