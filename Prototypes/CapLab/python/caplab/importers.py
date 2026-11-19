"""M2 import boundaries. SI conversion occurs once; source columns stay intact."""
import csv
import hashlib
import json
import math
import re
from pathlib import Path

DL_NAMES = ['W', 'N_DL', 'V', 'C0', 'C1', 'C2', 'A2_C02', 'W_CV', 'N_CV']
DL_UNITS = ['m', 'm^-3', 'V', 'F', 'F/V', 'F/V^2', 'm^4/F^2', 'm', 'm^-3']
DL_FACTORS = [1e-6, 1e6, 1, 1, 1, 1, 1, 1e-6, 1e6]
NUMBER = r'([+-]?\d+(?:[.,]\d+)?(?:[eE][+-]?\d+)?)'
UNITS = {'Hz': ('Hz', 1), 'kHz': ('Hz', 1e3), 'MHz': ('Hz', 1e6),
         'V': ('V', 1), 'mV': ('V', 1e-3), 'F': ('F', 1), 'nF': ('F', 1e-9),
         'pF': ('F', 1e-12), 'Ohm': ('Ohm', 1), 'kOhm': ('Ohm', 1e3),
         'S': ('S', 1), 'mS': ('S', 1e-3), 'uS': ('S', 1e-6), 'K': ('K', 1)}


def positive(value, name):
    if value is not None and (isinstance(value, bool) or not math.isfinite(value) or value <= 0):
        raise ValueError(f'{name} must be positive and finite')
    return value


def provenance(text, source, profile):
    return dict(source=str(source), sha256=hashlib.sha256(text.encode('utf-8')).hexdigest(), profile=profile)


def read_rows(text, decimal, cancelled, progress, scaps=False, exclude_rows=(), historical=False):
    if decimal not in ('.', ','):
        raise ValueError('Decimal separator must be . or ,')
    rows, numbers, header, width = [], [], [], None
    started = not scaps
    lines = text.lstrip('\ufeff').splitlines()
    excluded = set(exclude_rows)
    for n, line in enumerate(lines, 1):
        if cancelled():
            raise RuntimeError('Import cancelled')
        progress(n / max(1, len(lines)))
        line = line.strip()
        if n in excluded or not line:
            continue
        if not started:
            header.append(line)
            started = bool(re.match(r'(?:f\(Hz\)|v\(V\))', line, re.I))
            continue
        numeric = re.match(r'[+\-\d.,]|(?:nan|inf)(?:\s|$)', line, re.I)
        if line.startswith(('#', ';', '//')) or not numeric:
            if rows and not line.startswith(('#', ';', '//')):
                raise ValueError(f'Invalid data at row {n}')
            header.append(line)
            continue
        line = line.split('#', 1)[0].strip()
        delimiter = ';' if ';' in line else (',' if decimal == '.' and ',' in line else None)
        fields = next(csv.reader([line], delimiter=delimiter)) if delimiter else line.split()
        try:
            values = [float(v.replace(',', '.') if decimal == ',' else v) for v in fields]
        except ValueError as e:
            raise ValueError(f'Invalid numeric data at row {n}') from e
        if not historical and not all(math.isfinite(v) for v in values):
            raise ValueError(f'Nonfinite data at row {n}')
        if width is not None and len(values) != width:
            raise ValueError(f'Truncated/inconsistent columns at row {n}')
        width = len(values)
        rows.append(values)
        numbers.append(n)
    if not rows:
        raise ValueError('No data rows')
    if not excluded.issubset(set(range(1, len(lines) + 1))):
        raise ValueError('Excluded row outside source')
    return list(map(list, zip(*rows))), numbers, '\n'.join(header)


def metadata(header, source, options):
    result = {}
    for key, pattern, suffix in [('temperature_K', r'(?:temperature|\bT)\s*[=:]?\s*', 'K'),
                                 ('frequency_Hz', r'(?:frequency|\bf)\s*[=:]?\s*', 'Hz'),
                                 ('bias_V', r'(?:bias(?: voltage)?)\s*[=:]?\s*', '(?:Volt|V)')]:
        matches = [float(v.replace(',', '.')) for v in re.findall(pattern + NUMBER + r'\s*' + suffix, header, re.I)]
        if len(set(matches)) > 1:
            raise ValueError(f'Conflicting header {key}')
        suggestion = re.search(NUMBER + ('K' if key == 'temperature_K' else 'Hz'), Path(source).name, re.I) if key != 'bias_V' else None
        result['header_' + key] = matches[0] if matches else None
        result['suggested_' + key] = float(suggestion[1].replace(',', '.')) if suggestion else None
        result[key] = options.get(key, result['header_' + key])
        result[key + '_source'] = 'preview_confirmed' if key in options else ('header' if matches else None)
        if key != 'bias_V':
            positive(result[key], key)
        elif result[key] is not None and not math.isfinite(result[key]):
            raise ValueError('Nonfinite bias')
    result['diagnostics'] = []
    for key in ('temperature_K', 'frequency_Hz', 'bias_V'):
        if result['header_' + key] is not None and result[key] != result['header_' + key]:
            result['diagnostics'].append(f'{key}: confirmed value overrides conflicting header')
        if result['suggested_' + key] is not None and result[key] is not None and result[key] != result['suggested_' + key]:
            result['diagnostics'].append(f'{key}: filename suggestion conflicts with confirmed/header value')
    return result


def parse_measurement(text, source='', profile='lab_as_cp_rp', decimal='.', options=None,
                      cancelled=lambda: False, progress=lambda value: None, historical=False):
    options = options or {}
    raw, rows, header = read_rows(text, decimal, cancelled, progress, profile.startswith('scaps_'), options.get('exclude_rows', []), historical)
    meta = metadata(header, source, options)
    area = positive(options.get('area_m2'), 'area_m2')
    kind, variant = 'FrequencySweep', None
    if profile == 'propho_dl':
        names, units, factors = DL_NAMES[:], DL_UNITS[:], DL_FACTORS[:]
        mapping = list(range(9))
        input_units = ['um', 'cm^-3', 'V', 'F', 'F/V', 'F/V^2', 'm^4/F^2', 'um', 'cm^-3']
        kind = 'CV-derived' if len(raw) >= 9 and all(v == 0 for c in raw[3:6] for v in c) and raw[0] == raw[7] and raw[1] == raw[8] else 'DLCPProfile'
        variant = 'DL_CV'
        meta['profiles'] = {'DLCP': None if kind == 'CV-derived' else ['W', 'N_DL'], 'CV': ['W_CV', 'N_CV']}
    elif profile in ('scaps_cf', 'scaps_cv'):
        if area is None:
            raise ValueError('SCAPS requires explicit area_m2; no default area')
        a = area * 1e4
        if profile == 'scaps_cf':
            names = ['f', 'C', 'G', 'ReZ', 'ImZ', 'absZ', 'phase']
            units = ['Hz', 'F', 'S', 'Ohm', 'Ohm', 'Ohm', 'deg']
            factors = [1, a * 1e-9, a, 1/a, 1/a, 1/a, 1]
            variant = 'C_G'
            input_units = ['Hz', 'nF/cm^2', 'S/cm^2', 'Ohm*cm^2', 'Ohm*cm^2', 'Ohm*cm^2', 'deg']
        else:
            names = ['V', 'C', 'G', 'W', 'Napp', 'jtot', 'jbulk', 'jifr', 'jopp_n', 'jopp_p', 'eps_to_det_W', 'eps_to_det_N']
            units = ['V', 'F', 'S', 'm', 'm^-3'] + ['A/m^2'] * 5 + ['1', '1']
            factors = [1, a * 1e-9, a, 1e-6, 1e6] + [10] * 5 + [1, 1]
            kind, variant = 'CVSweep', 'V_C_G'
            input_units = ['V', 'nF/cm^2', 'S/cm^2', 'um', 'cm^-3'] + ['mA/cm^2'] * 5 + ['1', '1']
        mapping = list(range(len(names)))
        meta['area_m2'] = area
    else:
        definitions = {'lab_as_cp_rp': (['f', 'C', 'Rp'], ['Hz', 'F', 'Ohm'], 'Cp_Rp'),
                       'converted_as_c_g': (['f', 'C', 'G'], ['Hz', 'F', 'S'], 'C_G'),
                       'lab_is_c_g': (['f', 'C', 'G'], ['Hz', 'F', 'S'], 'C_G'),
                       'lab_is_cp_rp': (['f', 'C', 'Rp'], ['Hz', 'F', 'Ohm'], 'Cp_Rp'),
                       'lab_is_z': (['f', 'ReZ', 'ImZ'], ['Hz', 'Ohm', 'Ohm'], 'ReZ_ImZ'),
                       'lab_cv': (['V', 'C'], ['V', 'F'], 'V_C'),
                       'lab_dlcp_raw': (['V_bias', 'deltaV', 'C'], ['V', 'V', 'F'], 'amplitude')}
        if profile not in definitions:
            raise ValueError(f'Unknown import profile: {profile}')
        names, units, variant = definitions[profile]
        names, units = names[:], units[:]
        if profile == 'lab_cv':
            kind = 'CVSweep'
            if 'G' in options.get('mapping', {}):
                names.append('G'); units.append('S'); variant = 'V_C_G'
        if profile == 'lab_dlcp_raw':
            kind = 'DLCPRaw'
            for key, unit in [('temperature_K', 'K'), ('frequency_Hz', 'Hz')]:
                if key in options.get('mapping', {}):
                    names.append(key); units.append(unit)
            meta['amplitude_definition'] = options.get('amplitude_definition')
            meta['bias_definition'] = options.get('bias_definition')
            if meta['amplitude_definition'] not in (None, 'peak', 'RMS', 'peak-to-peak'):
                raise ValueError('Unknown amplitude definition')
            meta['constant_maximum_voltage'] = options.get('constant_maximum_voltage')
        mapping = [options.get('mapping', {}).get(name, i) for i, name in enumerate(names)]
        factors, input_units = [], []
        for name, expected in zip(names, units):
            declared = options.get('units', {}).get(name, expected)
            if declared not in UNITS or UNITS[declared][0] != expected:
                raise ValueError(f'Unsupported unit for {name}: {declared}')
            factors.append(UNITS[declared][1]); input_units.append(declared)
        if profile == 'lab_is_z':
            sign = options.get('imaginary_sign')
            if sign not in ('ImZ', '-ImZ'):
                raise ValueError('Select imaginary_sign: ImZ or -ImZ')
            factors[2] *= 1 if sign == 'ImZ' else -1
            meta['imaginary_sign'] = sign
    if len(set(mapping)) != len(mapping) or any(type(i) is not int or i < 0 or i >= len(raw) for i in mapping):
        raise ValueError('Missing, duplicate or invalid mapped column')
    columns = [[v * factor for v in raw[index]] for index, factor in zip(mapping, factors)]
    for i in range(len(raw)):
        if i not in mapping:
            names.append(f'Source{i+1}'); units.append(''); input_units.append(''); factors.append(1); columns.append(raw[i][:])
    quality = []
    for name in ('f', 'Rp', 'temperature_K', 'frequency_Hz'):
        if name in names:
            for i, v in enumerate(columns[names.index(name)]):
                if v <= 0:
                    if name == 'Rp':
                        quality.append(dict(column=names.index(name), source_row=rows[i], value=v, reason='nonpositive_Rp'))
                    else:
                        raise ValueError(f'{name} must be positive')
    if not historical and not all(math.isfinite(v) for c in columns for v in c):
        raise ValueError('Nonfinite SI conversion')
    source_column_rows = rows[:]
    if kind == 'FrequencySweep':
        f = columns[0]
        if len(set(f)) != len(f):
            raise ValueError('Duplicate frequencies; explicitly exclude source rows')
        order = sorted(range(len(f)), key=f.__getitem__)
        columns = [[c[i] for i in order] for c in columns]
        rows = [rows[i] for i in order]
    if kind == 'CVSweep':
        segments, start, direction = [], 0, 0
        for i in range(1, len(rows)):
            delta = columns[0][i] - columns[0][i-1]
            step = (delta > 0) - (delta < 0)
            if step and direction and step != direction:
                segments.append(dict(direction=direction, rows=list(range(start, i))))
                start = i-1
            if step:
                direction = step
        segments.append(dict(direction=direction, rows=list(range(start, len(rows)))))
        meta['sweep_segments'] = segments
    if kind == 'DLCPRaw':
        groups = {}
        for i in range(len(rows)):
            key = (columns[0][i], *(columns[names.index(k)][i] if k in names else meta[k] for k in ('temperature_K', 'frequency_Hz')))
            groups.setdefault(key, []).append(i)
        meta['groups'] = [dict(bias_V=k[0], temperature_K=k[1], frequency_Hz=k[2], rows=v) for k, v in groups.items()]
    if historical:
        for ci, c in enumerate(columns):
            for ri, v in enumerate(c):
                if not math.isfinite(v):
                    c[ri] = None
                    quality.append(dict(column=ci, source_row=rows[ri], value=None, reason='historical_nonfinite'))
        raw = [[v if math.isfinite(v) else None for v in c] for c in raw]
    return dict(provenance(text, source, profile), quality=quality, kind=kind, variant=variant, names=names, units=units, input_units=input_units,
                columns=columns, source_columns=raw, source_rows=rows, header=header, decimal=decimal,
                conversions=dict(zip(names, factors)), mapping=mapping, source_column_rows=source_column_rows, excluded_rows=options.get('exclude_rows', []), **meta)


def unwrap(value):
    if isinstance(value, dict):
        if value.get('__ndarray__') is True:
            return unwrap(value['data'])
        return {k: unwrap(v) for k, v in value.items()}
    if isinstance(value, list):
        return [unwrap(v) for v in value]
    if isinstance(value, float) and not math.isfinite(value):
        return {'value': None, 'reason': 'historical_nonfinite'}
    return value


def import_preview(text, source, decimal='.', options=None, cancelled=lambda: False, progress=lambda value: None):
    options = options or {}
    profile = options.get('profile', 'lab_as_cp_rp')
    if profile == 'auto':
        profile = {'.cf': 'scaps_cf', '.cv': 'scaps_cv', '.json': 'propho_json'}.get(Path(source).suffix.lower(), 'lab_as_cp_rp')
    if profile == 'propho_json':
        data = json.loads(text.lstrip('\ufeff'))
        result = migrate_project(data, text, source, cancelled, progress, options) if 'samples' in data else import_manifest(data, text, source, decimal, options, cancelled, progress)
        result['decimal'] = decimal
        return result
    measurements = []
    starts = [m.start() for m in re.finditer(r'^(?:\ufeff)?SCAPS ', text, re.M)] if profile.startswith('scaps_') else []
    starts = starts or [0]
    for i, start in enumerate(starts):
        end = starts[i+1] if i+1 < len(starts) else len(text)
        offset = text[:start].count('\n')
        block_options = dict(options)
        if 'exclude_rows' in options:
            block_options['exclude_rows'] = [n-offset for n in options['exclude_rows'] if offset < n <= offset + text[start:end].count('\n') + 1]
        m = parse_measurement(text[start:end], source, profile, decimal, block_options, cancelled,
                              lambda value: progress((i + value) / len(starts)))
        m['source_rows'] = [n + offset for n in m['source_rows']]
        m['source_column_rows'] = [n + offset for n in m['source_column_rows']]
        m['excluded_rows'] = [n + offset for n in m['excluded_rows']]
        m['sha256'] = hashlib.sha256(text.encode('utf-8')).hexdigest()
        m['block_index'] = i
        measurements.append(m)
    return dict(provenance(text, source, profile), decimal=decimal, samples=[dict(name=options.get('sample_name', Path(str(source).split(' (Cp/Rp -> C/G)', 1)[0]).stem or 'Sample'),
                area_m2=positive(options.get('area_m2'), 'area_m2'), metadata={}, measurements=measurements)], historical=[])


def import_manifest(data, text, source, decimal, options, cancelled, progress):
    base = Path(options.get('base_dir') or Path(source).resolve().parent).resolve()
    samples, jobs = [], []
    for s in data['textdata']['samples']:
        if cancelled():
            raise RuntimeError('Import cancelled')
        measurements = []
        for key, profile in [('as_data_dir', options.get('as_profile', 'lab_as_cp_rp')), ('dl_data_dir', 'propho_dl')]:
            if not s.get(key):
                continue
            directory = base / s[key]
            if not directory.is_dir():
                raise ValueError(f'Missing directory: {directory}. Select base_dir explicitly (legacy ./data_Cd paths usually require the ProPho directory).')
            for path in sorted(directory.iterdir()):
                if path.suffix.lower() in ('.dat', '.csv', '.txt'):
                    jobs.append((path, profile, measurements))
        samples.append(dict(name=s['name'], area_m2=positive(s.get('area'), 'area') * 1e-6 if s.get('area') is not None else None,
                            metadata=s, measurements=measurements))
    for i, (path, profile, measurements) in enumerate(jobs):
        confirmed = {k: options[k] for k in ('temperature_K', 'frequency_Hz', 'bias_V') if k in options}
        try:
            measurements.append(parse_measurement(path.read_bytes().decode('utf-8'), str(path.resolve()), profile, decimal, confirmed, cancelled,
                                                   lambda value: progress((i + value) / len(jobs))))
        except ValueError as error:
            raise ValueError(f'{path}: {error}') from error
    progress(1.0)
    return dict(provenance(text, source, 'propho_manifest'), base_dir=str(base), samples=samples, historical=[])


def migrate_project(data, text, source, cancelled, progress, options):
    samples, historical = [], []
    for si, s in enumerate(data['samples']):
        if cancelled():
            raise RuntimeError('Import cancelled')
        progress(si / max(1, len(data['samples'])))
        measurements = []
        for key, profile, keys in [('as_data', 'lab_as_cp_rp', ['freq', 'Cp', 'Rp']),
                                    ('dl_data', 'propho_dl', ['W_um', 'N', 'A_V', 'coeff_c', 'coeff_b', 'coeff_a', '1/C^2', 'W_um_CV', 'N_CV'])]:
            for index, record in enumerate(s.get(key, [])):
                if not all(k in record for k in keys):
                    raise ValueError(f'Incomplete historical {key}[{index}]')
                cols = [record[k]['data'] if isinstance(record[k], dict) and record[k].get('__ndarray__') is True else record[k] for k in keys]
                if not all(isinstance(c, list) for c in cols) or not cols[0] or any(len(c) != len(cols[0]) for c in cols):
                    raise ValueError('Unequal historical measurement columns')
                body = '\n'.join(' '.join(str(v) for v in row) for row in zip(*cols))
                opts = {k: record[v] for k, v in [('temperature_K', 'temp_K'), ('frequency_Hz', 'dl_freq')] if v in record}
                if profile == 'lab_as_cp_rp' and 'scaps' in str(s.get('as_data_dir', '')).lower() and 'as_profile' not in options:
                    raise ValueError('Historical SCAPS Rp field is ambiguous; select as_profile explicitly')
                actual = options.get('as_profile', profile) if key == 'as_data' else profile
                m = parse_measurement(body, source, actual, '.', opts, cancelled, lambda value: None, historical=True)
                m.update(sha256=hashlib.sha256(text.encode('utf-8')).hexdigest(), historical_source={'status': 'historical', 'record': unwrap(record)},
                         legacy_collection=key, legacy_index=index, json_pointer=f'/samples/{si}/{key}/{index}')
                measurements.append(m)
        samples.append(dict(name=s['name'], area_m2=positive(s.get('area'), 'area') * 1e-6 if s.get('area') is not None else None,
                            metadata=unwrap({k: v for k, v in s.items() if k not in ('as_data', 'dl_data', 'walter', 'defects', 'plot_browser_settings')}), measurements=measurements))
        historical.append(dict(status='historical', model='ProPho-compat', sample_index=si,
                               data=unwrap({k: s[k] for k in ('defects', 'walter', 'plot_browser_settings') if k in s})))
    progress(1.0)
    return dict(provenance(text, source, 'propho_project'), samples=samples, historical=historical,
                legacy_project=unwrap({k: v for k, v in data.items() if k != 'samples'}))
