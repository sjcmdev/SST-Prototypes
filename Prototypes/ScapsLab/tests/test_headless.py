import csv
import math
from pathlib import Path
import subprocess
import tempfile
import scapslab

# S4: public wrappers use the same typed command/job path as GUI Actions.
def check_run_actions(document):
    import copy
    draft = copy.deepcopy(document)
    draft['temperature_K'] = 300
    draft['mesh']['points_per_layer'] = 11
    for layer in draft['layers']:
        layer['defects'] = []
    draft['interface_defects'] = []
    run = scapslab.Session(draft)
    def samples(values):
        return dict(sampling='list', values=values)
    actions = dict(band=dict(include=True, voltage_V=0.012),
                   iv=dict(include=True, range=samples([0, 0.012, 0.024])),
                   cv=dict(include=True, range=samples([0, 0.021]), frequency_Hz=1234, temperature_K=315),
                   cf=dict(include=True, range=samples([100, 2000, 30000]), voltage_V=0.017),
                   qe=dict(include=True, range=samples([400, 550, 700])))
    assert run.run_settings(actions)['ok']
    single = run.single_shot()
    assert single['ok'], single
    assert len(single['project']['devices'][0]['history']) == 5
    columns = [dict(id='J0', quantity='J', voltage_V=0)]
    spec = dict(mode='linked', axes=[dict(parameter_id='temperature_K', values=[300])], include_curves=True)
    batch = run.run_batch(spec=spec, columns=columns, actions=actions)
    assert batch['ok'], batch
    table = batch['batch_result']
    member = table['action_members'][0]
    assert len(member['actions']) == 5
    for actual in member['actions']:
        kind = actual['kind']
        expected = single['band_result'] if kind == 'band' else single['result'] if kind == 'iv' else single['ac_results'][kind]
        fields = ('voltage_V', 'J_A_m2', 'frequency_Hz', 'capacitance_F', 'conductance_S', 'wavelength_nm', 'EQE', 'status')
        assert [{k: p[k] for k in fields if k in p} for p in actual['result']['points']] == [{k: p[k] for k in fields if k in p} for p in expected['points']], kind
    root = batch['selected_run']
    assert run.results_select(root, action='cv')['ok']
    assert not run.results_select(root, member_index=99)['ok']
    extracted = run.record_batch(columns + [dict(id='C0', quantity='C', voltage_V=0, frequency_Hz=777)])
    assert extracted['ok'] and extracted['batch_result']['rows'][0]['statuses'].get('C0') == 'not sampled', (extracted.get('status'), extracted['batch_result']['columns'], extracted['batch_result']['rows'][0]['statuses'])
    calculated = run.calculate_missing(columns + [dict(id='C0', quantity='C', voltage_V=0, frequency_Hz=777)])
    assert calculated['ok'] and calculated['batch_result']['rows'][0]['statuses']['C0'] == 'ok', calculated
    actions['iv']['pause_after_action'] = True
    paused = run.single_shot(actions)
    assert paused['ok'] and paused['paused'], paused
    while run.poll().get('paused'):
        resumed = run.resume_actions()
        assert resumed['ok'], resumed
    assert not run.poll()['running']
    assert not run.run_settings(dict(iv=dict(range=dict(sampling='step', minimum=0, maximum=1, step=0))))['ok']
    print('slux3d Python Actions: Single/Batch exact parity, selection, Extract/Calculate missing and Pause/Resume PASS', flush=True)

session = scapslab.Session()
loaded = session.execute({'operation': 'load', 'path': project_path})
assert loaded['ok'], loaded
ran = session.execute({'operation': 'run'})
assert ran['ok'], ran
points = ran['result']['points']
assert points and all('bias_id' in p for p in points)
assert any(p['J_A_m2'] is not None for p in points)
headless = Path(headless_path)
with tempfile.TemporaryDirectory(prefix='scapslab-headless-') as output:
    subprocess.run([str(headless), '--project', project_path, '--output', output], check=True)
    with open(Path(output) / 'iv.csv') as f:
        reference = list(csv.DictReader(f))
    assert len(reference) == len(points)
    maximum_relative_error = 0.0
    for i, (actual, expected) in enumerate(zip(points, reference)):
        assert actual['voltage_V'] == float(expected['voltage_V'])
        current = float(expected['current_density_A_m2'])
        if math.isnan(current):
            assert actual['J_A_m2'] is None
        else:
            error = abs(actual['J_A_m2'] - current)
            assert error <= abs(current) * 1e-10, (i, actual, expected)
            maximum_relative_error = max(maximum_relative_error, error / abs(current) if current else 0)
        with open(Path(output) / f'profiles/profile_{i:03}.csv') as f:
            mesh = [float(row['x_m']) for row in csv.DictReader(f)]
        assert mesh == actual['x_m']
    saved = str(Path(output) / 'roundtrip.yaml')
    assert session.execute({'operation': 'save', 'path': saved})['ok']
    assert session.execute({'operation': 'load', 'path': saved})['document'] == loaded['document']
    print(f'Python command run + scaps_headless CSV: {len(points)} points, max relative J error {maximum_relative_error:g}, mesh identical', flush=True)
assert not session.execute({'operation':'edit','patch':{'temperature_K':-1}})['ok']
assert not session.execute({'operation':'unknown'})['ok']
check_run_actions(loaded['document'])


for kind in ('cv', 'cf'):
    response = session.execute(dict(operation=kind, start_V=-0.1, stop_V=0.1, step_V=0.1,
        frequency_Hz=1e4, voltage_V=0.0, start_Hz=10.0, stop_Hz=1e6, points=9, temperature_K=300.0))
    assert response['ok'], response
    ac = response['ac_results'][kind]
    assert ac['points'] and all(p['point_id'].startswith(kind + '-') for p in ac['points'])
    assert all(p['capacitance_F'] is not None for p in ac['points']), ac
    with tempfile.TemporaryDirectory(prefix='scapslab-ac-') as output:
        path = str(Path(output) / 'ac.csv')
        assert session.execute(dict(operation='export_ac', kind=kind, path=path))['ok']
        with open(path) as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == len(ac['points'])
        for row, point in zip(rows, ac['points']):
            assert float(row['capacitance_F']) == point['capacitance_F']
            assert float(row['conductance_S']) == point['conductance_S']
print('Python AC commands + CSV: C-V and C-f verified', flush=True)

assert session.execute(dict(operation='auto_recalculate', enabled=False, band_enabled=False))['ok']
original = session.execute(dict(operation='inspect'))['document']
for temperature in (301, 302):
    assert session.execute(dict(operation='edit', patch={'temperature_K': temperature}, gesture_id='python-drag'))['ok']
assert session.execute(dict(operation='undo'))['document'] == original
assert session.execute(dict(operation='redo'))['document']['temperature_K'] == 302
band = session.execute(dict(operation='band', voltage_V=0.1))
assert band['ok'] and len(band['band_result']['points']) == 1, band
assert band['band_result']['points'][0]['voltage_V'] == 0.1
assert band['band_result']['compute_time_ms'] > 0
print('Python undo merge + redo + band calculation verified', flush=True)

# Editor parity: the same JSON edit used by the panels, including stable object IDs.
import copy
editor = scapslab.Session(copy.deepcopy(loaded['document']))
defect = dict(scapslab_id='python-defect', label='Gaussian', energy_reference='relative_to_Ev',
              energy_offset_eV=0.5, energy_distribution='gaussian', characteristic_energy_eV=0.05,
              Nt_m3=1e16, sigma_n_m2=1e-19, sigma_p_m2=1e-19, charge_type='neutral')
document = copy.deepcopy(loaded['document'])
document['layers'][0]['defects'] = [defect]
second = copy.deepcopy(document['layers'][0])
second.update(scapslab_id='python-layer-2', name='Second layer', thickness_m=2e-7, defects=[])
document['layers'].append(second)
document['interface_defects'] = [dict(id='python-interface', left_layer_index=0, energy_reference=7,
    energy_offset_eV=0.5, Nt_m2=1e10, energy_distribution='gaussian', density_convention='integrated_sheet',
    characteristic_energy_eV=0.05, sigma_n_left_m2=1e-19, sigma_n_right_m2=1e-19,
    sigma_p_left_m2=1e-19, sigma_p_right_m2=1e-19, charge_type='neutral')]
document['external'] = dict(area_m2=1e-4, series_ohm_m2=1e-5, shunt_ohm_m2=1.0)
assert editor.execute(dict(operation='edit', document=document))['ok']
preview = editor.execute(dict(operation='band', voltage_V=0.05))
assert preview['ok'], preview
assert len(preview['band_result']['points'][0]['defect_previews']) == 2
assert 'validity_status' in preview['band_result']
with tempfile.TemporaryDirectory(prefix='scapslab-editors-') as output:
    path = str(Path(output) / 'editors.yaml')
    assert editor.execute(dict(operation='save', path=path))['ok']
    assert editor.execute(dict(operation='load', path=path))['document'] == document
assert editor.execute(dict(operation='undo'))['ok']
print('Python defect/interface/resistance/thickness edit and YAML round-trip: passed', flush=True)

workspace = scapslab.Session(loaded['document'])
assert workspace.device_list()['ok']
first = workspace.device_list()['active_device']
second_response = workspace.device_duplicate(first)
assert second_response['ok'], second_response
second = second_response['active_device']
assert first != second
assert workspace.execute(dict(operation='edit', patch={'temperature_K': 301}))['ok']
assert workspace.device_select(first)['document']['temperature_K'] == loaded['document']['temperature_K']
assert workspace.run_all()['ok']
with tempfile.TemporaryDirectory(prefix='scapslab-multi-') as output:
    target = str(Path(output) / 'project.scapslab')
    assert workspace.project_save(target)['ok']
    reloaded = workspace.project_load(target)
    assert reloaded['ok'] and len(reloaded['devices']) == 2
    assert workspace.device_remove(second)['ok']
    assert len(workspace.device_list()['devices']) == 1
    assert workspace.execute(dict(operation='undo'))['ok']
    assert len(workspace.device_list()['devices']) == 2
print('Python multi-device project load/save/duplicate/select/remove/run_all verified', flush=True)

fresh = scapslab.Session()
assert fresh.device_list()['ok']
assert fresh.device_add(name='Template device')['ok']
assert len(fresh.device_list()['devices']) == 2

# The same request was submitted by clicking Run batch in the real ImGui panel.
batch_session = scapslab.Session(ui_document)
python_batch = batch_session.run_batch(spec=ui_spec, actions=ui_batch['run_plan'])
assert python_batch['ok'], python_batch
for actual, expected in zip(python_batch['batch_result']['rows'], ui_batch['rows'], strict=True):
    for key in ('id', 'parameters', 'status', 'error_type', 'project_hash', 'curves', 'values', 'statuses'):
        assert actual.get(key) == expected.get(key), key
assert len(python_batch['batch_result']['rows']) == 3
assert len(python_batch['history']) == 1
for actual, expected in zip(python_batch['batch_result']['action_members'], ui_batch['action_members'], strict=True):
    for action, reference_action in zip(actual['actions'], expected['actions'], strict=True):
        assert action['kind'] == reference_action['kind']
        assert action['result']['points'] == reference_action['result']['points']
assert batch_session.record_batch([dict(id='Voc', quantity='Voc')])['batch_result']['rows'][0]['statuses']['Voc'] == 'not_illuminated'
with tempfile.TemporaryDirectory(prefix='scapslab-batch-') as folder:
    path = Path(folder) / 'recorder.csv'
    exported = batch_session.export_batch(path)
    assert exported['ok'], exported.get('error')
    rows = list(csv.DictReader(path.open()))
    assert len(rows) == 3 and 'Voc status' in rows[0]
    assert batch_session.export_batch(path, 'point_1')['ok']
    assert len(list(csv.DictReader(path.open()))) == 1
    assert batch_session.send_to_plotlab(Path(folder) / 'recorder.plotlab')['ok']
print('UI Run batch == Python run_batch: parameters, status, curves and Recorder bitwise equal', flush=True)

# GUI button and Python use the identical illuminated command/job path.
light_session = scapslab.Session(ui_light['document'])
light = light_session.execute({'operation': 'run'})
assert light['ok'], light
assert light['result']['points'] == ui_light['result']['points']
assert light['result']['photovoltaic'] == ui_light['result']['photovoltaic']
assert light['result']['optics'] == ui_light['result']['optics']
for p in light['result']['points']:
    if p['valid'] and abs(p['J_A_m2']) > 1e-8:
        gr = p['generation_recombination']
        assert len(gr['contact_recombination']) == 2
        assert gr['terminal_budget_valid'], gr
        assert gr['terminal_budget_relative_error'] <= 1e-6
for mode in ('constant_flux', 'constant_power'):
    qe = light_session.execute(dict(operation='qe', start_nm=590., stop_nm=610., points=3,
                                    probe_mode=mode, probe_photon_flux_m2_s=1e16, probe_power_W_m2=1e-3))
    assert qe['ok'], qe
    assert qe['ac_results']['qe']['probe_mode'] == mode
    assert all(p['EQE'] is not None and p['internal_voltage_V'] is not None and p['terminal_voltage_V'] == 0. for p in qe['ac_results']['qe']['points'])
    with tempfile.TemporaryDirectory(prefix='scapslab-qe-') as folder:
        path = Path(folder) / 'qe.csv'
        runs = light_session.result_runs()['runs']
        selected = [r['run_id'] for r in runs if r['kind'] == 'qe'][-1:]
        assert light_session.export_results(path, run_ids=selected)['ok']
        with path.open(newline='', encoding='utf-8') as stream:
            rows = list(csv.DictReader(stream))
        assert [float(r['internal_voltage_V']) for r in rows] == [p['internal_voltage_V'] for p in qe['ac_results']['qe']['points']]

print('Light I-V GUI/Python: bitwise points, PV and optics; both QE probe modes PASS', flush=True)

am15 = light_session.execute(dict(operation='edit', patch={'illumination': {'scapslab_spectrum': 'AM1.5G'}}))
assert am15['ok'], am15
assert am15['document']['illumination']['mode'] == 'spectrum'
assert len(am15['document']['illumination']['spectrum_nm_W_m2']) > 100
preview = light_session.execute(dict(operation='band', voltage_V=0.))
assert preview['ok'], preview
assert preview['band_result']['optics']['incident_power_W_m2'] > 900.

zero = light_session.execute(dict(operation='edit', patch={'illumination': {'intensity_multiplier': 0.}}))
assert zero['ok'], zero
zero = light_session.execute(dict(operation='run'))
assert zero['ok'], zero
assert zero['result']['photovoltaic']['efficiency_fraction'] is None

# File menu operations also run through the embedded command/job API.
def_session = scapslab.Session()
# The package path is supplied by the C++ host and is independent of the engine checkout.
fixture = Path(scapslab.__file__).resolve().parents[2] / 'tests' / 'stage05-gaussian.def'
imported = def_session.import_def(fixture)
assert imported['ok'], imported
assert 'warnings' in imported['operation_result']
with tempfile.TemporaryDirectory(prefix='scapslab-def-') as folder:
    exported = def_session.export_def(Path(folder) / 'roundtrip.def')
    assert exported['ok'], exported
    assert (Path(folder) / 'roundtrip.def').stat().st_size > 0
    assert def_session.import_def(Path(folder) / 'roundtrip.def')['ok']
print('Python .def import/export and unsupported-items warnings PASS', flush=True)

# G/R uses the same command/job path and immutable working-point snapshot.
with tempfile.TemporaryDirectory(prefix='scapslab-gr-') as output:
    result = session.execute({'operation': 'run'})
    assert result['ok'], result
    for format in ('json', 'csv'):
        path = Path(output) / ('gr.' + format)
        exported = session.execute({'operation': 'export_gr', 'format': format, 'path': str(path), 'point_index': 0})
        assert exported['ok'], exported
        assert path.stat().st_size > 0
        if format == 'csv':
            with path.open(newline='') as f:
                rows = list(csv.DictReader(f))
            assert rows and all(None not in row for row in rows)
            assert all(row['terminal_budget_valid'] in ('true', 'false') for row in rows)
            assert all('terminal_budget_relative_error' in row for row in rows)
            assert all(row['bias_id'] == 'bias-0' for row in rows)

# slio: public aliases use the same native command -> job path as the GUI.
# Lossless profile/curve JSON fields can exceed Python csv's default 128 KiB field limit.
csv.field_size_limit(64 * 1024 * 1024)
with tempfile.TemporaryDirectory(prefix='scapslab-io-') as output:
    io_session = scapslab.Session(loaded['document'])
    assert io_session.execute(dict(operation='run'))['ok']
    before = io_session.execute(dict(operation='inspect'))
    path = Path(output) / 'project.scapslab'
    saved = io_session.save_project(path)
    assert saved['ok'] and saved['operation_result']['size_bytes'] == path.stat().st_size, saved
    reloaded = io_session.load_project(path)
    assert reloaded['ok'] and reloaded['document'] == before['document'], reloaded
    runs = io_session.result_runs()['runs']
    selected = [runs[0]['run_id']]
    for format in ('json', 'csv', 'png', 'svg', 'pdf'):
        destination = Path(output) / f'results.{format}'
        exported = io_session.export_results(destination, format=format, run_ids=selected)
        assert exported['ok'], exported
        assert destination.stat().st_size > 100
        if format == 'csv':
            with destination.open(newline='', encoding='utf-8') as stream:
                rows = list(csv.DictReader(stream))
            expected = before['result']['points']
            assert [json.loads(r['point_json']) for r in rows] == expected
            for row, point in zip(rows, expected):
                assert float(row['terminal_voltage_V']) == point['voltage_V']
                assert float(row['internal_voltage_V']) == point['internal_voltage_V']
                assert json.loads(row['settings_json']) == before['document']
                assert json.loads(row['provenance_json']) == before['result']['provenance']
    after = io_session.execute(dict(operation='inspect'))
    assert after['document'] == before['document'] and after['result'] == before['result']
    assert not io_session.export_results(Path(output), all=True)['ok']
    assert io_session.execute(dict(operation='inspect'))['result'] == before['result']
    print('slio Python load/save/export: CSV exact reread, JSON, PNG/SVG/PDF, failure isolation PASS', flush=True)
    batch_run = io_session.run_batch(spec=ui_spec)
    assert batch_run['ok'], batch_run
    for format in ('csv', 'json', 'svg'):
        destination = Path(output) / f'all-runs.{format}'
        exported = io_session.export_results(destination, format=format, all=True)
        assert exported['ok'], exported
        assert len(exported['operation_result']['files']) >= 1
        if format == 'csv':
            with destination.open(newline='', encoding='utf-8') as stream:
                rows = list(csv.DictReader(stream))
            batch_rows = [r for r in rows if r['kind'] == 'batch']
            assert [json.loads(r['point_json']) for r in batch_rows] == batch_run['batch_result']['rows']
            assert all(json.loads(r['batch_spec_json']) == batch_run['batch_result']['spec'] for r in batch_rows)
    print('slio selected/all: mixed I-V + batch/Recorder one-sheet CSV/JSON and retained batch plots PASS', flush=True)

    history = io_session.history_list()
    assert history['ok'], history
    runs = history['project']['devices'][0]['history']
    assert runs and all('model_hash' in run and 'diff' in run for run in runs)
    run_id = runs[0]['run_id']
    assert io_session.history_update(run_id, pinned=True, name='Python reference')['ok']
    assert io_session.history_clear()['ok']
    remaining = io_session.history_list()['project']['devices'][0]['history']
    assert len(remaining) == 1 and remaining[0]['pinned']
    assert io_session.history_list()['result']['points'] == before['result']['points']
    assert io_session.execute(dict(operation='undo'))['ok']
    assert io_session.quick_sweep('temperature_K', 5., points=2, voltages_V=[0.])['ok']
    assert io_session.history_clear(scope='all', include_pinned=True)['ok']
    assert not io_session.history_list()['project']['devices'][0]['history']
    print('slhist1 Python history, Keep/Pin, clear/Undo and batch sweep PASS', flush=True)

    tutorial_session = scapslab.Session()
    assert len(tutorial_session.tutorials()['tutorials']) == 17
    assert not tutorial_session.tutorial_start('../unknown')['ok']
    assert tutorial_session.tutorial_start('first-device')['tutorial']['status'] == 'running'
    assert tutorial_session.tutorial_state()['tutorial']['mode'] == 'learn'
    assert tutorial_session.tutorial_stop()['tutorial']['status'] == 'stopped'
    report = tutorial_session.tutorial_start('first-device', mode='verify')
    assert report['ok'], report
    assert all(step['status'] == 'PASS' for step in report['tutorial']['steps'])
    print('sltut1 embedded Python tutorial catalog, learn/stop and verify PASS', flush=True)

profile_session = scapslab.Session()
profile_doc = profile_session.execute({'operation':'inspect'})['document']
layer_id = profile_doc['layers'][0]['scapslab_id']
profile_file = Path(examples_path) / 'profiles' / 'doping.grd'
request = {'operation':'load_profile','layer_id':layer_id,'parameter_id':'Na_m3','path':str(profile_file)}
assert profile_session.execute(request)['ok']
preview = profile_session.execute(dict(request, operation='preview_profile'))
assert preview['ok'], preview
assert len(preview['operation_result']['x_m']) == 201
assert preview['operation_result']['value_SI'][0] == 1e20
assert profile_session.execute({'operation':'undo'})['document'] == profile_doc
assert profile_session.execute({'operation':'redo'})['ok']
report = profile_session.tutorial_start('load-grading-profile', mode='verify')
assert report['ok'], report
print('slprof1 Python command/job profiles, preview, Undo/Redo and tutorial verify PASS', flush=True)
