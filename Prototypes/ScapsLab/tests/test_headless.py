import csv
import math
from pathlib import Path
import subprocess
import tempfile
import scapslab

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
