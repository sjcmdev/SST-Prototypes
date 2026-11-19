"""Headless product objective coverage; run under the embedded Python host."""
import json
import numpy as np
import pytest
from test_scaps_gold1 import PHASE4, curve

@pytest.mark.parametrize('mode',['isc_shift','signed_sigma'])
@pytest.mark.parametrize('name',[name for name in PHASE4.files if '-light-' in name])
def test_phase4_final_fit_accepts_signed_light_data(name,mode):
    sst_jfm=pytest.importorskip('sst_jfm',reason='Product nanobind module requires embedded Python')
    a,t=curve(name)
    isc=-float(np.interp(0,a[:,0],a[:,1]));voc=float(np.interp(0,a[:,1],a[:,0]))
    rs=float(name.split('-rs')[1].split('-')[0]);rsh=name.split('-rsh')[1]
    thermal=1.1*8.617333262145e-5*t
    q=dict(voltage_V=a[:,0].tolist(),current_A=a[:,1].tolist(),temperature_K=t,
        model_id='SD-RsRsh-I_ph',objective_mode=mode,noise_percent=1.,max_iterations=5000,
        parameters=dict(I0_A=max(1e-20,isc*np.exp(-(voc-rs*isc)/thermal)),ideality_factor=1.1,
                        Rs_ohm=max(rs,1e-5),Rsh_ohm=1e10 if rsh=='off' else float(rsh)+rs,Iph_A=isc))
    result=sst_jfm.fit(q)
    assert result['ok'],result
    assert result['accepted'],result
    assert result['objective']['mode']==mode
    assert result['objective']['sigma_abs_A']>0
    assert result['objective']['linear_points']+result['objective']['log_points']==len(a)
    json.dumps(result,allow_nan=False)
