"""Frozen acceptance cases and independent importer boundary checks."""
import copy
import hashlib
import json
from pathlib import Path

import pytest
import sst_caplab
from caplab import project_from_preview
from caplab.importers import import_preview, parse_measurement

FIX = Path(__file__).resolve().parents[2] / 'tests/fixtures/m2'


def text(name):
    return (FIX / name).read_bytes().decode('utf-8')


def preview(name, profile, decimal='.', **options):
    return import_preview(text(name), str(FIX / name), decimal, dict(profile=profile, **options))


def roundtrip(p):
    assert json.loads(sst_caplab.roundtrip(json.dumps(p, allow_nan=False))) == p


def test_frozen_source_hashes():
    for item in json.loads(text('sources.json')):
        assert hashlib.sha256((FIX / item['file']).read_bytes()).hexdigest() == item['sha256']


def test_pfs_i1():
    updates = []
    result = import_preview(text('data_Cd/data_Cd.json'), str(FIX / 'data_Cd/data_Cd.json'), ',',
        {'profile': 'propho_json', 'base_dir': str(FIX)}, progress=updates.append)
    assert updates == sorted(updates) and updates[-1] == 1
    assert [s['name'] for s in result['samples']] == ['#132', '#133', '#138', '#139', '#143']
    assert [s['area_m2'] for s in result['samples']] == pytest.approx([3.7e-6, 10.7e-6, 9e-6, 5.5e-6, 3.75e-6])
    assert all(s['measurements'] for s in result['samples'])
    p = project_from_preview(result)
    assert len(p['samples']) == 5
    assert len(p['measurements']) == sum(len(s['measurements']) for s in result['samples'])
    assert p['samples'][0]['metadata']['Ea_eV'] == .76
    assert 'Eg' not in p['samples'][0]['metadata']
    roundtrip(p)


def test_pfs_i3():
    name = '#132_DL_1e3Hz_360K.dat'
    m = parse_measurement(text(name), name, 'propho_dl', options={'temperature_K': 360, 'frequency_Hz': 1000})
    assert len(m['source_rows']) == 21 and m['source_rows'][0] == 3
    assert m['temperature_K'] == m['suggested_temperature_K'] == 360
    assert m['frequency_Hz'] == m['suggested_frequency_Hz'] == 1000
    assert m['kind'] == 'DLCPProfile'
    assert m['columns'][0][0] == pytest.approx(7.351115e-7)


def test_pfs_i4():
    p = project_from_preview(preview('#1_CF_test_200K.dat', 'converted_as_c_g'))
    m = p['measurements'][0]['metadata']
    assert len(m['source_rows']) == 49
    assert m['variant'] == 'C_G' and m['units'][:3] == ['Hz', 'F', 'S']
    assert m['source_columns'][1][0] == pytest.approx(2.84031777e-8)
    assert m['source_columns'][2][0] == pytest.approx(2.716077105e-7)
    roundtrip(p)


def test_pfs_i5():
    result = preview('CV_test_230K_DL1e+03Hz_REL.dat', 'propho_dl')
    m = result['samples'][0]['measurements'][0]
    assert len(m['source_rows']) == 81 and m['kind'] == 'CV-derived'
    assert m['profiles'] == {'DLCP': None, 'CV': ['W_CV', 'N_CV']}
    roundtrip(project_from_preview(result))


def test_pfs_d1():
    m = parse_measurement(text('#002_CIGS2014_REL_DL1e5Hz_300K.dat'), profile='propho_dl')
    assert [m['columns'][i][0] for i in (0, 1, 7, 8)] == pytest.approx([.4659196e-6, 6.758017e20, .4656992e-6, 7.741254e20])
    assert m['profiles']['DLCP'] != m['profiles']['CV']


@pytest.mark.parametrize('area', [3.7e-6, 5e-5])
def test_scaps_cf_blocks_scaling_sign_hash_and_rows(area):
    result = preview('benchmark_propho.cf', 'scaps_cf', area_m2=area)
    ms = result['samples'][0]['measurements']
    assert len(ms) == 11 and [m['temperature_K'] for m in ms] == list(range(200, 301, 10))
    m, a = ms[0], area * 1e4
    assert [c[0] for c in m['columns'][:6]] == pytest.approx([100, 56.8063554*a*1e-9, 5.43215421e-7*a, 426.302537/a, -28010.6136/a, 28013.8574/a])
    assert len(m['source_rows']) == 49 and m['bias_V'] == 0
    assert m['input_units'][1:4] == ['nF/cm^2', 'S/cm^2', 'Ohm*cm^2']
    assert ms[1]['source_rows'][0] > m['source_rows'][-1]
    assert m['sha256'] == hashlib.sha256((FIX / 'benchmark_propho.cf').read_bytes()).hexdigest()
    roundtrip(project_from_preview(result))


def test_scaps_cv_scaling_and_no_historical_napp_correction():
    result = preview('CV_230K_1e3Hz.cv', 'scaps_cv', area_m2=3.7e-6)
    m = result['samples'][0]['measurements'][0]
    assert m['temperature_K'] == 230 and m['frequency_Hz'] == 1000
    assert len(m['columns']) == 12 and len(m['source_rows']) == 81
    assert m['columns'][1][0] == pytest.approx(39.9861689*.037e-9)
    assert m['columns'][4][0] == pytest.approx(1.36288516e22)
    assert m['columns'][6][0] == pytest.approx(-1.32122054e-10)
    roundtrip(project_from_preview(result))


def test_missing_area_never_defaults():
    with pytest.raises(ValueError, match='explicit area'):
        preview('CV_230K_1e3Hz.cv', 'scaps_cv')


def test_lab_cv_mapping_decimal_comma_units_extra_and_directions():
    m = parse_measurement('C;V;G;extra\n2,0;-1,0;3,0;7\n4,0;0,0;5,0;8\n3,0;-1,0;4,0;9',
        'pomiar.csv', 'lab_cv', ',', {'mapping': {'V': 1, 'C': 0, 'G': 2}, 'units': {'C': 'nF', 'G': 'uS'}})
    assert m['columns'][0] == [-1, 0, -1]
    assert m['columns'][1] == pytest.approx([2e-9, 4e-9, 3e-9])
    assert m['columns'][2] == pytest.approx([3e-6, 5e-6, 4e-6])
    assert m['columns'][3] == [7, 8, 9]
    assert m['temperature_K'] is None and m['frequency_Hz'] is None
    assert m['sweep_segments'] == [dict(direction=1, rows=[0, 1]), dict(direction=-1, rows=[1, 2])]


@pytest.mark.parametrize('profile,unit,value', [('lab_is_cp_rp', 'Ohm', 200), ('lab_is_c_g', 'S', .005)])
def test_is_r_g(profile, unit, value):
    m = parse_measurement(f'100 1e-9 {value}', profile=profile)
    assert m['units'][2] == unit and m['columns'][2] == [value]


def test_is_z_sign_sort_original_rows_and_extras():
    m = parse_measurement('200 5 3 9\n100 2 1 8', profile='lab_is_z', options={'imaginary_sign': '-ImZ'})
    assert m['columns'] == [[100, 200], [2, 5], [-1, -3], [8, 9]]
    assert m['source_rows'] == [2, 1] and m['source_column_rows'] == [1, 2]
    assert m['source_columns'][2] == [3, 1]
    with pytest.raises(ValueError, match='imaginary_sign'):
        parse_measurement('100 2 3', profile='lab_is_z')


def test_dlcp_groups_and_missing_conditions():
    body = '-1 10 2 300 1\n-1 20 3 300 1\n0 10 4 310 2'
    opts = {'mapping': {'temperature_K': 3, 'frequency_Hz': 4}, 'units': {'deltaV': 'mV', 'C': 'nF', 'frequency_Hz': 'kHz'},
            'amplitude_definition': 'RMS', 'bias_definition': 'DC bias', 'constant_maximum_voltage': False}
    m = parse_measurement(body, profile='lab_dlcp_raw', options=opts)
    assert m['groups'] == [dict(bias_V=-1, temperature_K=300, frequency_Hz=1000, rows=[0, 1]),
                           dict(bias_V=0, temperature_K=310, frequency_Hz=2000, rows=[2])]
    assert m['columns'][1] == [.01, .02, .01]
    p = project_from_preview(import_preview(body, 'raw.dat', options=dict(profile='lab_dlcp_raw', **opts)))
    assert all('row_ids' in g and 'rows' not in g for g in p['measurements'][0]['metadata']['groups'])
    roundtrip(p)
    missing = parse_measurement('-1 .01 1e-9', profile='lab_dlcp_raw')
    assert missing['amplitude_definition'] is None and missing['constant_maximum_voltage'] is None


def test_duplicates_need_explicit_exclusion():
    body = '100 1e-9 200\n100 2e-9 300\n200 3e-9 400'
    with pytest.raises(ValueError, match='Duplicate'):
        parse_measurement(body)
    m = parse_measurement(body, options={'exclude_rows': [2]})
    assert m['source_rows'] == [1, 3] and m['excluded_rows'] == [2]


@pytest.mark.parametrize('body', ['', '1 2', '1 2 3\n2 3', '1 2 3\nbroken', '1 nan 3', '0 1 2'])
def test_bad_or_truncated_rows_are_not_dropped(body):
    with pytest.raises(ValueError):
        parse_measurement(body)


@pytest.mark.parametrize('options', [{'area_m2': -1}, {'temperature_K': 0}, {'frequency_Hz': -1},
    {'mapping': {'f': 1, 'C': 1}}, {'mapping': {'f': 99}}, {'units': {'C': 'Ohm'}}])
def test_invalid_settings(options):
    with pytest.raises(ValueError):
        parse_measurement('100 1e-9 200', options=options)


def test_manifest_base_directories_unicode_and_duplicate_names(tmp_path):
    unicode_name = '\u017c\u00f3\u0142\u0107'
    directory = tmp_path / unicode_name
    directory.mkdir()
    (directory / 'pomiar.dat').write_text('100 1e-9 200', encoding='utf-8')
    sample = dict(name=unicode_name, area=3.7, Ea_eV=.76, as_data_dir=unicode_name)
    body = json.dumps({'textdata': {'samples': [sample, sample]}})
    result = import_preview(body, str(tmp_path / 'manifest.json'), options={'profile': 'propho_json'})
    assert result['base_dir'] == str(tmp_path.resolve())
    p = project_from_preview(result)
    assert len(p['samples']) == 2 and p['samples'][0]['id'] != p['samples'][1]['id']
    assert p['samples'][0]['name'] == unicode_name
    roundtrip(p)
    elsewhere = tmp_path / 'other'
    elsewhere.mkdir()
    with pytest.raises(ValueError, match='base_dir'):
        import_preview(body, str(elsewhere / 'manifest.json'), options={'profile': 'propho_json'})
    corrected = import_preview(body, str(elsewhere / 'manifest.json'), options={'profile': 'propho_json', 'base_dir': str(tmp_path)})
    assert corrected['samples'] == result['samples']


def test_historical_project_data_references_results_and_no_guessed_constants():
    p = project_from_preview(preview('samples_Cd_diffusion.json', 'propho_json'))
    assert len(p['samples']) == 5
    h = p['historical'][0]
    assert h['status'] == 'historical'
    d = h['data']
    measurements = {m['id']: m for m in p['measurements']}
    fit = d['walter']['fits'][0]
    assert fit['status'] == 'historical' and fit['model'] == 'ProPho-compat'
    assert measurements[fit['measurement_id']]['sample_id'] == h['sample_id']
    assert d['walter']['selections']['fits'] == fit['id']
    assert d['walter']['selections']['as_for_int'] in measurements
    assert d['walter']['concentration'] == 475105311003778.2
    assert d['defects'][0]['E_T'] == pytest.approx(.5506142907058237)
    assert all(point['measurement_id'] in measurements for point in d['defects'][0]['points'])
    assert 'epsilon_r' not in fit and 'kernel_version' not in fit
    assert h['source_data']['walter']['fits'][0]['dl_idx'] == 0
    roundtrip(p)


def test_historical_scaps_explicit_profile_and_nonfinite_quality():
    with pytest.raises(ValueError, match='as_profile'):
        preview('scaps_ref.json', 'propho_json')
    p = project_from_preview(preview('scaps_ref.json', 'propho_json', as_profile='converted_as_c_g'))
    assert p['measurements'][0]['metadata']['variant'] == 'C_G'
    assert any(m['metadata']['quality'] for m in p['measurements'])
    for m in p['measurements']:
        for q in m['metadata']['quality']:
            if q['reason'] == 'historical_nonfinite':
                assert q['value'] is None
    roundtrip(p)


def test_missing_historical_inputs_and_nonfinite_results():
    data = dict(samples=[dict(name='missing', area=None, defects=[], walter=dict(fits=[dict(dl_idx=99, Nd=float('nan'))], saved_idx={'as_for_int': [5]}))])
    p = project_from_preview(import_preview(json.dumps(data), 'old.json', options={'profile': 'propho_json'}))
    h = p['historical'][0]['data']['walter']
    assert h['fits'][0]['measurement_id'] is None and h['fits'][0]['input_status'] == 'missing_input'
    assert h['fits'][0]['Nd'] == {'value': None, 'reason': 'historical_nonfinite'}
    assert h['selections']['as_for_int'] is None
    roundtrip(p)


def test_cpp_boundary_rejects_invalid_bundle_and_columns():
    result = import_preview('100 1e-9 200', 'x.dat', options={'profile': 'lab_as_cp_rp'})
    for change in ('rows', 'unit', 'variant', 'frequency'):
        bad = copy.deepcopy(result)
        m = bad['samples'][0]['measurements'][0]
        if change == 'rows': m['source_rows'] = [0]
        if change == 'unit': m['units'][1] = 'Ohm'
        if change == 'variant': m['variant'] = 'unknown'
        if change == 'frequency': m['columns'][0][0] = 0
        with pytest.raises(RuntimeError):
            project_from_preview(bad)
    p = project_from_preview(result)
    p['measurements'][0]['columns'][1] = p['measurements'][0]['columns'][0]
    with pytest.raises(RuntimeError):
        sst_caplab.roundtrip(json.dumps(p))


def test_cancel_and_progress():
    updates = []
    import_preview('200 1e-9 200\n100 2e-9 300', 'x.dat', options={'profile': 'lab_as_cp_rp'}, progress=updates.append)
    assert updates == [.5, 1.]
    with pytest.raises(RuntimeError, match='cancelled'):
        import_preview(text('samples_Cd_diffusion.json'), 'old.json', options={'profile': 'propho_json'}, cancelled=lambda: True)


def test_conflicts_bom_and_negative_rp_are_visible():
    body = '\ufeff# T=200 K\n100 1e-9 -20'
    m = parse_measurement(body, 'cf_100K.dat', options={'temperature_K': 300})
    assert len(m['diagnostics']) == 2
    assert m['quality'] == [dict(column=2, source_row=2, value=-20, reason='nonpositive_Rp')]
    assert m['columns'][2] == [-20]
    assert m['sha256'] == hashlib.sha256(body.encode()).hexdigest()


def test_persisted_history_rejects_dangling_reference():
    p = project_from_preview(preview('samples_Cd_diffusion.json', 'propho_json'))
    p['historical'][0]['data']['walter']['fits'][0]['measurement_id'] = 999999999
    with pytest.raises(RuntimeError):
        sst_caplab.roundtrip(json.dumps(p))


def test_scaps_bom_does_not_drop_first_batch():
    body = '\ufeff' + text('benchmark_propho.cf')
    result = import_preview(body, 'batch.cf', options={'profile': 'scaps_cf', 'area_m2': 3.7e-6})
    ms = result['samples'][0]['measurements']
    assert len(ms) == 11 and ms[0]['temperature_K'] == 200
    assert ms[0]['sha256'] == hashlib.sha256(body.encode()).hexdigest()


def test_owner_cl02_uses_same_core_import_command_path(tmp_path):
    from caplab import execute, project_from_preview, parse_lab_as
    project = project_from_preview(parse_lab_as("100 1e-9 1000", "seed.dat"), 300)
    root = Path(__file__).resolve().parents[2] / "tests/fixtures"
    for filename in ("m2/samples_Cd_diffusion.json", "cf_100K_7.dat", "m2/benchmark_propho.cf"):
        request = {"operation": "import", "path": str(root / filename)}
        if filename.endswith(".cf"):
            request["options"] = {"profile": "auto", "area_m2": 3.7e-6}
        result = execute(project, request)
        assert result["ok"], json.dumps(result.get("error"), indent=2)
        assert len(result["project"]["samples"]) > len(project["samples"])
    suggested = tmp_path / "cf_222K.dat"
    suggested.write_text("100 1e-9 1000", encoding="utf-8")
    assert parse_lab_as(suggested.read_text(), str(suggested))["suggested_temperature_K"] == 222
    unknown = execute(project, {"operation": "import", "path": str(suggested)})
    assert unknown["ok"], json.dumps(unknown.get("error"), indent=2)
    assert unknown["project"]["sweeps"][-1]["temperature_K"] is None
    confirmed = execute(project, {"operation": "import", "path": str(suggested),
                                  "options": {"profile": "auto", "temperature_K": 222}})
    assert confirmed["ok"], json.dumps(confirmed.get("error"), indent=2)
    assert confirmed["project"]["measurements"][-1]["metadata"]["temperature_K"] == 222
    bad = execute(project, {"operation": "import", "path": str(root / "missing.json")})
    assert not bad["ok"] and bad["error"]["code"] == "import_failed"
    assert bad["project"] == project

    for filename, text in [("cf_0K.dat", "100 1e-9 1000"),
                           ("bad-name.json", '{"samples":[{"name":7,"as_data":[],"dl_data":[]}]}')]:
        invalid = tmp_path / filename
        invalid.write_text(text, encoding="utf-8")
        bad = execute(project, {"operation": "import", "path": str(invalid)})
        assert not bad["ok"] and bad["error"]["code"] == "import_failed"
        assert bad["project"] == project
    malformed = tmp_path / "malformed.json"
    malformed.write_text("{bad json", encoding="utf-8")
    bad = execute(project, {"operation": "import", "path": str(malformed)})
    assert not bad["ok"] and bad["error"]["code"] == "import_failed"
    assert bad["project"] == project


def test_conversion_annotation_does_not_become_sample_name():
    from caplab import import_preview
    preview = import_preview("100 1e-9 1e-6", "measurement.dat (Cp/Rp -> C/G)", options={"profile": "lab_is_c_g"})
    assert preview["samples"][0]["name"] == "measurement"
    assert preview["samples"][0]["measurements"][0]["source"].endswith("(Cp/Rp -> C/G)")
