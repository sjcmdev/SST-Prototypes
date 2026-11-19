"""AS preprocessing (PFS AS; PCF Page1_AS.plot_as_data) and SST-Physics fits.
Raw data always remains available. Default D is numpy.gradient(C, ln(f)),
edge_order=1, including unequal spacing. Candidate approval belongs to the user.
"""
import math
import numpy as np
from scipy.signal import find_peaks, savgol_filter
import sst_caplab as kernel
import json
from .analysis import column, positive, plot, curve, _standard_error


def preprocess(m, smoothing=None):
    if m['kind'] != 'FrequencySweep' or m['variant'] not in ('Cp_Rp', 'C_G'):
        raise ValueError('AS requires FrequencySweep with C and Cp/Rp or C/G')
    for name, unit in [('f', 'Hz'), ('C', 'F')]:
        if m['units'][m['names'].index(name)] != unit:
            raise ValueError(f'Invalid SI unit for {name}')
    f, c = column(m, 'f'), column(m, 'C')
    rows = m.get('row_ids', m['source_rows'])
    if len(f) < 2 or len(f) != len(c) or len(f) != len(rows) or len(set(rows)) != len(rows) or np.any(f <= 0):
        raise ValueError('AS needs >=2 positive frequencies and matching unique row IDs')
    order = np.argsort(f, kind='stable'); f, c = f[order], c[order]
    rows = [rows[i] for i in order]
    if np.any(np.diff(f) == 0):
        raise ValueError('Resolve duplicate frequencies before derivative')
    raw = np.gradient(c, np.log(f), edge_order=1)
    smooth = dict(smoothing or {'method': 'none'})
    processed = c
    if smooth['method'] == 'Savitzky-Golay':
        window, degree = smooth['window'], smooth['degree']
        if not isinstance(window, int) or not isinstance(degree, int) or window < 3 or window % 2 != 1 or window > len(c) or not 0 <= degree < window:
            raise ValueError('Invalid Savitzky-Golay window/degree')
        # Filter samples in acquisition index space; derivative still uses ln(f).
        processed = savgol_filter(c, window, degree)
    elif smooth['method'] != 'none':
        raise ValueError('Unknown smoothing method')
    d = np.gradient(processed, np.log(f), edge_order=1)
    indices = find_peaks(-d, distance=2)[0]
    candidates = []
    log_f = np.log(f)
    for i in indices:
        # Refine within the bracketing bins; the row ID keeps the source anchor.
        a, b, c0 = np.polyfit(log_f[i-1:i+2] - log_f[i], d[i-1:i+2], 2)
        offset = -b / (2*a) if a > 0 else 0.
        if not log_f[i-1] - log_f[i] <= offset <= log_f[i+1] - log_f[i]:
            offset = 0.
        candidates.append(dict(measurement_id=m['id'], row_id=rows[i], temperature_K=m.get('temperature_K'),
            frequency_Hz=float(np.exp(log_f[i] + offset)), D_F=float(a*offset**2+b*offset+c0),
            provenance='AS candidate; three-point log-frequency refinement'))
    diagnostic = []
    name, unit = ('Rp', 'Ohm') if m['variant'] == 'Cp_Rp' else ('G', 'S')
    if m['units'][m['names'].index(name)] != unit:
        raise ValueError(f'Invalid SI unit for {name}')
    g = column(m, name)[order]
    if len(g) != len(f):
        raise ValueError('Unequal conductance/resistance length')
    if name == 'Rp':
        if np.any(g <= 0):
            diagnostic.append('Nonpositive Rp: G/omega unavailable at affected rows')
        g = np.divide(1., g, out=np.full_like(g, np.nan), where=g > 0)
    go = g/(2*math.pi*f)
    return dict(frequency_Hz=f.tolist(), capacitance_F=c.tolist(), processed_capacitance_F=processed.tolist(),
        D_F=d.tolist(), raw_D_F=raw.tolist(), row_ids=rows, smoothing=smooth,
        G_over_omega_F=[float(v) if math.isfinite(v) else None for v in go], diagnostics=diagnostic,
        candidates=candidates)


def analyze_as(m, options, cancelled=lambda: False, progress=lambda v: None):
    if cancelled():
        raise RuntimeError('Analysis cancelled')
    result = dict(type=options['type'], kernel_version=1, settings=options, inputs=[],
        status='valid', computed_status='valid', diagnostics=[], plots=[],
        constants=json.loads(kernel.constants()))
    if options['type'] == 'AS':
        r = preprocess(m, options.get('smoothing'))
        result.update(r, inputs=[m], sample_id=m['sample_id'], model='AS-ln-frequency-gradient')
        f = r['frequency_Hz']; t = str(m.get('temperature_K'))+' K'
        result['plots'] = [plot('C-f '+t, 'Frequency (Hz)', 'Capacitance (F)', curve('raw '+t, f, r['capacitance_F'])),
            plot('D-f '+t, 'Frequency (Hz)', 'dC/dln(f) (F)',
                curve('candidates '+t, [p['frequency_Hz'] for p in r['candidates']], [p['D_F'] for p in r['candidates']]),
                curve('raw '+t, f, r['raw_D_F'])),
            plot('G/omega '+t, 'Frequency (Hz)', 'G/omega (F)', curve(t, f, [v if v is not None else np.nan for v in r['G_over_omega_F']]))]
        if r['smoothing']['method'] != 'none':
            result['plots'][0]['curves'].append(curve('smoothed '+t, f, r['processed_capacitance_F']))
            result['plots'][1]['curves'].append(curve('smoothed '+t, f, r['D_F']))
        for p in result['plots']:
            p['log_x'] = True
        # Shared screen picking breaks equal-distance ties in series order.
        result['plots'][1]['curves'][0]['candidates'] = True
    else:
        defect = options['defect']
        points = [p for p in defect['points'] if p['enabled']]
        t = [positive(p.get('temperature_K'), 'temperature_K') for p in points]
        f = [positive(p.get('frequency_Hz'), 'frequency_Hz') for p in points]
        if len(set(t)) < 2:
            raise ValueError('Arrhenius requires at least two distinct temperatures')
        models = options.get('models', ['omega-T2'])
        if not models or len(set(models)) != len(models) or any(v not in ('omega-T2', 'ProPho-compat') for v in models):
            raise ValueError('Select valid distinct Arrhenius models')
        result.update(sample_id=defect['sample_id'], defect=defect, model='Arrhenius-comparison', fits=[])
        result['inputs'] = options.get('point_inputs', [])
        for model in models:
            compat = model == 'ProPho-compat'
            rates = f if compat else [2*math.pi*v for v in f]
            fit = json.loads(kernel.arrhenius_fit(t, rates, compat))
            x, y = np.asarray(fit['inverse_temperature_K']), np.asarray(fit['log_rate'])
            predicted = fit['slope']*x+fit['intercept']
            fit.update(model=model, prefactor_unit='Hz' if compat else 's^-1 K^-2',
                sigma_cm2=None, residuals=(y-predicted).tolist(), point_ids=[p['id'] for p in points],
                covariance_order=['E_a_eV', 'prefactor'], linear_covariance_order=['intercept', 'slope'],
                covariance_scaling='relative residual variance; unavailable when dof=0')
            cov = fit['covariance']
            fit.update(E_a_standard_error_eV=_standard_error(cov, [1., 0.]),
                prefactor_standard_error=_standard_error(cov, [0., 1.]),
                uncertainty_scope='1 standard error from residual covariance; temperature and frequency calibration held fixed')
            if compat:
                assumptions = dict(Ncv_cm3=2e18, vth_cm_s=1e7, source='PCF ASCalc.calc_defect_params; PFS AS')
                fit['assumptions'] = assumptions
                fit['sigma_cm2'] = kernel.compat_sigma(fit['prefactor'], assumptions['Ncv_cm3'], assumptions['vth_cm_s'])
            elif options.get('gamma_cm_2_s_K_2') is not None:
                gamma = positive(options['gamma_cm_2_s_K_2'], 'gamma')
                if not options.get('gamma_source'):
                    raise ValueError('Explicit gamma source required')
                fit['sigma_cm2'] = fit['prefactor']/gamma
                fit['assumptions'] = dict(gamma_cm_2_s_K_2=gamma, source=options['gamma_source'])
            if fit['dof'] == 0:
                result['diagnostics'].append(model+': two points; no residual degrees of freedom, covariance unavailable')
            result['fits'].append(fit)
            label = 'ln(f / Hz)' if compat else 'ln(omega / T^2 [s^-1 K^-2])'
            result['plots'].extend([plot(defect['name']+' '+model, '1000/T (K^-1)', label,
                curve(defect['name'], 1000*x, y), curve('fit', 1000*x, predicted, True)),
                plot(defect['name']+' '+model+' residuals', '1000/T (K^-1)', 'Log rate residual', curve('residuals', 1000*x, y-predicted))])
    if cancelled():
        raise RuntimeError('Analysis cancelled')
    progress(1.)
    return result
