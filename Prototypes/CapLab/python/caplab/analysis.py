"""M3 preprocessing and NumPy polynomial fit; all physical equations use SST-Physics.
References: PFS CV/DLCP; Capacitance Spectroscopy of Thin-Film Solar Cells,
p.98 eq.4.1 and pp.110-111 eqs.4.15-4.16.
"""
import json
import math
import numpy as np
import sst_caplab as kernel

SI = dict(N_DL='m^-3', V='V', C='F', deltaV='V', V_bias='V', A2_C02='m^4/F^2', W='m')



def column(m, name):
    i = m['names'].index(name)
    if name in SI and m['units'][i] != SI[name]:
        raise ValueError(f'Invalid SI unit for {name}')
    a = np.asarray(m['columns'][i], dtype=float)
    if a.ndim != 1 or not np.all(np.isfinite(a)):
        raise ValueError(f'Nonfinite {name}')
    return a


def positive(value, name):
    if value is None or not math.isfinite(value) or value <= 0:
        raise ValueError(f'Missing/invalid {name}')
    return float(value)


def material(options, temperature):
    name = options.get('material')
    if name == 'ProPho-compat':
        preset=json.loads(kernel.compat_material())
        return dict(name=name, eps_r=preset['eps_r']['value'], N_ref_m3=preset['N_ref_m3']['value'],
                    source=preset['eps_r']['source'], reference_source=preset['N_ref_m3']['source'], reference_band='compat')
    if name == 'project':
        m = dict(options['project_material'])
        positive(m.get('eps_r'), 'eps_r')
        if not m.get('source'):
            raise ValueError('Project material requires a source')
        m['name'] = name
        if m.get('N_ref_m3') is not None:
            positive(m['N_ref_m3'], 'N_ref_m3')
            if m.get('reference_band') not in ('conduction', 'valence', 'compat'):
                raise ValueError('Select reference band')
        return m
    found = next((m for m in json.loads(kernel.materials(temperature or 300.)) if m['name'] == name), None)
    if found is None:
        raise ValueError('Select a material; missing values block analysis')
    band = options.get('reference_band')
    ref = found.get('Nc_m3' if band == 'conduction' else 'Nv_m3') if band in ('conduction', 'valence') else None
    return dict(name=name, eps_r=found['eps_r'], source=found['eps_source'], reference_band=band,
                N_ref_m3=ref if ref and temperature else None,
                reference_source=found.get('Nc_source' if band == 'conduction' else 'Nv_source'))


def cv_data(m, area):
    v = column(m, 'V')
    if m['kind'] == 'CVSweep':
        c = column(m, 'C')
        if np.any(c <= 0):
            raise ValueError('Nonpositive CV capacitance')
        y = np.array([kernel.area_inverse_square(float(ci), area) for ci in c])
    elif m['kind'] in ('DLCPProfile', 'CV-derived'):
        y = column(m, 'A2_C02')
        if np.any(y <= 0):
            raise ValueError('Nonpositive CV inverse capacitance')
        source_area = positive(m.get('normalization_area_m2') or area, 'normalization area')
        c = np.array([kernel.capacitance_from_inverse_square(float(yi), source_area) for yi in y])
        y = np.array([kernel.area_inverse_square(float(ci), area) for ci in c])
    else:
        raise ValueError('Select CV or imported DL/CV')
    return v, c, y


def choose_width(v, w, target, policy):
    if policy not in ('block', 'linear', 'nearest'):
        raise ValueError('Unknown x_d policy')
    v, w = np.asarray(v), np.asarray(w)
    if len(v) != len(w) or not len(v) or not np.all(np.isfinite(v)) or not np.all(np.isfinite(w)) or np.any(w <= 0):
        raise ValueError('Invalid x_d profile')
    order = np.argsort(v); v, w = v[order], w[order]
    if len(np.unique(v)) != len(v):
        raise ValueError('Ambiguous duplicate voltages for x_d')
    exact = np.flatnonzero(v == target)
    if len(exact):
        return float(w[exact[0]]), None
    if policy == 'block':
        raise ValueError('Missing exact V=0 / AS bias point for x_d')
    if policy == 'nearest':
        return float(w[np.argmin(abs(v-target))]), 'Nearest measured voltage used for x_d'
    if not v[0] < target < v[-1]:
        raise ValueError('x_d extrapolation is forbidden')
    return float(np.interp(target, v, w)), 'Linear interpolation used for x_d'


def plot(name, x_label, y_label, *curves):
    return dict(name=name, x_label=x_label, y_label=y_label, curves=list(curves))


def curve(name, x, y, fit=False):
    pairs = [(float(a), float(b)) for a, b in zip(x, y) if np.isfinite(a) and np.isfinite(b)]
    return dict(name=name, x=[a for a, b in pairs], y=[b for a, b in pairs], fit=fit)


def analyze(m, options, xd_measurement=None, cancelled=lambda: False, progress=lambda v: None):
    if cancelled():
        raise RuntimeError('Analysis cancelled')
    if options['type'] in ('Walter', 'WalterSaved'):
        from .walter import analyze_walter
        return analyze_walter(m, options, xd_measurement, cancelled, progress)
    if options['type'] in ('AS', 'Arrhenius'):
        from .admittance import analyze_as
        return analyze_as(m, options, cancelled, progress)
    area = positive(m.get('area_m2'), 'area_m2')
    mat = material(options, m.get('temperature_K'))
    constants = dict(json.loads(kernel.constants()), **options.get('constants', {}))
    for k in ('q_C', 'eps0_F_m', 'kappa_eV_K'):
        positive(constants[k], k)
    epsilon = positive(mat['eps_r'], 'eps_r')*constants['eps0_F_m']
    inputs = [m]
    result = dict(type=options['type'], model='CV-linear' if options['type'] == 'CV' else 'DLCP-polynomial',
                  kernel_version=1, settings=options, constants=constants, material=mat, inputs=inputs,
                  status='valid', computed_status='valid', diagnostics=[], plots=[])
    excluded = set(options.get('excluded_row_ids', []))
    row_ids = m.get('row_ids', m['source_rows'])
    if not excluded.issubset(set(row_ids)):
        raise ValueError('Mask contains unknown row IDs')
    if options['type'] == 'CV':
        v, c, y = cv_data(m, area)
        lo, hi = options['range_V']
        if not math.isfinite(lo) or not math.isfinite(hi) or lo > hi:
            raise ValueError('Invalid voltage range')
        mask = np.array([lo <= x <= hi and r not in excluded for x, r in zip(v, row_ids)])
        vx, yx = v[mask], y[mask]
        if len(vx) < 2 or len(np.unique(vx)) != len(vx):
            raise ValueError('CV needs distinct voltages in one sweep direction')
        steps = np.diff(vx)
        if not (np.all(steps > 0) or np.all(steps < 0)):
            raise ValueError('Select a single CV sweep direction')
        f = json.loads(kernel.cv_linear_fit(vx.tolist(), yx.tolist()))
        slope, intercept = f['slope'], f['intercept']
        if slope == 0:
            raise ValueError('Zero CV slope')
        density = kernel.cv_density(slope, epsilon, constants['q_C'])
        fitted = intercept+slope*vx
        residuals = yx-fitted
        width = np.array([kernel.width(ci, area, epsilon) for ci in c])
        # gradient follows acquisition order, never mixes opposite directions.
        derivative = np.gradient(yx, vx, edge_order=1)
        profile = [kernel.cv_density(float(d), epsilon, constants['q_C']) if d != 0 else None for d in derivative]
        result.update(f, V_b_V=-intercept/slope, N_m3=density, E_F_eV=None,
                      selected_row_ids=[r for r, use in zip(row_ids, mask) if use], residuals=residuals.tolist(),
                      profile=dict(W_m=width[mask].tolist(), N_m3=profile,
                          quality=['zero_slope' if n is None else 'nonphysical_density' if n <= 0 else 'valid' for n in profile]),
                      residual_unit='m^4/F^2', coefficient_order=['intercept','slope'],
                      covariance_scaling='relative residual variance; null when dof=0')
        if density <= 0:
            result['diagnostics'].append('Nonphysical CV density sign')
            result['status'] = result['computed_status'] = 'invalid'
        elif mat.get('N_ref_m3') is not None:
            result['E_F_eV'] = kernel.fermi_energy(positive(m.get('temperature_K'), 'temperature_K'),
                mat['N_ref_m3'], density, constants['kappa_eV_K'])
        result['plots'] = [plot('C-V', 'Voltage (V)', 'Capacitance (F)', curve('CV', v, c)),
            plot('CV fit', 'Voltage (V)', 'A2/C2 (m4/F2)', curve('measured', vx, yx), curve('fit', vx, fitted, True)),
            plot('CV profile', 'Width (m)', 'N_CV (m^-3)', curve('CV', width[mask], [n if n is not None else np.nan for n in profile])),
            plot('CV residuals', 'Voltage (V)', 'Residual (m4/F2)', curve('residuals', vx, residuals))]
        if m['kind'] == 'DLCPProfile':
            result['plots'][2]['curves'].append(curve('DLCP', column(m,'W'), column(m,'N_DL')))
    elif options['type'] == 'DLCPPolynomial':
        if m['kind'] != 'DLCPRaw':
            raise ValueError('Select raw DLCP')
        degree = options.get('degree', 2)
        if degree not in (1, 2):
            raise ValueError('DLCP degree must be 1 or 2')
        definition = m.get('amplitude_definition')
        if definition not in ('peak', 'RMS', 'peak-to-peak'):
            raise ValueError('Explicit amplitude definition required')
        factor = {'peak': 1., 'RMS': math.sqrt(2), 'peak-to-peak': .5}[definition]
        x, c = column(m, 'deltaV')*factor, column(m, 'C')
        if np.any(x < 0):
            raise ValueError('Negative amplitude')
        groups = m['groups']; results = []
        for gi, group in enumerate(groups):
            if cancelled():
                raise RuntimeError('Analysis cancelled')
            ids = group.get('row_ids', [row_ids[i] for i in group.get('rows', [])])
            indices = [row_ids.index(r) for r in ids if r not in excluded]
            lo, hi = options.get('range_amplitude_V', [0, float(max(x))])
            indices = [i for i in indices if lo <= x[i] <= hi]
            xx, yy = x[indices], c[indices]
            if len(xx) < degree+1 or len(np.unique(xx)) < degree+1:
                raise ValueError('Rank deficient DLCP fit')
            scale = positive(float(max(xx)), 'amplitude scale')
            design = np.vander(xx/scale, degree+1, increasing=True)
            coef, _, rank, _ = np.linalg.lstsq(design, yy, rcond=None)
            if rank != degree+1:
                raise ValueError('Rank deficient DLCP fit')
            fitted = design@coef; residual = yy-fitted
            powers = scale**np.arange(degree+1); coef /= powers
            dof = len(xx)-degree-1
            cov = (np.linalg.inv(design.T@design)*float(residual@residual)/dof / np.outer(powers, powers)).tolist() if dof > 0 else None
            g = dict(bias_V=group['bias_V'], temperature_K=group['temperature_K'], frequency_Hz=group['frequency_Hz'],
                coefficients=coef.tolist(), coefficient_units=['F','F/V','F/V^2'][:degree+1],
                covariance=cov, covariance_scaling='relative residual variance; null when dof=0',
                dof=dof, residuals=residual.tolist(), residual_unit='F',
                row_ids=[row_ids[i] for i in indices], x_e_m=None, N_DL_m3=None, status='valid')
            if coef[0] <= 0:
                g['status'] = 'invalid_C0'
            else:
                g['x_e_m'] = kernel.width(float(coef[0]), area, epsilon)
                # Numerical zero after a floating-point least-squares solve.
                if abs(coef[1])*scale <= np.finfo(float).eps*64*max(abs(yy)):
                    g['status'] = 'invalid_C1'
                else:
                    g['N_DL_m3'] = kernel.dlcp_density(float(coef[0]), float(coef[1]), area, epsilon, constants['q_C'])
                    if g['N_DL_m3'] <= 0:
                        g['status'] = 'nonphysical_density'
            results.append(g)
            result['plots'].extend([plot(f'DLCP {gi}', 'Peak amplitude (V)', 'Capacitance (F)', curve('measured', xx, yy), curve('fit', xx, fitted, True)),
                plot(f'DLCP residuals {gi}', 'Peak amplitude (V)', 'Residual (F)', curve('residuals', xx, residual))])
            progress((gi+1)/len(groups))
        result['groups'] = results
        if any(g['status'] != 'valid' for g in results):
            result['status'] = result['computed_status'] = 'invalid'
            result['diagnostics'].append('Invalid DLCP groups: inspect group status; no density substituted')
        result['plots'].append(plot('DLCP profile', 'x_e (m)', 'N_DL (m^-3)',
            curve('DLCP', [g['x_e_m'] if g['x_e_m'] is not None else np.nan for g in results],
                         [g['N_DL_m3'] if g['N_DL_m3'] is not None else np.nan for g in results])))
        if m.get('constant_maximum_voltage') is not True:
            result['diagnostics'].append('Conditional DLCP: constant maximum voltage unconfirmed')
            if result['status'] == 'valid':
                result['computed_status'] = result['status'] = 'conditional'
    else:
        raise ValueError('Unknown M3 analysis type')
    progress(.9)
    result['x_d'] = resolve_width(m, options, xd_measurement, epsilon, result)
    if cancelled():
        raise RuntimeError('Analysis cancelled')
    progress(1.)
    return result


def resolve_width(m, options, xd_measurement, epsilon, result):
    inputs = result['inputs']
    source, policy = options.get('xd_source'), options.get('xd_policy', 'block')
    xd = dict(source=source, policy=policy, value_m=None, measurement_id=None, temperature_K=None, frequency_Hz=None, bias_V=None)
    if source == 'manual':
        xd['value_m'] = positive(options.get('xd_manual_m'), 'manual x_d')
        xd['description'] = options.get('xd_description', '')
        if not xd['description']:
            raise ValueError('Manual x_d requires a description')
    elif source in ('CV', 'DLCP'):
        sm = xd_measurement or m
        if sm.get('sample_id') != m.get('sample_id'):
            raise ValueError('x_d source must belong to the same sample')
        if sm is not m and sm != m and sm not in inputs:
            inputs.append(sm)
        sa = positive(sm.get('area_m2'), 'x_d source area')
        if source == 'CV':
            v, c, _ = cv_data(sm, sa)
            w = [kernel.width(float(ci), sa, epsilon) for ci in c]
            target = options.get('as_bias_V')
            if target is None or not math.isfinite(target):
                raise ValueError('Explicit AS bias required for CV x_d')
        elif sm['kind'] == 'DLCPProfile':
            v, w, target = column(sm, 'V'), column(sm, 'W'), 0.
        elif sm['kind'] == 'DLCPRaw' and options.get('dlcp_fit'):
            fit = options['dlcp_fit']
            if fit['inputs'][0]['id'] != sm['id'] or fit['status'] != 'valid':
                raise ValueError('Invalid DLCP width fit source')
            v = [g['bias_V'] for g in fit['groups']]; w = [g['x_e_m'] for g in fit['groups']]; target = 0.
        elif sm is m and options['type'] == 'DLCPPolynomial':
            v = [g['bias_V'] for g in result['groups']]; w = [g['x_e_m'] for g in result['groups']]; target = 0.
        else:
            raise ValueError('Select a measured DLCP profile for x_d')
        xd['value_m'], warning = choose_width(v, w, target, policy)
        xd.update(measurement_id=sm['id'], temperature_K=sm.get('temperature_K'), frequency_Hz=sm.get('frequency_Hz'), bias_V=target)
        if warning:
            result['diagnostics'].append(warning)
    else:
        raise ValueError('Select x_d source')
    return xd


def analyze_json(measurement_json, options_json, xd_json, cancelled, progress):
    return json.dumps(analyze(json.loads(measurement_json), json.loads(options_json),
        json.loads(xd_json) if xd_json else None, cancelled, progress), allow_nan=False)
