"""claccept1: integrated owner workflow through the embedded execute surface."""
import json
import os
from pathlib import Path
import pytest

from caplab import execute
from test_compare_browser import children
from test_m8_workflow import FIX, pipeline, run
from test_m6_fit import fixture


def test_two_projects_folder_science_maps_and_circuit_acceptance(tmp_path):
    output = Path(os.environ.get('SST_CAPLAB_ACCEPTANCE_OUTPUT', tmp_path))
    output.mkdir(parents=True, exist_ok=True)
    p = pipeline()  # CV, DLCP, AS, approved defect, Arrhenius and apparent Walter DOS.
    science_path = str(output/'science.caplab.json')
    first = run(p, 'save_as', path=science_path)
    first_id = first['workspace']['active_project_id']
    circuit, settings = fixture()
    circuit_path = str(output/'circuit.caplab.json')
    run(circuit, 'save', path=circuit_path)
    second = run(first['project'], 'open', workspace=first['workspace'], path=circuit_path)
    assert len(second['workspace']['projects']) == 2
    second_id = second['workspace']['active_project_id']
    current = run(second['project'], 'activate', workspace=second['workspace'], project_id=first_id)
    folder = FIX/'m2/data_Cd/#133_Ann250C_10.7mm2/AS'
    options = dict(profile='lab_as_cp_rp', bias_V=0, use_metadata_candidates=True, variable_delimiter='K',
                   custom_variables=[dict(name='T', source='stem', token_index=0, substring_start=8)],
                   metadata_rules={'temperature_K': '{T}'})
    request = dict(paths=[str(folder)], options=options,
                   target_sample_id=current['project']['samples'][0]['id'])
    preview = run(current['project'], 'preview', **request)
    assert len(preview['sources']) == 34 and all(s['error'] is None for s in preview['sources'])
    before = {m['id'] for m in current['project']['measurements']}
    current = run(current['project'], 'import', workspace=current['workspace'], **request)
    p = current['project']
    def step(operation, **request):
        nonlocal current, p
        current = run(p, operation, workspace=current['workspace'], **request)
        p = current['project']
        return current
    assert len(p['samples']) == 1
    ids = [m['id'] for m in p['measurements'] if m['id'] not in before]
    assert len(ids) == 34
    step('plot', plot_type='C-f', measurement_ids=ids)
    assert len(children(p, children(p, p['graph'])[0]['id'])) == 34
    step('save', path=str(output/'compare.caplab.json'))
    # The folder includes a repeated 140 K acquisition; select the original explicitly.
    map_ids = [m['id'] for m in p['measurements'] if m['id'] in ids
               and not m['metadata']['source'].endswith('140K_REL_1.dat')]
    for view in ('surface', 'heatmap'):
        result = step('plot', plot_type='AS C(f,T)', measurement_ids=map_ids, view_kind=view)
        p, graph = result['project'], result['graph_id']
        assert p['comparisons'][-1]['view_kind'] == view
        layer = children(p, graph)[0]
        curve = children(p, layer['id'])[0]
        objects = {o['id']: o for o in p['engine']['objects']}
        f, t, z = [objects[c]['values'] for c in curve['columns'][1:4]]
        assert len(set(t)) == 33 and len(set(f)) == 40 and len(z) == 1320
        limits = [min(f), max(f), min(t), max(t), min(z), max(z)]
        state = dict(ranges=limits[:4])
        if view == 'surface':
            state['camera'] = dict(rotation_xyzw=[-.513269, -.212596, -.318184, .76819], axis_limits=limits)
        step('plot', action='view', graph_id=graph, view=state)
        step('save_as', path=str(output/f'AS-{view}.caplab.json'))
        for extension in ('png', 'svg', 'pdf'):
            exported = step('export', path=str(output/f'AS-{view}.{extension}'))['export']
            assert exported['z_axis']['unit'] == 'F' and exported['colormap'] == 'Viridis'
            assert exported['x_axis']['ticks'] and exported['y_axis']['ticks']
            if view == 'surface':
                assert any('F' in t['label'] for t in exported['z_axis']['ticks'])
            assert Path(output/f'AS-{view}.{extension}').stat().st_size > 1000
    saved = step('save_as', path=science_path)
    closed = run(saved['project'], 'close', workspace=saved['workspace'])
    assert len(closed['workspace']['projects']) == 1
    reopened = run(closed['project'], 'open', workspace=closed['workspace'], path=science_path)
    assert reopened['project'] == p and len(reopened['workspace']['projects']) == 2
    current = run(p, 'activate', workspace=reopened['workspace'], project_id=second_id)
    circuit = current['project']
    model_settings = {k: v for k, v in settings.items() if k in ('circuit_id',)}
    model_settings.update(frequency_Hz=[1, 10, 100, 1000], temperature_K=300, bias_V=0)
    circuit = run(circuit, 'simulate', settings=model_settings)['project']
    assert circuit['analyses'][-1]['grid_source'] == 'model'
    assert circuit['analyses'][-1]['row_ids'] == []  # Same-sample measurements are overlays only.
    draft = run(circuit, 'fit', settings=settings)['draft']
    accepted = run(circuit, 'accept_fit', draft=draft)['project']
    assert accepted['analyses'][-1]['status'] == 'valid'
    for parameter in accepted['analyses'][-1]['parameters']:
        assert parameter['fitted'] == pytest.approx({'R0': 20, 'R1': 500, 'C1': 20e-6}[parameter['element']], rel=1e-3)
    run(accepted, 'save', path=circuit_path)
    assert run(accepted, 'open', path=circuit_path)['project'] == accepted
    (output/'workflow.json').write_text(json.dumps(dict(temperatures=33, frequencies=40,
        cells=1320, analyses=[a['type'] for a in p['analyses']], circuit_status='valid'), indent=2))
