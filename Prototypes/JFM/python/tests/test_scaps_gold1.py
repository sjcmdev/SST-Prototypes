"""SCAPS 4/9 regression. Phase 4 lacks area: explicitly declare 1 cm2.
Resistance conversion is R[ohm] = rho[ohm cm2] / area[cm2], not multiplication.
Five-parameter SD+Iph is distinct from the requested seven-parameter light model.
Failure lists are frozen after investigation; strict xfail makes improvements visible.
"""
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from jfm_prefit.cpp_adapter import auto_prefit_characteristic, prefit_characteristic
from jfm_prefit.linear_fit import fit_line_lm
from jfm_prefit.preprocessing import prepare_sample
from jfm_prefit.adaptive_range import fit_and_diagnose
from jfm_prefit.types import RangeWindow, AutoPrefitOptions
from jfm_arrhenius.fit_arrhenius import weighted_linear

FIX=Path(__file__).resolve().parents[2]/'tests/fixtures/scaps-gold1'
REF=json.loads((FIX/'reference.json').read_text())
PHASE4=np.load(FIX/'curves.npz')
TSVS=sorted(p.name for p in FIX.glob('*.tsv'))
AREA_M2=1e-4  # Declared normalization; phase-4 params.json has no area.
AUTO_FAILURES = {
    'phase-4-dark-rs0-rsh10000',
    'phase-4-dark-rs1-rshoff',
    'phase-4-dark-rs10-rshoff',
    'phase-4-light-rs0-rsh100',
    'phase-4-light-rs0-rsh10000',
    'phase-4-light-rs0-rshoff',
    'phase-4-light-rs1-rsh100',
    'phase-4-light-rs1-rsh10000',
    'phase-4-light-rs1-rshoff',
    'phase-4-light-rs10-rsh100',
    'phase-4-light-rs10-rsh10000',
    'phase-4-light-rs10-rshoff',
}
RECOVERY_FAILURES = {
    'phase-4-light-rs0-rsh100',
    'phase-4-light-rs0-rsh10000',
    'phase-4-light-rs1-rsh100',
    'phase-4-light-rs1-rsh10000',
    'phase-4-light-rs1-rshoff',
    'phase-4-light-rs10-rsh100',
    'phase-4-light-rs10-rsh10000',
    'phase-4-light-rs10-rshoff',
}
BRANCH_FAILURES = set()
N_FAILURES = {
    'cigs',
}
PHASE9_FAILURES = set()
RESULTS={}


def test_fixture_sha256():
    for name,digest in json.loads((FIX/'SHA256.json').read_text())['fixtures'].items():
        assert hashlib.sha256((FIX/name).read_bytes()).hexdigest()==digest
    assert len(TSVS)==32 and len(PHASE4.files)==18


def curve(name):
    if name in PHASE4:
        a=PHASE4[name].copy();a[:,1]*=AREA_M2;return a,300
    a=np.loadtxt(FIX/name,skiprows=1);return a,int(name.split('-T')[1].split('K')[0])


@pytest.fixture(scope='module')
def automatic():
    cache={}
    def get(name):
        if name not in cache:
            a,t=curve(name);cache[name]=auto_prefit_characteristic(a[:,0],a[:,1],t)
            RESULTS[name]={'branch':cache[name]['branch'],'selected':cache[name]['selected']}
        return cache[name]
    return get


def case(name,failures,reason):
    if failures is AUTO_FAILURES and name == 'phase-4-light-rs1-rsh100':
        reason = 'Historical 7p target; owner rule selects valid 5p over 97% of positive voltage span'
    return pytest.param(name,marks=pytest.mark.xfail(strict=True,reason=reason)) if name in failures else name


@pytest.mark.parametrize('name',[case(n,AUTO_FAILURES,'SCAPS: automatic residual/range search rejects curve or chooses a different model; REPORT.md') for n in PHASE4.files])
def test_phase4_model(name,automatic):
    r=automatic(name);s=r['selected'];assert s is not None
    assert s['model_id']==('SD-RsRsh-alpha-Rsh2-I_ph' if REF[name]['light'] else 'SD-RsRsh')


@pytest.mark.parametrize('name',[case(n,BRANCH_FAILURES,'SCAPS high-Rs illumination is misclassified as dark') for n in PHASE4.files])
def test_phase4_branch(name,automatic):
    assert automatic(name)['branch']==('light' if REF[name]['light'] else 'dark')


@pytest.mark.parametrize('name',[case(n,RECOVERY_FAILURES,'SCAPS: automatic prefit range confounds Rs/Rsh with diode terms; REPORT.md') for n in PHASE4.files])
def test_phase4_resistance(name):
    a,t=curve(name);light=REF[name]['light'];r=prefit_characteristic(a[:,0],a[:,1],t,'SD-RsRsh-I_ph' if light else 'SD-RsRsh','light' if light else 'dark')
    p=r['parameters'];RESULTS.setdefault(name,{})['fixed']=r
    area_cm2=AREA_M2*1e4
    # 5% permits an effective diode fit to nonideal drift-diffusion SCAPS data;
    # zero-Rs absolute tolerance is 0.01 ohm cm2; off-shunt is a lower bound.
    assert p['Rs']['value']*area_cm2==pytest.approx(REF[name]['Rs_Ohm_cm2'],rel=.05,abs=.01)
    shunt=REF[name]['Rsh_Ohm_cm2']
    recovered_shunt=(p['Rsh']['value']-p['Rs']['value'])*area_cm2
    if shunt is None or isinstance(shunt,str):assert recovered_shunt>=1e8
    # Inverse Drobiazg mapping keeps the original external-Rsh 5% tolerance.
    else:assert recovered_shunt==pytest.approx(shunt,rel=.05)


@pytest.mark.parametrize('name',[case(n,PHASE9_FAILURES,'SCAPS phase9: default auto search returns no valid candidate; REPORT.md') for n in TSVS])
def test_phase9_auto_fit(name,automatic):
    r=automatic(name); assert r['branch']==('light' if '-light-' in name else 'dark')
    assert r['selected'] is not None


@pytest.mark.parametrize('cell',['cigs','cdte'])
def test_phase9_local_n_300K(cell):
    a,t=curve(cell+'-dark-T300K.tsv');mask=(a[:,0]>=.2-1e-10)&(a[:,0]<=.4+1e-10)&(a[:,1]>0)
    fit=fit_line_lm(a[mask,0],np.log(a[mask,1]));n=1/(fit.slope*8.617333262145e-5*t)
    expected=next(p['n_eff'] for p in REF['phase9_reference'][cell]['dark_effective_parameters'] if p['temperature_K']==300)
    sample,_=prepare_sample(a[:,0],a[:,1],t,'dark')
    indices=np.flatnonzero(mask)
    candidate=fit_and_diagnose(sample,'SD-0',RangeWindow(int(indices[0]),int(indices[-1])+1,'SCAPS local window'),{},AutoPrefitOptions())
    RESULTS[cell+'-local-n']={'n':n,'reference':expected,'JFM_fitted_n':candidate.parameters['A'].value}
    assert candidate.parameters['A'].value == pytest.approx(expected, abs=.04)
    assert n==pytest.approx(expected,rel=.002)
    assert n==pytest.approx(2. if cell=='cigs' else 1.88,abs=.02)


@pytest.mark.parametrize('cell',[case(n,N_FAILURES,'SCAPS phase9: automatic range does not recover local 0.2-0.4 V effective n; REPORT.md') for n in ['cigs','cdte']])
def test_phase9_auto_n_300K(cell,automatic):
    s=automatic(cell+'-dark-T300K.tsv')['selected'];assert s is not None
    assert s['parameters']['A']['value']==pytest.approx(2. if cell=='cigs' else 1.88,abs=.04)


@pytest.mark.parametrize('cell',['cigs','cdte'])
@pytest.mark.parametrize('minimum_K',[200,280])
def test_phase9_voc_activation(cell,minimum_K):
    ts=[];vs=[]
    for t in range(minimum_K,341,20):
        a,_=curve(f'{cell}-light-T{t}K.tsv');ts.append(t);vs.append(float(np.interp(0,a[:,1],a[:,0])))
    fit=weighted_linear(ts,vs,[1.]*len(ts),False)
    expected=REF['phase9_reference'][cell][f'Ea_Voc_intercept_{minimum_K}_340K_eV']
    RESULTS[f'{cell}-Voc-{minimum_K}']={'Ea_eV':fit['b'],'reference_eV':expected}
    # Interpolation of exported 5mV voltage grid, 0K intercept tolerance 2meV.
    assert fit['b']==pytest.approx(expected,abs=.002)


@pytest.fixture(scope='session',autouse=True)
def save_results():
    yield
    import os
    if os.environ.get('GOLD1_RESULTS'):
        Path(os.environ['GOLD1_RESULTS']).write_text(json.dumps(RESULTS,indent=2))
