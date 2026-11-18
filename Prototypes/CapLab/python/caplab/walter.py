"""Walter selection, PCF mask-before-gradient, SST-Physics kernels and quadrature.
Saved spectra replay does not reconstruct the unresolved historical GUI.
"""
import json
import math
import numpy as np
import sst_caplab as kernel
from .analysis import column, positive, material, resolve_width, plot


def _range(options, key, default):
    lo, hi = options.get(key, default)
    if not math.isfinite(lo) or not math.isfinite(hi) or lo >= hi:
        raise ValueError('Invalid '+key)
    return lo, hi


def analyze_walter(m, options, xd_measurement, cancelled, progress):
    if cancelled():
        raise RuntimeError('Walter cancelled')
    saved = options['type'] == 'WalterSaved'
    model = 'ProPho-compat' if saved else options.get('model', 'Walter1996-linear-field')
    if model not in ('Walter1996-linear-field', 'ProPho-compat'):
        raise ValueError('Unknown Walter model')
    compat = model == 'ProPho-compat'
    result = dict(type=options['type'], model=model, kernel_version=1, settings=options,
                  constants=json.loads(kernel.constants()), inputs=[], status='valid', computed_status='valid',
                  diagnostics=[], spectra=[], plots=[])
    if saved:
        h = options['historical']
        w = h['data']['walter']
        spectrum = w.get('dist_for_integration')
        if not spectrum:
            raise ValueError('Missing frozen historical integration slice; GUI reconstruction unsupported')
        result.update(sample_id=h['sample_id'], historical=h, compatibility='saved-spectrum-integral-only')
        energy, density = spectrum['E_w'], spectrum['N_T']
        lo, hi = _range(options, 'integration_range_eV', w['saved_integration_range'])
        integral = json.loads(kernel.walter_integral(energy, density, lo, hi, True))
        result['spectra'] = [dict(energy_eV=energy, density_cm3_eV=density,
            quality=['valid' if v is not None else 'historical_nonfinite' for v in density],
            integral=integral, saved_selection=w.get('selections', w.get('saved_idx')),
            measurement_id=spectrum.get('measurement_id'), saved_range_eV=w['saved_integration_range'])]
        result['diagnostics'].append('Historical slice replay only; full ProPho spectrum reconstruction unsupported')
    else:
        measurements = options.get('measurements', [m])
        if not measurements or len({v['id'] for v in measurements}) != len(measurements):
            raise ValueError('Select distinct Walter measurements')
        result.update(sample_id=m['sample_id'], source_analyses=options.get('source_analyses', []))
        vb, ef = options.get('V_b_V'), options.get('E_F_eV')
        prefactor = options.get('B_s_1' if compat else 'zeta_s_1_K_2')
        for a in result['source_analyses']:
            sample = a.get('sample_id', a['inputs'][0]['sample_id'] if a['inputs'] else None)
            if sample != m['sample_id'] or a['status'] != 'valid':
                raise ValueError('Walter source analysis must be valid and belong to the same sample')
            if a['type'] == 'CV':
                vb, ef = a['V_b_V'], a['E_F_eV']
            elif a['type'] == 'Arrhenius' and not compat:
                fit = next((v for v in a['fits'] if v['model'] == 'omega-T2'), None)
                if fit is None:
                    raise ValueError('Walter1996 requires omega-T2 Arrhenius prefactor')
                prefactor = fit['prefactor']
            for v in a['inputs']:
                if v not in result['inputs']:
                    result['inputs'].append(v)
        positive(vb, 'V_b_V'); positive(prefactor, 'B_s_1' if compat else 'zeta_s_1_K_2')
        if ef is None or not math.isfinite(ef):
            raise ValueError('Missing/invalid E_F_eV')
        if not options.get('parameter_source') and (not options.get('cv_fit_id') or (not compat and not options.get('arrhenius_fit_id'))):
            raise ValueError('Manual Walter parameters require parameter_source')
        if compat and not options.get('emission_source'):
            raise ValueError('Explicit B and emission_source required; never infer B from a compat fit')
        if options.get('xd_policy', 'block') not in ('block', 'linear', 'nearest'):
            raise ValueError('Unknown x_d policy')
        result['constants']['u_J_eV'] = result['constants']['q_C']
        result.update(V_b_V=vb, E_F_eV=ef,
                      emission_unit='s^-1' if compat else 's^-1 K^-2')
        result['B_s_1' if compat else 'zeta_s_1_K_2'] = prefactor
        tlo, thi = _range(options, 'temperature_range_K', [0, 1e6])
        flo, fhi = _range(options, 'frequency_range_Hz', [0, 1e30])
        elo, ehi = _range(options, 'energy_range_eV', [-1e6, 1e6])
        all_rows = {r for v in measurements for r in v.get('row_ids', v['source_rows'])}
        if not set(options.get('excluded_row_ids', [])).issubset(all_rows):
            raise ValueError('Unknown excluded row ID')
        for mi, measurement in enumerate(measurements):
            if cancelled():
                raise RuntimeError('Walter cancelled')
            if measurement['sample_id'] != m['sample_id']:
                raise ValueError('Walter measurements must belong to the same sample')
            t = positive(measurement.get('temperature_K'), 'temperature_K')
            if not tlo <= t <= thi:
                continue
            if measurement['kind'] != 'FrequencySweep' or measurement['variant'] not in ('Cp_Rp', 'C_G'):
                raise ValueError('Walter requires AS capacitance')
            for name, unit in [('f', 'Hz'), ('C', 'F')]:
                if measurement['units'][measurement['names'].index(name)] != unit:
                    raise ValueError('Invalid SI unit for '+name)
            area = positive(measurement.get('area_m2'), 'area_m2')
            f, c = column(measurement, 'f'), column(measurement, 'C')
            rows = measurement.get('row_ids', measurement['source_rows'])
            excluded = set(options.get('excluded_row_ids', []))
            if len(f) != len(c) or len(rows) != len(f) or np.any(f <= 0) or len(set(rows)) != len(rows):
                raise ValueError('Invalid Walter frequency/row arrays')
            indices = sorted((i for i in range(len(f)) if flo <= f[i] <= fhi and rows[i] not in excluded), key=lambda i: f[i])
            if len(indices) < 2 or len(set(f[indices])) != len(indices):
                raise ValueError('Walter derivative needs at least two distinct selected frequencies')
            f, c = f[indices], c[indices]/area
            omega = 2*math.pi*f
            derivative = np.gradient(c, np.log(omega), edge_order=1)
            width_options = dict(options)
            for source_fit in result['source_analyses']:
                if source_fit['type'] == 'DLCPPolynomial':
                    width_options['dlcp_fit'] = source_fit
            bias = measurement.get('bias_V')
            if bias is not None:
                if options.get('as_bias_V', bias) != bias:
                    raise ValueError('AS bias confirmation conflicts with measurement')
                width_options['as_bias_V'] = bias
            epsilon = None
            if options.get('xd_source', 'CV') == 'CV':
                mat = material(options, t)
                epsilon = mat['eps_r']*result['constants']['eps0_F_m']
                result['material'] = mat
            width_options.setdefault('xd_source', 'CV')
            xd = resolve_width(measurement, width_options, xd_measurement, epsilon, result)
            xd['fit_id'] = options.get('cv_fit_id') if xd['source'] == 'CV' else options.get('dlcp_fit_id') if xd['source'] == 'DLCP' else None
            if measurement not in result['inputs']:
                result['inputs'].append(measurement)
            points = [json.loads(kernel.walter_point(float(w), t, prefactor, vb, ef, xd['value_m'], float(d), compat))
                      for w, d in zip(omega, derivative)]
            keep = [i for i, p in enumerate(points) if elo <= p['energy_eV'] <= ehi]
            if len(keep) < 2:
                raise ValueError('Energy range contains fewer than two spectrum samples')
            energy = [points[i]['energy_eV'] for i in keep]
            density = [points[i]['density_cm3_eV'] for i in keep]
            lo, hi = _range(options, 'integration_range_eV', [min(energy), max(energy)])
            integral = json.loads(kernel.walter_integral(energy, density, lo, hi, compat))
            result['spectra'].append(dict(measurement_id=measurement['id'], temperature_K=t,
                row_ids=[rows[indices[i]] for i in keep], frequency_Hz=f[keep].tolist(), x_d=xd,
                energy_eV=energy, density_cm3_eV=density, D_c_F_m2=derivative[keep].tolist(),
                field_factor_V_m_C=[points[i]['field_factor_V_m_C'] for i in keep], quality=[points[i]['quality'] for i in keep], integral=integral))
            progress((mi+1)/len(measurements))
        if not result['spectra']:
            raise ValueError('No spectra in selected temperature range')
    if any(any(q != 'valid' for q in s['quality']) or s['integral']['density_cm3'] is None for s in result['spectra']):
        result['computed_status'] = result['status'] = 'invalid'
        result['diagnostics'].append('Invalid domain / unavailable integral; inspect point quality')
    # Null samples stay in the plot: the engine breaks the line at missing cells.
    result['plots'] = [plot('Walter '+model, 'Energy (eV)', 'N_T (cm^-3 eV^-1)',
        *[dict(name=str(s.get('temperature_K', 'historical'))+' K', x=s['energy_eV'], y=s['density_cm3_eV']) for s in result['spectra']])]
    if cancelled():
        raise RuntimeError('Walter cancelled')
    progress(1.)
    return result
