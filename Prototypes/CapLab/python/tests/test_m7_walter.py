"""M7 independent spec/PCF/PFS acceptance; no expected kernel-generated values."""
import copy
import hashlib
import json
import math
from pathlib import Path
import numpy as np
import pytest
import sst_caplab as kernel
from caplab import execute, import_preview, project_from_preview
from caplab.analysis import analyze
from caplab.importers import unwrap

FIX = Path(__file__).resolve().parents[2]/'tests/fixtures/m2'


def sweep():
    return dict(id=1, sample_id=2, area_m2=1e-6, kind='FrequencySweep', variant='Cp_Rp',
        temperature_K=100, bias_V=0, names=['f','C','Rp'], units=['Hz','F','Ohm'],
        source_rows=[1,2,3], row_ids=[11,12,13],
        columns=[[100/math.e,100,100*math.e],[3e-12,2e-12,1e-12],[1e6]*3])


def options(**kw):
    return dict(type='Walter', V_b_V=1, E_F_eV=0, zeta_s_1_K_2=2*math.pi*.01,
        B_s_1=2*math.pi*100, parameter_source='PCF cf_100K_walter.dat', emission_source='PCF saved B',
        xd_source='manual', xd_manual_m=1e-6, xd_description='PCF fixture', **kw)


@pytest.mark.parametrize('compat',[False,True])
def test_independent_point_factors_units_T2_2pi(compat):
    # Spec 4.4, independent SI arithmetic (literal exact q and kB).
    t=250.; omega=2*math.pi*1234; prefactor=8e7
    e=(1.380649e-23/1.602176634e-19)*t*math.log(prefactor*(1 if compat else t*t)/omega)
    denominator=1.602176634e-19*(.8-.15+e)
    factor=(2*.8**1.5/(2e-6*math.sqrt(1.602176634e-19)*math.sqrt(denominator)) if compat
            else .8**2/(2e-6*denominator))
    density=-1.602176634e-19*factor*(-3e-7)/(1e6*1.380649e-23*t)
    p=json.loads(kernel.walter_point(omega,t,prefactor,.8,.15,2e-6,-3e-7,compat))
    assert p['energy_eV']==pytest.approx(e,rel=1e-13)
    assert p['field_factor_V_m_C']==pytest.approx(factor,rel=1e-13)
    assert p['density_cm3_eV']==pytest.approx(density,rel=1e-13)
    # Frequency is angular: forgetting 2pi shifts energy by kappa*T*ln(2pi).
    p2=json.loads(kernel.walter_point(1234,t,prefactor,.8,.15,2e-6,-3e-7,compat))
    assert p2['energy_eV']-p['energy_eV']==pytest.approx((1.380649e-23/1.602176634e-19)*t*math.log(2*math.pi))


def test_pcf_B_gradient_and_synthetic_log_capacitance():
    r=analyze(sweep(),options(model='ProPho-compat'))['spectra'][0]
    assert r['D_c_F_m2']==pytest.approx([-1e-6]*3,rel=1e-13)
    assert r['energy_eV']==pytest.approx([.008617333262145179,0,-.008617333262145179],abs=1e-15)
    assert r['field_factor_V_m_C'][1]==pytest.approx(2/(1e-6*1.602176634e-19),rel=1e-13)
    assert r['density_cm3_eV'][1]==pytest.approx(2/(1e6*1.602176634e-19*.008617333262145179),rel=1e-13)
    default=analyze(sweep(),options())['spectra'][0]
    assert default['density_cm3_eV'][1]==pytest.approx(r['density_cm3_eV'][1]/2,rel=1e-13)
    m=sweep(); m['temperature_K']=200
    twice=analyze(m,options(model='ProPho-compat'))['spectra'][0]
    assert twice['energy_eV'][0]==pytest.approx(r['energy_eV'][0]*2,abs=1e-15)
    d=analyze(m,options())['spectra'][0]
    assert d['energy_eV'][1]==pytest.approx((1.380649e-23/1.602176634e-19)*200*math.log(4),rel=1e-13)


def test_gradient_after_frequency_and_row_mask():
    m=sweep(); m['columns'][1]=[1e-12,4e-12,9e-12]
    o=options(frequency_range_Hz=[100,1000])
    r=analyze(m,o)['spectra'][0]
    assert r['D_c_F_m2']==pytest.approx([5e-6]*2,rel=1e-13)
    assert r['row_ids']==[12,13]
    r=analyze(m,options(excluded_row_ids=[11]))['spectra'][0]
    assert r['D_c_F_m2']==pytest.approx([5e-6]*2,rel=1e-13)
    assert all(v<0 for v in r['density_cm3_eV'])  # No abs/clipping.


@pytest.mark.parametrize('compat',[False,True])
def test_both_quadratures_and_reversed_order(compat):
    e=[.3,.2,.1]; n=[1e15,3e15,1e15]
    r=json.loads(kernel.walter_integral(e,n,.1,.3,compat))
    assert r['density_cm3']==pytest.approx(4e14,rel=1e-14)
    reverse=json.loads(kernel.walter_integral(e[::-1],n[::-1],.1,.3,compat))
    assert reverse['density_cm3']==pytest.approx(-4e14 if compat else 4e14)
    clipped=json.loads(kernel.walter_integral(e,n,.15,.25,compat))
    if compat:
        assert clipped['density_cm3'] is None
    else:
        assert clipped['density_cm3']==pytest.approx(2.5e14)


def test_invalid_domain_and_gaps_never_bridge():
    o=options(); o['E_F_eV']=2
    r=analyze(sweep(),o)
    s=r['spectra'][0]
    assert r['status']=='invalid' and s['density_cm3_eV']==[None]*3
    assert s['quality']==['nonpositive_walter_domain']*3 and s['integral']['density_cm3'] is None
    for compat in (False,True):
        r=json.loads(kernel.walter_integral([0,1,2,3],[1,None,1,1],0,3,compat))
        assert r['gaps'] and r['segments']==1
        if compat:
            assert r['density_cm3'] is None
        else:
            assert r['density_cm3']==1
    json.dumps(s,allow_nan=False)


@pytest.mark.parametrize('key,value',[('V_b_V',0),('zeta_s_1_K_2',0),('E_F_eV',None),('xd_manual_m',0),
    ('frequency_range_Hz',[1,1]),('integration_range_eV',[1,0]),('excluded_row_ids',[999]),('model','unknown')])
def test_invalid_inputs(key,value):
    o=options(); o[key]=value
    with pytest.raises((ValueError,RuntimeError)):
        analyze(sweep(),o)


def project():
    m=sweep()
    text='\n'.join(' '.join(str(c[i]) for c in m['columns']) for i in range(3))
    return project_from_preview(import_preview(text,'PCF.dat',options=dict(profile='lab_as_cp_rp',area_m2=1e-6,temperature_K=100,bias_V=0)))


def test_headless_core_job_roundtrip_cancel_stale_and_bad_request():
    p=project(); o=options(); o['measurement_id']=p['measurements'][0]['id']
    result=execute(p,dict(operation='walter',settings=o))
    assert result['ok'],result
    q=result['project']; a=q['analyses'][-1]
    assert a['type']=='Walter' and a['model']=='Walter1996-linear-field'
    assert len(a['graphs'])==1 and a['spectra'][0]['x_d']['value_m']==1e-6
    assert json.loads(kernel.roundtrip(json.dumps(q)))==q
    old=copy.deepcopy(q)
    q['samples'][0]['area_m2']=2e-6
    assert json.loads(kernel.refresh(json.dumps(q)))['analyses'][-1]['status']=='stale'
    c=execute(p,dict(operation='walter',settings=o,cancel=True))
    assert not c['ok'] and c['error']['code']=='cancelled' and c['project']==p
    o['zeta_s_1_K_2']=0
    bad=execute(p,dict(operation='walter',settings=o))
    assert not bad['ok'] and bad['error']['code']=='walter_failed' and bad['project']==p
    old['analyses'][-1]['spectra'][0]['density_cm3_eV'][0]=None
    assert not execute(old,dict(operation='walter',settings=o))['ok']


CASES=[('W1','samples_Cd_diffusion.json','#132',4.751053110e14),
       ('W2','samples_Cd_diffusion.json','#139',6.104602546e14),
       ('W3','scaps_ref.json','CIGS_scaps_ref',4.138087352e15)]


@pytest.mark.parametrize('ident,filename,name,expected',CASES)
def test_pfs_frozen_saved_integrals_via_Core(ident,filename,name,expected):
    text=(FIX/filename).read_text()
    p=project_from_preview(import_preview(text,str(FIX/filename),options=dict(profile='propho_json',as_profile='converted_as_c_g' if ident=='W3' else 'lab_as_cp_rp')))
    sid=next(s['id'] for s in p['samples'] if s['name']==name)
    h=next(v for v in p['historical'] if v['sample_id']==sid)
    r=execute(p,dict(operation='integrate_saved_walter',settings=dict(sample_id=h['sample_id'])))
    assert r['ok'],r
    a=r['project']['analyses'][-1]; actual=a['spectra'][0]['integral']['density_cm3']
    assert actual==pytest.approx(expected,rel=1e-4)
    assert a['compatibility']=='saved-spectrum-integral-only'
    assert a['spectra'][0]['saved_selection']==h['data']['walter']['selections']
    assert a['spectra'][0]['measurement_id']==h['data']['walter']['dist_for_integration']['measurement_id']
    assert json.loads(kernel.roundtrip(json.dumps(r['project'])))==r['project']
    out=Path(__file__).resolve().parents[4]/'build'/('caplab-m7-'+ident+'.json')
    out.write_text(json.dumps(dict(id=ident,spec_value=expected,ours=actual,diff=actual-expected,relative_diff=abs(actual/expected-1)),indent=2))


def test_w1_w3_full_reconstruction_explicitly_unsupported_owner_8():
    # Owner §8 closes this blocker by limiting v1 to integrals of saved spectra.
    h=dict(id=4,sample_id=2,data=dict(walter=dict(saved_coeff=[123])))
    with pytest.raises(ValueError,match='GUI reconstruction unsupported'):
        analyze(None,dict(type='WalterSaved',historical=h))

@pytest.mark.parametrize('source',['CV','DLCP'])
@pytest.mark.parametrize('policy',['block','linear','nearest'])
def test_width_sources_policies_same_sample_and_provenance(source,policy):
    from test_m3_analysis import cv
    m=sweep(); xd=cv(); xd['id']=7
    o=options(); o.update(xd_source=source,xd_policy=policy,material='ProPho-compat',as_bias_V=0)
    if source=='DLCP':
        xd.update(kind='DLCPProfile',names=['V','W'],units=['V','m'],columns=[[-1,1],[1e-6,3e-6]],source_rows=[1,2],row_ids=[70,71])
    else:
        xd['columns']=[v[:2] for v in xd['columns']]; xd['columns'][0]=[-1,1]
        xd['source_rows']=[1,2]; xd['row_ids']=[70,71]
    if policy=='block':
        with pytest.raises(ValueError,match='Missing exact'):
            analyze(m,o,xd)
        return
    r=analyze(m,o,xd); s=r['spectra'][0]
    assert s['x_d']['measurement_id']==7 and s['x_d']['policy']==policy
    assert s['x_d']['temperature_K']==300 and s['x_d']['frequency_Hz']==1000
    assert s['x_d']['bias_V']==0 and r['diagnostics']
    if source=='DLCP':
        assert s['x_d']['value_m']==pytest.approx(2e-6 if policy=='linear' else 1e-6)
    xd['sample_id']=999
    with pytest.raises(ValueError,match='same sample'):
        analyze(m,o,xd)
    xd['sample_id']=m['sample_id']; xd['columns'][0]=[1,2]
    if policy=='linear':
        with pytest.raises(ValueError,match='extrapolation'):
            analyze(m,o,xd)


def test_multiple_temperature_overlay_filters_and_measurement_selection():
    a=sweep(); b=copy.deepcopy(a); b.update(id=3,temperature_K=200,row_ids=[21,22,23])
    o=options(measurements=[a,b])
    r=analyze(a,o)
    assert len(r['spectra'])==2 and len(r['plots'][0]['curves'])==2
    o['temperature_range_K']=[150,250]
    assert len(analyze(a,o)['spectra'])==1
    o['temperature_range_K']=[250,300]
    with pytest.raises(ValueError,match='No spectra'):
        analyze(a,o)
    o['temperature_range_K']=[0,500]; b['sample_id']=99
    with pytest.raises(ValueError,match='same sample'):
        analyze(a,o)


def test_cv_arrhenius_sources_and_stale_missing_dependency():
    p=project(); m=json.loads(kernel.measurement(json.dumps(p),p['measurements'][0]['id']))
    # The source fit here is an independent, validated M4 result with manual T/f points.
    from test_m4_admittance import fit
    a=fit([(200,100),(250,1000),(300,10000)])
    a.update(id=99990,sample_id=m['sample_id'],graphs=[])
    a['defect']['sample_id']=m['sample_id']
    p['defects']=[copy.deepcopy(a['defect'])]
    p['analyses']=[a]
    o=options(); o.update(measurement_id=m['id'],arrhenius_fit_id=a['id'])
    r=execute(p,dict(operation='walter',settings=o))
    assert r['ok'],r
    q=r['project']; result=q['analyses'][-1]
    assert result['zeta_s_1_K_2']==a['fits'][0]['prefactor']
    assert result['source_analyses'][0]['id']==a['id']
    q['analyses'].pop(0)
    assert json.loads(kernel.refresh(json.dumps(q)))['analyses'][-1]['status']=='missing_input'
    p['analyses'][0]['status']='stale'
    bad=execute(p,dict(operation='walter',settings=o))
    assert not bad['ok'] and 'Stale/invalid' in bad['error']['message']


def test_compat_B_never_inferred_from_arrhenius():
    o=options(model='ProPho-compat'); del o['B_s_1']
    with pytest.raises(ValueError,match='B_s_1'):
        analyze(sweep(),o)
    o=options(model='ProPho-compat'); del o['emission_source']
    with pytest.raises(ValueError,match='Explicit B'):
        analyze(sweep(),o)

@pytest.mark.parametrize('entry',json.loads((FIX.parent/'m7/sources.json').read_text()),ids=lambda e:e['id'])
def test_frozen_slice_hashes(entry):
    data=(FIX/Path(entry['source']).name).read_bytes()
    assert hashlib.sha256(data).hexdigest()==entry['source_sha256']
    s=next(s for s in unwrap(json.loads(data))['samples'] if s['name']==entry['sample'])
    frozen=s['walter']['dist_for_integration']
    assert hashlib.sha256(json.dumps(frozen,sort_keys=True,separators=(',',':')).encode()).hexdigest()==entry['slice_sha256']
    assert len(frozen['E_w'])==entry['sample_count']


def test_selected_cv_fit_supplies_parameters_default_width_source_and_material():
    from caplab.importers import parse_measurement
    from test_m3_analysis import cv, options as cv_options
    cm=cv()
    preview=import_preview('36.787944117144235 3e-12 1e6\n100 2e-12 1e6\n271.8281828459045 1e-12 1e6',
        'PCF.dat',options=dict(profile='lab_as_cp_rp',area_m2=1e-6,temperature_K=100,bias_V=0))
    text='\n'.join(f'{v} {c}' for v,c in zip(cm['columns'][0],cm['columns'][1]))
    preview['samples'][0]['measurements'].append(parse_measurement(text,'CV.dat','lab_cv','.',dict(temperature_K=300,frequency_Hz=1000)))
    p=project_from_preview(preview)
    am=json.loads(kernel.measurement(json.dumps(p),p['measurements'][0]['id']))
    cvm=json.loads(kernel.measurement(json.dumps(p),p['measurements'][1]['id']))
    a=analyze(cvm,cv_options()); a.pop('plots'); a.update(id=99991,graphs=[])
    p['analyses']=[a]
    o=options(); o.update(measurement_id=am['id'],cv_fit_id=a['id'],xd_source='CV')
    del o['V_b_V']; del o['E_F_eV']
    r=execute(p,dict(operation='walter',settings=o))
    assert r['ok'],r
    result=r['project']['analyses'][-1]; s=result['spectra'][0]
    assert result['V_b_V']==a['V_b_V'] and result['E_F_eV']==a['E_F_eV']
    assert s['x_d']['source']=='CV' and s['x_d']['fit_id']==a['id']
    assert s['x_d']['measurement_id']==cvm['id'] and s['x_d']['bias_V']==0
    assert result['material']['eps_r']==a['material']['eps_r']
    assert s['x_d']['value_m']==pytest.approx((10*8.8541878188e-12*1e-6/9.416067662620894e-11),rel=1e-6)
    q=r['project']; q['measurements'].pop(1)
    assert json.loads(kernel.refresh(json.dumps(q)))['analyses'][-1]['status']=='missing_input'


def test_raw_dlcp_fit_width_reuses_existing_fit_groups():
    from test_m3_analysis import raw, dl_options
    measurement=raw(); measurement['id']=7
    fit=analyze(measurement,dl_options()); fit.update(id=99,sample_id=2)
    o=options(source_analyses=[fit],dlcp_fit_id=99); o['xd_source']='DLCP'
    result=analyze(sweep(),o,measurement)
    xd=result['spectra'][0]['x_d']
    assert xd['measurement_id']==7 and xd['fit_id']==99
    assert xd['bias_V']==0 and xd['value_m']==pytest.approx(8.8541878188e-8,rel=1e-13)


@pytest.mark.parametrize('key,value',[('area_m2',0),('temperature_K',0),('bias_V',1)])
def test_invalid_measurement_metadata(key,value):
    m=sweep(); m[key]=value
    o=options(as_bias_V=0)
    with pytest.raises(ValueError):
        analyze(m,o)


@pytest.mark.parametrize('compat',[False,True])
def test_zero_denominator_is_null_not_a_number(compat):
    t=1.; omega=1.
    prefactor=1.
    p=json.loads(kernel.walter_point(omega,t,prefactor,1,1,1e-6,-1e-6,compat))
    assert p['density_cm3_eV'] is None and p['quality']=='nonpositive_walter_domain'
