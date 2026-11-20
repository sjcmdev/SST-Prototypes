"""Compare stored data through nanobind -> Core command/job -> undoable acceptance."""
import copy
import json
from pathlib import Path

import pytest
from caplab import execute, import_preview, project_from_preview
from test_m8_workflow import pipeline, run


@pytest.fixture(scope="module")
def project():
    return pipeline()


def children(p, parent):
    return sorted((o for o in p['engine']['objects'] if o['parent'] == parent), key=lambda o: (o['order'], o['id']))


@pytest.mark.parametrize('plot_type', ['C-V', 'Mott-Schottky'])
def test_cv_direct_without_analysis(project, plot_type):
    p = copy.deepcopy(project)
    mid = p['measurements'][1]['id']
    result = run(p, 'plot', plot_type=plot_type, measurement_ids=[mid])
    assert len(result['project']['analyses']) == len(p['analyses'])
    assert result['project']['comparisons'][-1]['inputs'][0]['id'] == mid
    raw = children(result['project'], children(result['project'], result['graph_id'])[0]['id'])
    assert len(raw) == 1
    objects = {o['id']: o for o in result['project']['engine']['objects']}
    source = next(o for o in p['measurements'] if o['id'] == mid)
    values = [next(o['values'] for o in p['engine']['objects'] if o['id'] == cid) for cid in source['columns']]
    assert objects[raw[0]['columns'][1]]['values'] == values[0]
    area = p['samples'][0]['area_m2']
    expected = values[1] if plot_type == 'C-V' else [area**2/c**2 for c in values[1]]
    assert objects[raw[0]['columns'][2]]['values'] == pytest.approx(expected)


@pytest.mark.parametrize('plot_type', ['C-V', 'Mott-Schottky', 'N_CV(W)', 'N_DLCP(W)'])
def test_imported_profile_direct(plot_type):
    source = Path(__file__).resolve().parents[2]/'tests/fixtures/m2/#132_DL_1e3Hz_360K.dat'
    preview = import_preview(source.read_text(), str(source), options=dict(profile='propho_dl', temperature_K=360, frequency_Hz=1000))
    p = project_from_preview(preview)
    result = run(p, 'plot', plot_type=plot_type, measurement_ids=[p['measurements'][0]['id']])
    assert not result['project']['analyses']
    assert len(result['project']['comparisons']) == 1


@pytest.mark.parametrize('plot_type', ['C-f', 'G/omega-f', 'Nyquist', 'Bode magnitude', 'Bode phase'])
def test_frequency_direct_and_cancel(project, plot_type):
    mid = project['measurements'][0]['id']
    result = run(project, 'plot', plot_type=plot_type, measurement_ids=[mid])
    layer = children(result['project'], result['graph_id'])[0]
    assert layer['spec']['equalAspect'] == (plot_type == 'Nyquist')
    cancelled = execute(project, dict(operation='plot', plot_type=plot_type, measurement_ids=[mid], cancel=True))
    assert not cancelled['ok'] and cancelled['error']['code'] == 'cancelled'
    assert cancelled['project'] == project


def test_every_saved_analysis_plot_can_be_compared(project):
    for analysis in project['analyses']:
        for graph in analysis['graphs']:
            result = run(project, 'plot', graph_ids=[graph], name='Stored '+analysis['type'])
            p = result['project']
            assert p['comparisons'][-1]['analysis_ids'] == [analysis['id']]
            assert len(children(p, children(p, result['graph_id'])[0]['id'])) == len(children(project, children(project, graph)[0]['id']))
    cv_graph = project['analyses'][0]['graphs'][0]
    walter_graph = project['analyses'][-1]['graphs'][0]
    result = execute(project, dict(operation='plot', graph_ids=[cv_graph, walter_graph]))
    assert not result['ok'] and result['error']['code'] == 'plot_failed'
    assert result['project'] == project
    result = run(project, 'plot', analysis_ids=[project['analyses'][0]['id']], plot_index=2)
    assert result['project']['comparisons'][-1]['analysis_ids'] == [project['analyses'][0]['id']]


def test_measurement_and_simulation_can_share_axes(project):
    p = run(project, 'edit_circuit', edit=dict(action='create', sample_id=project['samples'][0]['id'], name='R', topology='R0'))['project']
    p = run(p, 'simulate', settings=dict(circuit_id=p['circuits'][-1]['id'], frequency_Hz=[100, 1000], temperature_K=300, bias_V=0))['project']
    nyquist = p['analyses'][-1]['graphs'][0]
    result = run(p, 'plot', plot_type='Nyquist', measurement_ids=[p['measurements'][0]['id']], graph_ids=[nyquist])
    assert result['project']['comparisons'][-1]['analysis_ids'] == [p['analyses'][-1]['id']]


def test_invalid_ids_and_duplicate_sources_are_atomic(project):
    mid = project['measurements'][0]['id']
    for request in [dict(measurement_ids=[mid, mid]), dict(measurement_ids=[999999]), dict(graph_ids=[mid]),
                    dict(measurement_ids=[]), dict(plot_type='N_DLCP(W)', measurement_ids=[mid])]:
        result = execute(project, dict(operation='plot', **request))
        assert not result['ok'] and result['error']['code'] == 'plot_failed'
        assert result['project'] == project


@pytest.mark.parametrize('format', ['png', 'svg', 'pdf'])
def test_saved_views_batch_export_matches_snapshot(project, tmp_path, format):
    mid = project['measurements'][0]['id']
    p = copy.deepcopy(project)
    graphs = []
    for plot_type in ['C-f', 'G/omega-f']:
        source = project['analyses'][2]['graphs'][0 if plot_type == 'C-f' else 2]
        r = run(p, 'plot', plot_type=plot_type, measurement_ids=[mid], graph_ids=[source])
        p, graph = r['project'], r['graph_id']
        plots = children(p, children(p, graph)[0]['id'])
        r = run(p, 'plot', action='view', graph_id=graph, view=dict(ranges=[100, 10000, 1e-14, 1e-10],
            visibility={str(plots[-1]['id']): False},
            x_ticks=[dict(value=100, label='10^2'), dict(value=10000, label='10^4')]))
        p = r['project']; graphs.append(graph)
    path = tmp_path/'views.caplab.json'
    run(p, 'save', path=str(path))
    reopened = run(p, 'open', path=str(path))['project']
    assert reopened == p
    out = run(reopened, 'export', path=str(tmp_path/f'batch.{format}'), graphs=[dict(graph_id=g) for g in graphs])
    assert len(out['export']['graphs']) == 2
    for item in out['export']['graphs']:
        assert item['snapshot']['ranges'] == [100, 10000, 1e-14, 1e-10]
        assert item['snapshot']['x_axis']['ticks'][0]['label'] == '10^2'
        assert len(item['snapshot']['series']) == 1
        assert Path(item['path']).stat().st_size > 500
    selected = run(reopened, 'export', graph_id=graphs[0], path=str(tmp_path/f'selected.{format}'))
    assert selected['export']['ranges'] == [100, 10000, 1e-14, 1e-10]


def test_view_rejects_foreign_visibility_and_invalid_log_ranges(project):
    result = run(project, 'plot', plot_type='C-f', measurement_ids=[project['measurements'][0]['id']])
    p, graph = result['project'], result['graph_id']
    for view in [dict(ranges=[100, 10000, 1e-14, 1e-10], visibility={'0': False}),
                 dict(ranges=[-1, 10000, 1e-14, 1e-10])]:
        failed = execute(p, dict(operation='plot', action='view', graph_id=graph, view=view))
        assert not failed['ok'] and failed['error']['code'] == 'plot_failed'
        assert failed['project'] == p
