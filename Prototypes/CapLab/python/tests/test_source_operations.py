"""clfiles1: real Core commands/jobs, stable identities and source-only changes."""
import json
from pathlib import Path

import pytest
from caplab import execute, import_preview, project_from_preview, parse_lab_as
from caplab.importers import parse_measurement


def run(project, operation, **request):
    result = execute(project, dict(operation=operation, **request))
    assert result['ok'], result
    return result


def initial(path):
    path.write_text('# T=300 K\n100 3e-9 1000000\n1000 2e-9 1000000\n10000 1e-9 1000000', encoding='utf-8')
    return project_from_preview(import_preview(path.read_text(encoding='utf-8'), str(path),
        options=dict(profile='lab_as_cp_rp', area_m2=1e-6, bias_V=0)))


def test_named_profile_persistence_units_sign_and_existing_sample(tmp_path):
    p = initial(tmp_path/'seed.dat')
    path = tmp_path/'測定_żółć.dat'
    path.write_text('2 10 3\n1 5 2', encoding='utf-8')
    options = dict(profile='lab_is_z', imaginary_sign='-ImZ', units={'f': 'kHz', 'ReZ': 'kOhm', 'ImZ': 'kOhm'},
                   temperature_K=310, bias_V=-1)
    p = run(p, 'profile', action='save', name='Impedance / lab', options=options)['project']
    loaded = run(p, 'profile', action='load', name='Impedance / lab')
    assert loaded['options'] == options
    saved = tmp_path/'project.caplab.json'
    run(p, 'save', path=str(saved))
    p = run(p, 'open', path=str(saved))['project']
    request = dict(path=str(path), format_profile='Impedance / lab', target_sample_id=p['samples'][0]['id'])
    preview = run(p, 'preview', **request)
    assert preview['groups'][0]['temperature_K'] == 310
    assert preview['groups'][0]['bias_V'] == -1
    assert preview['groups'][0]['kind'] == 'FrequencySweep'
    imported = run(p, 'import', **request)['project']
    assert len(imported['samples']) == 1 and len(imported['measurements']) == 2
    measurement = imported['measurements'][1]
    assert measurement['sample_id'] == p['samples'][0]['id']
    result = run(imported, 'profile', action='delete', name='Impedance / lab')['project']
    assert not result['import_profiles']
    import sst_caplab, json
    values = json.loads(sst_caplab.measurement(json.dumps(imported), measurement['id']))
    assert values['columns'] == [[1000, 2000], [5000, 10000], [-2000, -3000]]


def test_filename_folder_candidates_header_wins_conflict_requires_confirmation(tmp_path):
    folder = tmp_path/'annealed_350'
    folder.mkdir()
    source = folder/'S17_400_-0.5.dat'
    source.write_text('# T=300 K\n# sample_name=Header sample\n100 1e-9 200', encoding='utf-8')
    p = initial(tmp_path/'seed.dat')
    options = dict(profile='lab_as_cp_rp', custom_variables=[dict(name='sample', source='stem', token_index=0)],
        metadata_rules=dict(sample_name='{sample}', temperature_K='{stem_1}', treatment='{folder_0}', bias_V='{stem_2}'))
    preview = run(p, 'preview', path=str(source), options=options)['sources'][0]['preview']
    sample = preview['samples'][0]
    m = sample['measurements'][0]
    assert sample['name'] == 'Header sample'
    assert m['temperature_K'] == 300 and m['bias_V'] is None
    assert m['metadata_candidates'] == dict(sample_name='S17', temperature_K=400, treatment='annealed', bias_V=-.5)
    assert {c['field'] for c in m['metadata_conflicts']} == {'temperature_K', 'sample_name'}
    failed = execute(p, dict(operation='import', path=str(source), options=options))
    assert not failed['ok'] and failed['project'] == p
    imported = run(p, 'import', path=str(source), options=options, confirm_metadata_conflicts=True)['project']
    assert imported['measurements'][-1]['metadata']['temperature_K'] == 300
    confirmed = dict(options, temperature_K=400, sample_name='S17', treatment='annealed', bias_V=-.5)
    imported = run(p, 'import', path=str(source), options=confirmed, confirm_metadata_conflicts=True)['project']
    assert imported['samples'][-1]['name'] == 'S17'
    assert imported['measurements'][-1]['metadata']['temperature_K'] == 400
    candidates = run(p, 'preview', path=str(source), options=dict(options, use_metadata_candidates=True))['sources'][0]['preview']
    assert candidates['samples'][0]['name'] == 'S17'
    assert candidates['samples'][0]['measurements'][0]['temperature_K'] == 400
    assert candidates['samples'][0]['measurements'][0]['metadata_conflicts']


def test_reimport_diff_invalidation_stale_accept_and_unicode_relink(tmp_path):
    source = tmp_path/'source.dat'
    p = initial(source)
    mid = p['measurements'][0]['id']
    p = run(p, 'analyze', settings=dict(type='AS', measurement_id=mid))['project']
    assert p['analyses'][0]['status'] == 'valid'
    p = run(p, 'plot', plot_type='C-f', measurement_ids=[mid])['project']
    source.write_text(source.read_text(encoding='utf-8').replace('3e-9', '4e-9'), encoding='utf-8')
    result = run(p, 'reimport', measurement_id=mid)
    assert result['project'] == p
    draft = result['source_draft']
    assert draft['diff']['changed_cells'] == 1 and not draft['diff']['grid_changed']
    updated = run(p, 'accept_source', draft=draft)['project']
    assert updated['measurements'][0]['id'] == mid
    assert updated['measurements'][0]['columns'] == p['measurements'][0]['columns']
    assert updated['analyses'][0]['status'] == 'stale'
    assert updated['comparisons'][0]['status'] == 'stale'
    assert not execute(updated, dict(operation='accept_source', draft=draft))['ok']
    cancelled = execute(updated, dict(operation='reimport', measurement_id=mid, cancel=True))
    assert not cancelled['ok'] and cancelled['project'] == updated and cancelled['error']['code'] == 'cancelled'
    updated = run(updated, 'analyze', settings=dict(type='AS', measurement_id=mid))['project']
    updated = run(updated, 'plot', plot_type='C-f', measurement_ids=[mid])['project']
    moved = tmp_path/'przeniesione_測定_żółć.dat'
    source.rename(moved)
    relink = run(updated, 'relink', measurement_id=mid, path=str(moved))
    assert relink['project'] == updated and relink['source_draft']['provenance_only']
    linked = run(updated, 'accept_source', draft=relink['source_draft'])['project']
    assert linked['measurements'][0]['metadata']['source'] == str(moved)
    assert linked['measurements'][0]['metadata']['sha256'] == updated['measurements'][0]['metadata']['sha256']
    assert linked['analyses'][-1]['status'] == 'valid'
    assert linked['comparisons'][-1]['status'] == 'valid'
    assert linked['comparisons'][0]['status'] == 'stale'
    import sst_caplab
    assert json.loads(sst_caplab.measurement(json.dumps(linked), mid))['columns'] == json.loads(sst_caplab.measurement(json.dumps(updated), mid))['columns']
    moved.write_text(moved.read_text(encoding='utf-8').replace('4e-9', '5e-9'), encoding='utf-8')
    failed = execute(updated, dict(operation='relink', measurement_id=mid, path=str(moved)))
    assert not failed['ok'] and failed['project'] == updated


def test_changed_grid_disables_approved_defect_points(tmp_path):
    source = tmp_path/'source.dat'
    p = initial(source)
    mid = p['measurements'][0]['id']
    p = run(p, 'edit_defect', edit=dict(action='create', sample_id=p['samples'][0]['id'], name='D1'))['project']
    sheet = next(o for o in p['engine']['objects'] if o['id'] == mid)
    p = run(p, 'edit_defect', edit=dict(action='add', defect_id=p['defects'][0]['id'],
        point=dict(measurement_id=mid, row_id=sheet['rows'][0], provenance='Approved measured point')))['project']
    source.write_text(source.read_text(encoding='utf-8').replace('100 3e-9', '200 3e-9'), encoding='utf-8')
    draft = run(p, 'reimport', measurement_id=mid)['source_draft']
    assert draft['diff']['grid_changed']
    updated = run(p, 'accept_source', draft=draft)['project']
    point = updated['defects'][0]['points'][0]
    assert point['measurement_id'] is None and point['row_id'] is None and not point['enabled']
    assert point['previous_measurement_id'] == mid


def test_raw_dlcp_preview_groups_row_metadata(tmp_path):
    p = initial(tmp_path/'seed.dat')
    source = tmp_path/'raw.dat'
    source.write_text('-1 .01 1e-9 300 1000\n-1 .02 2e-9 300 1000\n0 .01 3e-9 310 2000', encoding='utf-8')
    groups = run(p, 'preview', path=str(source), options=dict(profile='lab_dlcp_raw',
        mapping=dict(temperature_K=3, frequency_Hz=4)))['groups']
    assert {(g['bias_V'], g['temperature_K'], g['frequency_Hz']) for g in groups} == {(-1, 300, 1000), (0, 310, 2000)}
    assert sorted(g['sources'][0]['row_indices'] for g in groups) == [[0, 1], [2]]


def test_legacy_reimport_retains_auxiliary_columns(tmp_path):
    source = tmp_path/'legacy.dat'
    source.write_text('100 1e-9 200 9', encoding='utf-8')
    p = project_from_preview(parse_lab_as(source.read_text(), str(source)), 300)
    mid = p['sweeps'][0]['id']
    source.write_text('100 1e-9 200 10', encoding='utf-8')
    draft = run(p, 'reimport', measurement_id=mid)['source_draft']
    updated = run(p, 'accept_source', draft=draft)['project']
    assert updated['measurements'][0]['metadata']['names'] == ['f', 'C', 'Rp', 'Source4']
    assert len(updated['measurements'][0]['columns']) == 4


def test_relink_never_reinterprets_filename_rules_or_extension(tmp_path):
    source = tmp_path/'S1_300.dat'
    source.write_text('100 1e-9 200', encoding='utf-8')
    p = project_from_preview(import_preview(source.read_text(), str(source), options=dict(profile='lab_as_cp_rp',
        metadata_rules=dict(sample_name='{stem_0}', temperature_K='{stem_1}'), use_metadata_candidates=True)))
    mid = p['measurements'][0]['id']
    moved = tmp_path/'renamed_測定.bin'
    source.rename(moved)
    draft = run(p, 'relink', measurement_id=mid, path=str(moved))['source_draft']
    linked = run(p, 'accept_source', draft=draft)['project']
    assert linked['measurements'][0]['metadata']['source'] == str(moved)
    assert linked['measurements'][0]['metadata']['temperature_K'] == 300
    assert linked['samples'][0]['name'] == 'S1'


def test_batch_creates_one_named_target_sample(tmp_path):
    p = initial(tmp_path/'seed.dat')
    sources = []
    for t in (280, 300, 320):
        source = tmp_path/f'm_{t}.dat'
        source.write_text(f'# T={t} K\n100 1e-9 200', encoding='utf-8')
        sources.append(str(source))
    result = run(p, 'import', paths=sources, target_sample_name='Temperature series',
        options=dict(profile='lab_as_cp_rp', area_m2=1e-6))['project']
    assert len(result['samples']) == 2
    sample = next(s for s in result['samples'] if s['name'] == 'Temperature series')
    assert sample['area_m2'] == 1e-6
    assert {m['metadata']['temperature_K'] for m in result['measurements'] if m['sample_id'] == sample['id']} == {280, 300, 320}


def test_headless_reimport_without_source_is_a_typed_error():
    p = project_from_preview(import_preview('100 1e-9 200', '', options=dict(profile='lab_as_cp_rp')))
    result = execute(p, dict(operation='reimport', measurement_id=p['measurements'][0]['id']))
    assert not result['ok'] and result['error']['code'] == 'invalid_request'
    assert 'source path' in result['error']['message']


def test_reimport_conflicts_require_confirmation_at_acceptance(tmp_path):
    source = tmp_path/'測定_400K.dat'
    p = initial(source)
    mid = p['measurements'][0]['id']
    source.write_text(source.read_text(encoding='utf-8').replace('3e-9', '4e-9'), encoding='utf-8')
    draft = run(p, 'reimport', measurement_id=mid)['source_draft']
    failed = execute(p, dict(operation='accept_source', draft=draft))
    assert not failed['ok'] and failed['project'] == p
    assert 'metadata conflicts' in failed['error']['message']
    updated = run(p, 'accept_source', draft=draft, confirm_metadata_conflicts=True)['project']
    assert updated['measurements'][0]['metadata']['temperature_K'] == 300


@pytest.mark.parametrize('options', [dict(metadata_rules={'bad': '{stem}'}), dict(metadata_rules={'bias_V': 'nan'}),
    dict(metadata_rules={'temperature_K': '-1'}), dict(metadata_rules={'sample_name': '{missing}'}), dict(variable_delimiter=3)])
def test_metadata_rule_validation(options):
    with pytest.raises(ValueError):
        parse_measurement('100 1e-9 200', options=options)
