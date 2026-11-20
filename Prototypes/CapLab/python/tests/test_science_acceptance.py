"""Independent covariance propagation and frozen A7 source investigation."""
import copy
import json
import math
from pathlib import Path

import numpy as np
import pytest

from caplab.analysis import analyze
from caplab.importers import parse_measurement
from test_m3_analysis import CASES, FIX, cv, raw, options, dl_options
from test_m4_admittance import fit
from test_m8_workflow import pipeline


@pytest.mark.parametrize('case', CASES, ids=[c[0] for c in CASES])
def test_real_lab_cv_derived_errors(case):
    _, _, file, t, area, limits, _, _ = case
    m = parse_measurement((FIX/'data_Cd'/file).read_text(encoding='utf-8'), file, 'propho_dl', ',',
                          dict(temperature_K=t, frequency_Hz=1000))
    m.update(id=1, sample_id=2, area_m2=area)
    settings = options(); settings.update(range_V=limits, xd_source='DLCP', as_bias_V=sum(limits)/2)
    r = analyze(m, settings)
    v = np.array(m['columns'][m['names'].index('V')])
    y = np.array(m['columns'][m['names'].index('A2_C02')])
    selected = (v >= limits[0]) & (v <= limits[1])
    # Independent NumPy polynomial covariance, reordered to intercept/slope.
    coeff, cov = np.polyfit(v[selected], y[selected], 1, cov=True)
    slope, intercept = coeff; cov = cov[::-1, ::-1]
    vb_grad = np.array([-1/slope, intercept/slope**2])
    assert r['V_b_standard_error_V'] == pytest.approx(math.sqrt(vb_grad@cov@vb_grad), rel=1e-8)
    assert r['N_standard_error_m3'] == pytest.approx(abs(r['N_m3']/slope)*math.sqrt(cov[1, 1]), rel=1e-8)
    width = r['x_d_fit']
    grad = width['value_m']/(2*(intercept+slope*width['bias_V']))*np.array([1., width['bias_V']])
    assert width['standard_error_m'] == pytest.approx(math.sqrt(grad@cov@grad), rel=1e-8)
    assert width['standard_error_m'] > 0
    # Imported profiles have no coefficient covariance: do not invent an error bar.
    assert r['x_d']['standard_error_m'] is None
    assert r['x_d']['uncertainty_status'].startswith('unavailable')
    json.dumps(r, allow_nan=False)


@pytest.mark.parametrize('model_index', [0, 1])
def test_arrhenius_standard_errors_are_not_capture_cross_section(model_index):
    points = [(180, 120), (210, 740), (250, 4100), (300, 32000), (340, 100000)]
    r = fit(points)['fits'][model_index]
    t, f = np.array(points, float).T
    log_rate = np.log(f) if model_index else np.log(2*math.pi*f/t**2)
    coeff, cov = np.polyfit(1/t, log_rate, 1, cov=True)
    assert r['E_a_standard_error_eV'] == pytest.approx(math.sqrt(cov[0, 0])*8.617333262145179e-5, rel=1e-8)
    assert r['prefactor_standard_error'] == pytest.approx(math.exp(coeff[1])*math.sqrt(cov[1, 1]), rel=1e-8)
    assert r['E_a_standard_error_eV'] != r['sigma_cm2']
    two = fit(points[:2])['fits'][model_index]
    assert two['E_a_standard_error_eV'] is None and two['prefactor_standard_error'] is None


def test_noisy_dlcp_covariance_cross_term_and_width_interpolation():
    m = raw()
    m['columns'][2] = (np.array(m['columns'][2])+np.array([1, -2, 1, 2, -3, 1])*1e-12).tolist()
    r = analyze(m, dl_options()); g = r['groups'][0]
    coef, cov = np.polyfit(m['columns'][1], m['columns'][2], 2, cov=True)
    coef, cov = coef[::-1], cov[::-1, ::-1]
    # Finite differences of the physical equations, including correlated C0/C1.
    density = lambda c: -c[0]**3/(2*1.602176634e-19*8.8541878188e-11*1e-12*c[1])
    step = 1e-15
    grad = np.array([(density(coef+np.eye(3)[i]*step)-density(coef-np.eye(3)[i]*step))/(2*step) for i in range(3)])
    assert g['N_DL_standard_error_m3'] == pytest.approx(math.sqrt(grad@cov@grad), rel=1e-6)
    assert g['x_e_standard_error_m'] == pytest.approx(abs(g['x_e_m']/coef[0])*math.sqrt(cov[0, 0]), rel=1e-8)
    assert r['x_d']['standard_error_m'] == g['x_e_standard_error_m']
    fit_result = copy.deepcopy(r)
    fit_result['groups'] = [dict(g, bias_V=-1), dict(g, bias_V=1, x_e_standard_error_m=2*g['x_e_standard_error_m'])]
    settings = dl_options(); settings.update(dlcp_fit=fit_result, xd_policy='linear')
    interpolated = analyze(m, settings)['x_d']
    assert interpolated['standard_error_m'] == pytest.approx(math.sqrt(1.25)*g['x_e_standard_error_m'])


def test_cv_no_dof_and_no_fit_width_extrapolation():
    settings = options(); settings['excluded_row_ids'] = [12, 13, 14]
    r = analyze(cv(), settings)
    assert r['V_b_standard_error_V'] is None and r['N_standard_error_m3'] is None
    assert r['x_d_fit']['standard_error_m'] is None
    settings['as_bias_V'] = 10
    assert analyze(cv(), settings)['x_d_fit']['value_m'] is None


def test_cv_optional_fit_bias_and_invalid_bias():
    settings = options()
    default = analyze(cv(), settings)['x_d_fit']
    settings['as_bias_V'] = None
    assert analyze(cv(), settings)['x_d_fit'] == default
    for bias in (float('nan'), float('inf')):
        settings['as_bias_V'] = bias
        with pytest.raises(ValueError, match='AS bias'):
            analyze(cv(), settings)


@pytest.mark.parametrize('degree', [1, 2])
def test_dlcp_width_errors_follow_selected_bias_and_missing_covariance(degree):
    settings = dl_options(); settings['degree'] = degree
    m = raw()
    r = analyze(m, settings)
    g = r['groups'][0]
    settings.update(dlcp_fit=copy.deepcopy(r), xd_policy='nearest')
    settings['dlcp_fit']['groups'] = [dict(g, bias_V=1, x_e_standard_error_m=2e-9),
                                      dict(g, bias_V=-.25, x_e_standard_error_m=1e-9)]
    assert analyze(m, settings)['x_d']['standard_error_m'] == 1e-9
    settings['xd_policy'] = 'linear'
    assert analyze(m, settings)['x_d']['standard_error_m'] == pytest.approx(math.hypot(.8e-9, .4e-9))
    settings['dlcp_fit']['groups'][0]['x_e_standard_error_m'] = None
    assert analyze(m, settings)['x_d']['standard_error_m'] is None
    settings.pop('dlcp_fit')
    settings['excluded_row_ids'] = m['row_ids'][degree+1:]
    r = analyze(m, settings)
    assert r['groups'][0]['x_e_standard_error_m'] is None
    assert r['groups'][0]['N_DL_standard_error_m3'] is None
    assert r['x_d']['standard_error_m'] is None


def test_a7_source_sets_are_frozen_without_tuning():
    evidence = json.loads((Path(__file__).parent/'fixtures/a7_sources.json').read_text())
    for item, expected in zip(evidence['sources'], [.156167135801643, .173038521237882]):
        r = fit(item['points_T_K_f_Hz'])['fits'][1]
        assert r['E_a_eV'] == pytest.approx(expected, rel=1e-10)
        assert r['E_a_eV'] == pytest.approx(item['saved_energy_eV'], rel=1e-9)
        assert r['sigma_cm2'] == pytest.approx(item['saved_capture_cross_section_cm2'], rel=1e-9)
        assert abs(r['E_a_eV']-evidence['thesis']['energy_eV']) > .0005
    assert float(f"{r['sigma_cm2']:.3g}") == evidence['thesis']['capture_cross_section_cm2']
    assert len(evidence['sources'][0]['points_T_K_f_Hz']) == 15
    assert len(evidence['sources'][1]['points_T_K_f_Hz']) == 12


def test_headless_command_job_pipeline_keeps_uncertainty_and_apparent_dos():
    p = pipeline()
    cv_result, dlcp, _, arrhenius, walter = p['analyses']
    assert cv_result['V_b_standard_error_V'] is not None
    assert dlcp['groups'][0]['N_DL_standard_error_m3'] is not None
    assert arrhenius['fits'][0]['E_a_standard_error_eV'] is not None
    assert walter['quantity'] == 'apparent DOS'
    assert 'W1-W3' in walter['historical_limits']
    assert walter['interpretation'].startswith('CL-04')
    json.dumps(p, allow_nan=False)
