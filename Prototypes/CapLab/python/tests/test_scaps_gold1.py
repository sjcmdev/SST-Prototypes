"""SCAPS phase 3/6/7/8 golden data: C only; G is deliberately excluded.
Inputs are losslessly selected columns; SHA256.json records source and fixture hashes.
AS uses the source's three-point log-frequency inflection refinement and windows.
The comparison is to extracted Ea, never to the differently biased trap input.
"""
import hashlib
import json
from pathlib import Path
import numpy as np
import pytest
from caplab.admittance import analyze_as, preprocess
from caplab.analysis import analyze

FIX = Path(__file__).resolve().parents[2] / 'tests/fixtures/scaps-gold1'
REF = json.loads((FIX/'reference.json').read_text())
DATA = np.load(FIX/'curves.npz')
RESULTS = {}


def measurement(data, temperature, cv=False):
    n = len(data)
    return dict(id=1, sample_id=2, area_m2=1e-4, temperature_K=temperature,
        kind='CVSweep' if cv else 'FrequencySweep', variant='C_G',
        names=['V','C'] if cv else ['f','C','G'], units=['V','F'] if cv else ['Hz','F','S'],
        columns=[data[:,0].tolist(), (data[:,1]*1e-4).tolist()] + ([] if cv else [[0.]*n]),
        row_ids=list(range(1,n+1)), source_rows=list(range(1,n+1)))


def test_fixture_sha256():
    for name, digest in json.loads((FIX/'SHA256.json').read_text())['fixtures'].items():
        assert hashlib.sha256((FIX/name).read_bytes()).hexdigest() == digest


def extraction(model, level=None, refine=True):
    if model == 'phase3':
        expected = REF['phase3_reference']; points=expected['admittance']
        temperatures=[p['temperature_K'] for p in points]
        frequencies=[p['inflection_frequency_Hz'] for p in points]
    else:
        expected=REF['phase7_reference']['fits'][model][level]
        temperatures=expected['temperatures_K']; frequencies=expected['frequency_Hz']
    picked=[]
    for t, target in zip(temperatures,frequencies):
        if model=='phase3': a=DATA[f'phase-3-cf-defect-T{int(t)}'][:,:2]
        else:
            a=DATA[f'phase-7-{model}-admittance']; a=a[a[:,0]==t,1:3]
        r=preprocess(measurement(a,t)); f=np.array(r['frequency_Hz']); d=-np.array(r['D_F'])
        # Same branch window as the SCAPS analysis; prevents swapping double levels.
        candidates=[p for p in r['candidates'] if target/2 < p['frequency_Hz'] < target*2]
        assert candidates, (model,t,target)
        p=min(candidates,key=lambda p:abs(np.log(p['frequency_Hz']/target))); i=p['row_id']-1
        if refine:
            x=np.log(f); c=np.polyfit(x[i-1:i+2]-x[i],d[i-1:i+2],2)
            picked.append(float(np.exp(x[i]-c[1]/(2*c[0]))))
        else:
            picked.append(p['frequency_Hz'])
    defect=dict(id=1,sample_id=2,name=model,points=[dict(id=i+1,temperature_K=t,frequency_Hz=f,enabled=True) for i,(t,f) in enumerate(zip(temperatures,picked))])
    fit=analyze_as(None,dict(type='Arrhenius',defect=defect))['fits'][0]
    key=('refined-' if refine else 'grid-')+model+str(level)
    RESULTS[key]=dict(Ea_eV=fit['E_a_eV'],reference_eV=expected.get('Ea_eV',expected.get('activation_energy_eV')),prefactor=fit['prefactor'])
    return RESULTS[key]


@pytest.mark.parametrize('model,level',[('phase3',None),('single','0.35'),('gaussian','0.35'),('double','0.28'),('double','0.43')])
def test_as_arrhenius(model,level):
    r=extraction(model,level)
    assert r['Ea_eV']==pytest.approx(r['reference_eV'],abs=.002)
    if model == 'phase3':
        assert r['Ea_eV'] == pytest.approx(REF['phase6_reference']['admittance_inflection']['energy_eV'], abs=.002)


@pytest.mark.parametrize('model,level', [
    ('phase3',None),('single','0.35'),('gaussian','0.35'),('double','0.28'),
    pytest.param('double','0.43',marks=pytest.mark.xfail(strict=True,
        reason='CapLab grid-only inflection shifts double 0.43 eV Ea by +3.158 meV; no sub-bin peak refinement'))])
def test_default_as_grid_arrhenius(model,level):
    r=extraction(model,level,refine=False)
    assert r['Ea_eV']==pytest.approx(r['reference_eV'],abs=.002)


def test_baseline_has_no_resolved_trap_step():
    a=DATA['phase-7-baseline-admittance']
    for t in np.unique(a[:,0]):
        r=preprocess(measurement(a[a[:,0]==t,1:3],t))
        # Baseline changes <2e-8 F/m2 across seven decades; a trap at 150 K
        # is outside the frequency window, so a trap/baseline ratio is meaningless.
        assert np.ptp(r['capacitance_F']) < 2e-12



def cv_result(name):
    a=DATA[name];t=int(name.split('-T')[1].split('-')[0]);m=measurement(a,t,True)
    return analyze(m,dict(type='CV',material='project',project_material=dict(eps_r=11.7,source='SCAPS saved definition'),range_V=[-3,-.2],xd_source='CV',as_bias_V=0.))


@pytest.mark.parametrize('name',[k for k in DATA.files if k.startswith('uniform-')])
def test_uniform_mott_schottky(name):
    r=cv_result(name); RESULTS[name]=dict(N_m3=r['N_m3'])
    assert r['N_m3']==pytest.approx(1e22,rel=.001)


@pytest.mark.parametrize('name',[k for k in DATA.files if k.startswith('step-')])
def test_step_position(name):
    r=cv_result(name);w=np.array(r['profile']['W_m']);n=np.array(r['profile']['N_m3']); order=np.argsort(w)
    crossing=float(np.interp(2e22,n[order],w[order])); RESULTS[name]=dict(step_m=crossing)
    expected=next(s for s in REF['phase8_reference']['scans'] if s['id']==name)
    # Phase-8 step_midpoint_depth_m is the interpolated N=2e22 crossing.
    nearest=float(w[np.argmin(abs(n-2e22))]);RESULTS[name]['nearest_midpoint_m']=nearest
    assert abs(crossing-.4e-6)<.02e-6
    assert crossing == pytest.approx(expected['step_midpoint_depth_m'], abs=1e-12)


def test_walter_gaussian_peak():
    a=DATA['phase-7-gaussian-admittance']; b=DATA['phase-7-baseline-admittance'];a=a[a[:,0]==300,1:3];b=b[b[:,0]==300,1:3]
    a=a.copy();a[:,1]-=b[:,1]
    source=REF['phase7_reference']; meta=next(m for m in source['measurements'] if m['model']=='gaussian' and m['temperature_K']==300)
    expected=next(m for m in source['walter'] if m['model']=='gaussian' and m['temperature_K']==300)
    m=measurement(a,300)
    options=dict(type='Walter',energy_range_eV=[.001,1.12],V_b_V=meta['Vbi_V'],E_F_eV=0.,zeta_s_1_K_2=source['fits']['gaussian']['0.35']['xi_s1_K2'],
        parameter_source='SCAPS phase-7 Vbi/width/prefactor; explicit Ef=0 comparison',xd_source='manual',xd_manual_m=meta['width_m'],xd_description='SCAPS epsilon/C(10MHz)')
    r=analyze(m,options);s=r['spectra'][0];i=np.argmax(s['density_cm3_eV']);peak=s['energy_eV'][i]
    # LinearField1996 has Vbi^2/(Vbi-Ef+E); SCAPS used constant Vbi.
    # Keep the native kernel result; also compare after explicitly removing that factor.
    e=np.array(s['energy_eV']); density=np.array(s['density_cm3_eV']); order=np.argsort(e)
    same_method=density*(meta['Vbi_V']+e)/meta['Vbi_V']
    same_peak=float(e[np.argmax(same_method)])
    integral_ratio=float(np.trapezoid(np.maximum(density[order],0),e[order])*1e6/5e20)
    RESULTS['walter']=dict(peak_eV=peak,reference_eV=expected['peak_eV'],source_integral_ratio=expected['integral_ratio'],native_integral_ratio=integral_ratio,same_method_peak_eV=same_peak)
    assert same_peak == pytest.approx(expected['peak_eV'], abs=1e-10)
    # One 30-points/decade frequency bin at 300 K is 1.984 meV.
    assert peak==pytest.approx(expected['peak_eV'],abs=.002)


@pytest.fixture(scope='session',autouse=True)
def save_results():
    yield
    import os
    if os.environ.get('GOLD1_RESULTS'):
        Path(os.environ['GOLD1_RESULTS']).write_text(json.dumps(RESULTS,indent=2))

