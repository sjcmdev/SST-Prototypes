"""M4: PFS A1-A7, PCF log-grid fixture, independent synthetic emission law.
Frozen project values are separate from the thesis comparison, notably A7.
"""
import copy
import json
import math
from pathlib import Path
import numpy as np
import pytest
import sst_caplab as kernel
from caplab.admittance import preprocess, analyze_as
from caplab.importers import unwrap

FIX = Path(__file__).resolve().parents[2]/'tests/fixtures/m2'
SAVED = unwrap(json.loads((FIX/'samples_Cd_diffusion.json').read_text()))
CASES = [('A1','#132','def2',.550614291,2.506903252e-15),
         ('A2','#139','def2',.146513748,2.018925754e-17),
         ('A3','#139','def3',.390515655,1.675548099e-15),
         ('A4','#143','def1',.226592568,2.637081964e-16),
         ('A7','#133','def1',.156167136,1.020398281e-17)]


def defect(points):
    return dict(id=42, sample_id=2, name='test defect', points=[dict(id=i+100, measurement_id=None,
        row_id=None, temperature_K=t, frequency_Hz=f, provenance='independent manual fixture', enabled=True)
        for i,(t,f) in enumerate(points)])


def fit(points, **options):
    return analyze_as(None, dict(type='Arrhenius', defect=defect(points), models=['omega-T2','ProPho-compat'], **options))


def sweep(f=None, c=None):
    f = f if f is not None else [100,200,400,800,1600]
    c = c if c is not None else [5e-9,4e-9,3e-9,2e-9,1e-9]
    return dict(id=1,sample_id=2,kind='FrequencySweep',variant='Cp_Rp',temperature_K=100,
        names=['f','C','Rp'],units=['Hz','F','Ohm'],source_rows=list(range(1,len(f)+1)),
        row_ids=list(range(11,11+len(f))),columns=[f,c,[1e6]*len(f)])


@pytest.mark.parametrize('case', CASES, ids=[c[0] for c in CASES])
def test_pfs_arrhenius_project_and_thesis(case):
    ident, sample, name, energy, sigma=case
    source=next(d for s in SAVED['samples'] if s['name']==sample for d in s['defects'] if d['name']==name)
    result=fit(source['ft_points']); ours=result['fits'][1]
    assert ours['E_a_eV']==pytest.approx(energy,rel=1e-8)
    assert ours['sigma_cm2']==pytest.approx(sigma,rel=1e-8,abs=0)
    assert ours['E_a_eV']==pytest.approx(source['E_T'],rel=1e-6)
    assert ours['sigma_cm2']==pytest.approx(source['sigma'],rel=1e-6,abs=0)
    assert result['fits'][0]['sigma_cm2'] is None
    assert ours['assumptions']['Ncv_cm3']==2e18
    if ident=='A1':
        assert result['fits'][0]['E_a_eV']==pytest.approx(.489646714,rel=1e-8)  # A6
        assert round(ours['sigma_cm2']/1e-15,2)==2.51
    if ident in ('A2','A4'):
        assert abs(ours['E_a_eV']-({'A2':.150,'A4':.230}[ident])) <= .005
    if ident=='A7':
        # Owner §8: thesis is normative; retain this conflict, never tune the fit.
        assert abs(ours['E_a_eV']-.170) > .005
        assert ours['sigma_cm2'] != pytest.approx(2.54e-17,rel=1e-2,abs=0)
    assert len(ours['residuals'])==len(source['ft_points'])
    json.dumps(result,allow_nan=False)
    output=Path(__file__).resolve().parents[4]/'build'/('caplab-m4-'+ident+'.json')
    output.write_text(json.dumps(dict(case=ident, energy_eV=ours['E_a_eV'], sigma_cm2=ours['sigma_cm2'],
        energy_diff=ours['E_a_eV']-energy, sigma_diff=ours['sigma_cm2']-sigma,
        default_energy_eV=result['fits'][0]['E_a_eV']),indent=2))


@pytest.mark.parametrize('sample', SAVED['samples'], ids=[s['name'] for s in SAVED['samples']])
def test_a5_full_gradient_arrays(sample):
    a=sample['as_data'][0]
    m=sweep(a['freq'],a['Cp']); m['columns'][2]=a['Rp']
    r=preprocess(m)
    assert np.array_equal(r['D_F'],a['dC'])
    assert len(r['D_F'])==len(a['freq'])


def test_pcf_log_grid_and_unequal_grid_edges():
    r=preprocess(sweep())
    assert r['D_F']==pytest.approx([-1.4426950408889634e-9]*5,rel=1e-13,abs=0)
    # C=x^2 for x=ln(f)=[0,1,3,6]: exact quadratic interior, first-order edges.
    r=preprocess(sweep([1,math.e,math.exp(3),math.exp(6)],[0,1,9,36]))
    assert r['D_F']==pytest.approx([1,2,6,9],rel=1e-13)


def test_synthetic_and_f_to_omega_prefactor():
    t=np.array([180.,220.,260.,300.,340.]); ea=.32; zeta=2e7
    # Independent SI law, constants literal; never generate expected values with kernel.
    omega=zeta*t*t*np.exp(-ea/((1.380649e-23/1.602176634e-19)*t))
    r=fit(list(zip(t,omega/(2*math.pi))))['fits'][0]
    assert r['E_a_eV']==pytest.approx(ea,rel=1e-6)
    assert r['prefactor']==pytest.approx(zeta,rel=1e-6)
    ffit=json.loads(kernel.arrhenius_fit(t.tolist(),(omega/(2*math.pi)).tolist(),False))
    assert r['prefactor']/ffit['prefactor']==pytest.approx(2*math.pi,rel=1e-12)
    assert r['E_a_eV']==pytest.approx(ffit['E_a_eV'],rel=1e-12)


def test_two_points_masks_models_gamma():
    d=defect([(200,100),(250,1000),(300,1e9)]);d['points'][2]['enabled']=False
    r=analyze_as(None,dict(type='Arrhenius',defect=d,models=['omega-T2','ProPho-compat']))
    assert len(r['diagnostics'])==2
    for f in r['fits']:
        assert f['dof']==0 and f['covariance'] is None and f['linear_covariance'] is None
        assert f['point_ids']==[100,101]
    g=fit([(200,100),(250,1000)],gamma_cm_2_s_K_2=2e24,gamma_source='explicit synthetic gamma')['fits'][0]
    assert g['sigma_cm2']==pytest.approx(g['prefactor']/2e24,rel=1e-12,abs=0)
    with pytest.raises(ValueError):fit([(200,100),(250,1000)],gamma_cm_2_s_K_2=2e24)
    for points in ([],[(200,100)],[(200,100),(200,1000)],[(0,100),(200,200)],[(200,0),(250,100)]):
        with pytest.raises((ValueError,RuntimeError)):fit(points)
    for models in ([],['bad'],['omega-T2','omega-T2']):
        with pytest.raises(ValueError): analyze_as(None,dict(type='Arrhenius',defect=d,models=models))


def test_sorting_mapping_candidates_and_smoothing():
    m=sweep([1600,100,800,200,400],[1e-9,5e-9,2e-9,4e-9,3e-9])
    r=preprocess(m)
    assert r['row_ids']==[12,14,15,13,11]
    assert r['D_F']==pytest.approx([-1.4426950408889634e-9]*5,rel=1e-13,abs=0)
    a=sweep([1,2,4,8,16,32,64],[10,9,7,3,2,1,0])
    r=analyze_as(a,dict(type='AS',smoothing={'method':'none'}))
    # Central slope at f=4 is (3-9)/(2*ln(2)); it is the deepest minimum.
    assert r['candidates'][0]['row_id']==13
    assert r['candidates'][0]['frequency_Hz']==pytest.approx(4*2**.25)
    assert r['candidates'][0]['D_F']==pytest.approx(-3.0625/math.log(2))
    assert r['candidates'][0]['measurement_id']==1
    # Candidate series wins screen-picking ties with overlapping raw points.
    assert r['plots'][1]['curves'][0]['candidates'] is True
    assert r['plots'][1]['curves'][0]['x']==[p['frequency_Hz'] for p in r['candidates']]
    smooth=preprocess(a,dict(method='Savitzky-Golay',window=5,degree=3))
    assert smooth['raw_D_F']==r['raw_D_F']
    assert smooth['processed_capacitance_F']!=r['capacitance_F']
    assert a['columns'][1]==[10,9,7,3,2,1,0]
    with pytest.raises(RuntimeError):analyze_as(a,dict(type='AS'),lambda:True)


@pytest.mark.parametrize('field,value', [('f',[100,100,400,800,1600]),('f',[0,200,400,800,1600]),('C',[1,float('nan'),3,4,5])])
def test_invalid_frequency_data(field,value):
    m=sweep();m['columns'][m['names'].index(field)]=value
    with pytest.raises(ValueError):preprocess(m)


def test_conductance_units_and_invalid_rp():
    m=sweep();m['columns'][2][0]=0
    r=preprocess(m);assert r['G_over_omega_F'][0] is None and r['diagnostics']
    assert r['G_over_omega_F'][1]==pytest.approx(1e-6/(2*math.pi*200),rel=1e-12,abs=0)
    m=sweep();m.update(variant='C_G',names=['f','C','G'],units=['Hz','F','S']);m['columns'][2]=[1e-6]*5
    assert preprocess(m)['G_over_omega_F'][0]==pytest.approx(1e-6/(2*math.pi*100),rel=1e-12,abs=0)
    m['units'][0]='kHz'
    with pytest.raises(ValueError):preprocess(m)
    for settings in ({'method':'bad'},{'method':'Savitzky-Golay','window':4,'degree':2},
                     {'method':'Savitzky-Golay','window':7,'degree':2}):
        with pytest.raises(ValueError):preprocess(sweep(),settings)
