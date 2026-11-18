"""M3 goldens come from PFS C1-C5 / thesis table 4.2 and frozen ProPho projects.
Synthetic expectations are literal independent SI examples, never kernel output.
"""
import copy
import json
from pathlib import Path
import numpy as np
import pytest
import sst_caplab
from caplab.analysis import analyze, choose_width
from caplab.importers import parse_measurement, unwrap

FIX = Path(__file__).resolve().parents[2]/'tests/fixtures/m2'
CASES = [
 ('C1', '#139', '#139_Std_5.5mm2/DL/#139_DL_1e4Hz_mp_199K_REL_Umin-2000mV_10022014.dat',199,5.5e-6,[-.30,.35],(.76,3.79e-7,.13,5.93e15),1),
 ('C2', '#138', '#138_Ann150C_9mm2/DL/005_#138_Ann150C_DL_2e3Hz_170K_09092016.dat',170,9e-6,[.15,.35],(.46,8.10e-7,.14,7.23e14),3),
 ('C3', '#143', '#143_Ann200C_3.75mm2/DL/#143_Ann200C_DL_1e4Hz_200K.dat',200,3.75e-6,[-.20,.30],(.42,9.30e-7,.17,5.44e14),2),
 ('C4', '#133', '#133_Ann250C_10.7mm2/DL/#133_DL_1e3Hz_200K_REL_07022014.dat',200,10.7e-6,[0,.25],(.37,1.62e-6,.19,1.53e14),0),
 ('C5', '#132', '#132_Ann300C_3.7mm2/DL/#132_DL_1e3Hz_360K.dat',360,3.7e-6,[0,.25],(.76,5.19e-7,.26,3.15e15),0),
]


def options(**kw):
    return dict(type='CV', material='ProPho-compat', range_V=[-.5,.5], xd_source='manual',
                xd_manual_m=1e-6, xd_description='Independent reference', **kw)


def cv():
    # q=1.602176634e-19, eps=10*8.8541878188e-12, A=1e-6,
    # N=1e21 m^-3, Vb=0.8 V: independently computed C [F].
    return dict(id=1, sample_id=2, area_m2=1e-6, kind='CVSweep', temperature_K=300, frequency_Hz=1000,
        names=['V','C'], units=['V','F'], source_rows=[1,2,3,4,5], row_ids=[11,12,13,14,15],
        columns=[[-.4,-.2,0,.2,.4], [7.688187052314086e-11,8.42198694974315e-11,9.416067662620894e-11,1.0872738399443807e-10,1.3316330592701198e-10]])


def raw(c1=-2e-10, c0=1e-9):
    x=np.array([0,.05,.1,.15,.2,.25])
    return dict(id=1, sample_id=2, area_m2=1e-6, kind='DLCPRaw', temperature_K=300, frequency_Hz=1000,
        names=['V_bias','deltaV','C'], units=['V','V','F'], source_rows=[1,2,3,4,5,6], row_ids=[11,12,13,14,15,16],
        columns=[[0]*6,x.tolist(),(c0+c1*x+3e-11*x*x).tolist()], amplitude_definition='peak',
        constant_maximum_voltage=True, groups=[dict(bias_V=0,temperature_K=300,frequency_Hz=1000,row_ids=[11,12,13,14,15,16])])


def dl_options():
    o=options(); o.update(type='DLCPPolynomial', degree=2, xd_source='DLCP'); return o


@pytest.mark.parametrize('area', [None, 0])
@pytest.mark.parametrize('kind', ['CV', 'DLCP'])
def test_analysis_blocks_missing_or_zero_area(area, kind):
    m = cv() if kind == 'CV' else raw()
    m['area_m2'] = area
    with pytest.raises(ValueError, match='Missing/invalid area_m2'):
        analyze(m, options() if kind == 'CV' else dl_options())


@pytest.mark.parametrize('case', CASES, ids=[c[0] for c in CASES])
def test_pfs_c1_c5_and_saved_project(case):
    ident, sample, file, t, area, limits, expected, index = case
    m=parse_measurement((FIX/'data_Cd'/file).read_text(encoding='utf-8'),file,'propho_dl',',',
        dict(temperature_K=t,frequency_Hz=1000))
    m.update(id=1,sample_id=2,area_m2=area)
    o=options(); o.update(range_V=limits,xd_source='DLCP')
    r=analyze(m,o)
    ours=[r['V_b_V'],r['x_d']['value_m'],r['E_F_eV'],r['N_m3']/1e6]
    # Published precision: Vb/Ef 2 decimal places, xd and Nd 3 significant figures.
    rounded=[float(f'{ours[0]:.2f}'),float(f'{ours[1]:.3g}'),float(f'{ours[2]:.2f}'),float(f'{ours[3]:.3g}')]
    assert rounded == list(expected)
    j=unwrap(json.loads((FIX/'samples_Cd_diffusion.json').read_text()))
    saved=next(s for s in j['samples'] if s['name']==sample)['walter']['fits']
    f=next(f for f in saved if f['dl_idx']==index)
    # Constants explicitly frozen from PFS reconstruction (N_ref ~1.24315e19),
    # historical scipy eps0; no expected quantity is generated with kernel code.
    o.update(material='project',project_material=dict(eps_r=10,N_ref_m3=1.24315e25,source='PFS CV reconstruction',reference_band='compat'),
             constants=dict(eps0_F_m=8.8541878128e-12))
    legacy=analyze(m,o)
    for a,b in zip([legacy['V_b_V'],legacy['x_d']['value_m'],legacy['E_F_eV'],legacy['N_m3']/1e6],
                   [f['Vb'],f['xd'],f['E_finf'],f['Nd']]):
        assert a == pytest.approx(b,rel=1e-6)
    assert np.shape(r['covariance'])==(4,)
    assert len(r['residuals'])==len(r['selected_row_ids'])
    assert len(r['plots'])==4 and len(r['plots'][2]['curves'])==2
    json.dumps(r,allow_nan=False)
    print(ident,ours,'relative_project',[(a-b)/b for a,b in zip(ours,[f['Vb'],f['xd'],f['E_finf'],f['Nd']])])


def test_synthetic_cv():
    r=analyze(cv(),options())
    assert r['V_b_V']==pytest.approx(.8,rel=1e-6)
    assert r['N_m3']==pytest.approx(1e21,rel=1e-6)
    assert r['profile']['N_m3']==pytest.approx([1e21]*5,rel=1e-6)


def test_synthetic_dlcp():
    r=analyze(raw(),dl_options()); g=r['groups'][0]
    assert g['coefficients']==pytest.approx([1e-9,-2e-10,3e-11],rel=1e-6)
    assert g['x_e_m']==pytest.approx(8.8541878188e-8,rel=1e-6)
    assert g['N_DL_m3']==pytest.approx(1.762304234502524e23,rel=1e-6)
    assert r['x_d']['value_m']==pytest.approx(g['x_e_m'])
    assert np.shape(g['covariance'])==(3,3)
    assert max(abs(v) for v in g['residuals'])<1e-23


@pytest.mark.parametrize('definition,factor',[('RMS',2**.5),('peak-to-peak',.5)])
def test_amplitude_conversions(definition,factor):
    m=raw(); m['amplitude_definition']=definition; m['columns'][1]=[x/factor for x in m['columns'][1]]
    assert analyze(m,dl_options())['groups'][0]['coefficients']==pytest.approx([1e-9,-2e-10,3e-11],rel=1e-6)


def test_zero_c1_and_nonpositive_c0():
    for m,status in [(raw(c1=0),'invalid_C1'),(raw(c0=-1e-9),'invalid_C0')]:
        o=dl_options(); o.update(xd_source='manual')
        g=analyze(m,o)['groups'][0]
        assert g['status']==status and g['N_DL_m3'] is None


@pytest.mark.parametrize('kind',['CV','DLCP'])
def test_rank_deficient(kind):
    m=cv() if kind=='CV' else raw()
    m['columns'][0 if kind=='CV' else 1]=[0]*len(m['source_rows'])
    with pytest.raises(ValueError): analyze(m,options() if kind=='CV' else dl_options())


def test_invalid_amplitude_unit():
    m=raw(); m['units'][1]='mV'
    with pytest.raises(ValueError,match='SI unit'): analyze(m,dl_options())


def test_unknown_amplitude_and_conditional_measurement():
    m=raw(); m['amplitude_definition']=None
    with pytest.raises(ValueError,match='amplitude definition'): analyze(m,dl_options())
    m['amplitude_definition']='peak'; m['constant_maximum_voltage']=None
    assert analyze(m,dl_options())['status']=='conditional'


@pytest.mark.parametrize('target',[-2,2])
def test_missing_zero_bias_no_implicit_extrapolation(target):
    with pytest.raises(ValueError): choose_width([-1,1],[1e-6,3e-6],target,'block')
    with pytest.raises(ValueError,match='extrapolation'): choose_width([-1,1],[1e-6,3e-6],target,'linear')
    value,warning=choose_width([-1,1],[1e-6,3e-6],target,'nearest')
    assert value==(1e-6 if target<0 else 3e-6) and warning


def test_missing_zero_and_linear():
    with pytest.raises(ValueError): choose_width([-.1,.1],[1e-6,3e-6],0,'block')
    value,warning=choose_width([-.1,.1],[1e-6,3e-6],0,'linear')
    assert value==pytest.approx(2e-6) and warning


def test_missing_as_bias_and_exact_cv_width():
    o=options(); o.update(xd_source='CV')
    with pytest.raises(ValueError,match='AS bias'): analyze(cv(),o)
    o['as_bias_V']=0
    r=analyze(cv(),o)
    assert r['x_d']['policy']=='block' and r['x_d']['measurement_id']==1


def test_masks_and_two_point_covariance():
    m=cv(); o=options(); o['excluded_row_ids']=[12,13,14]
    r=analyze(m,o)
    assert r['dof']==0 and r['covariance']==[None]*4
    assert r['selected_row_ids']==[11,15]
    assert r['N_m3']==pytest.approx(1e21,rel=1e-6)


def test_materials_and_cancellation():
    o=options(); o['material']=None
    with pytest.raises(ValueError,match='material'): analyze(cv(),o)
    with pytest.raises(RuntimeError,match='cancelled'): analyze(cv(),options(),cancelled=lambda: True)
    o=options(); o.update(material='Si',reference_band='conduction')
    assert analyze(cv(),o)['material']['N_ref_m3']>0


def test_degree_one_and_no_dof():
    m=raw(); m['columns'][2]=[1e-9-2e-10*x for x in m['columns'][1]]
    o=dl_options(); o['degree']=1
    assert analyze(m,o)['groups'][0]['coefficients']==pytest.approx([1e-9,-2e-10],rel=1e-6)


def test_refresh_area_and_missing_input():
    from caplab import project_from_preview
    from caplab.importers import import_preview
    preview=import_preview((FIX/'#132_DL_1e3Hz_360K.dat').read_text(), 'dl.dat', options=dict(profile='propho_dl',temperature_K=360,frequency_Hz=1000,area_m2=3.7e-6))
    p=project_from_preview(preview); mid=p['measurements'][0]['id']
    m=json.loads(sst_caplab.measurement(json.dumps(p),mid)); o=options(); o['range_V']=[0,.25]
    a=analyze(m,o); a.pop('plots'); a.update(id=99999,graphs=[])
    p['analyses']=[a]
    p['samples'][0]['area_m2']*=2
    assert json.loads(sst_caplab.refresh(json.dumps(p)))['analyses'][0]['status']=='stale'
    p['measurements']=[]
    assert json.loads(sst_caplab.refresh(json.dumps(p)))['analyses'][0]['status']=='missing_input'

def test_dlcp_covariance_unavailable_without_degrees_of_freedom():
    m=raw(); m['columns']=[c[:3] for c in m['columns']]; m['source_rows']=m['source_rows'][:3]; m['row_ids']=m['row_ids'][:3]
    m['groups'][0]['row_ids']=m['row_ids']
    g=analyze(m,dl_options())['groups'][0]
    assert g['dof']==0 and g['covariance'] is None


def test_nonphysical_cv_sign_remains_visible():
    m=cv(); m['columns'][1].reverse()
    r=analyze(m,options())
    assert r['N_m3']<0 and r['status']=='invalid'
    assert all(q=='nonphysical_density' for q in r['profile']['quality'])

def test_imported_cv_normalization_uses_original_area():
    m=parse_measurement((FIX/'#132_DL_1e3Hz_360K.dat').read_text(), 'dl.dat', 'propho_dl',
                        options=dict(temperature_K=360,frequency_Hz=1000))
    m.update(id=1,sample_id=2,area_m2=3.7e-6,normalization_area_m2=3.7e-6)
    o=options(); o['range_V']=[0,.25]
    before=analyze(m,o)
    m['area_m2']*=2
    after=analyze(m,o)
    assert after['N_m3']==pytest.approx(before['N_m3']/4)
    assert after['profile']['W_m']==pytest.approx(np.array(before['profile']['W_m'])*2)

def test_sourced_material_catalog_and_owner_preset():
    preset=json.loads(sst_caplab.compat_material())
    assert preset['eps_r']['value']==10 and preset['N_ref_m3']['value']==1.24e25
    assert all(item['source'] for item in preset.values())
    catalog=json.loads(sst_caplab.materials(300))
    si=next(m for m in catalog if m['name']=='Si')
    assert si['eps_r']==11.9 and si['Nc_m3']==pytest.approx(2.8e25)
    assert si['eps_source'] and si['Nc_source']
