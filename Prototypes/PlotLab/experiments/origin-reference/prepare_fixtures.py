"""Regenerate fixtures only. No Origin rendering or ground truth is claimed."""
import csv
import hashlib
import json
import math
from pathlib import Path
import yaml

ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent


def prepare(out=OUT):
    folder = out / 'fixtures'
    folder.mkdir(parents=True, exist_ok=True)
    def write(name, headers, rows):
        with (folder / name).open('w', newline='', encoding='utf-8') as stream:
            writer = csv.writer(stream)
            writer.writerow(headers)
            writer.writerows(rows)
    project = ROOT / 'Prototypes/JFM/JFM-projects/HZB-project.yaml.jfm.yaml'
    curves = yaml.safe_load(project.read_text(encoding='utf-8'))['characteristics']
    if len(curves) != 16:
        raise ValueError('Expected 16 HZB curves')
    sources, long_rows, pairs = [], [], []
    for curve in curves:
        source = ROOT / 'Prototypes/JFM/example-data/real-measurement/dat' / Path(curve['source_path']).name
        rows = [[float(v) for v in line.split()] for line in source.read_text().splitlines() if line.strip()]
        if len(rows) != curve['raw_point_count'] or any(len(row) != 3 for row in rows):
            raise ValueError(f'Unexpected data shape: {source}')
        if not all(math.isfinite(v) for row in rows for v in row):
            raise ValueError(f'Non-finite data: {source}')
        t = curve['temperature_K']
        long_rows.extend([curve['id'], t, *row] for row in rows)
        pairs.extend([[row[0] for row in rows], [row[1] for row in rows]])
        sources.append(dict(curve_id=curve['id'], temperature_K=t, long_name=curve['name'],
                            source=source.relative_to(ROOT).as_posix(), rows=len(rows),
                            sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    headers = [name for c in sources for name in (f"voltage_V_T{c['temperature_K']}", f"current_A_T{c['temperature_K']}")]
    write('iv_temperature_long.csv', ['curve_id', 'temperature_K', 'voltage_V', 'current_A', 'source_column_3_unit_unknown'], long_rows)
    write('iv_temperature_pairs.csv', headers, zip(*pairs, strict=True))
    write('iv_temperature_second_dataset.csv', headers,
          [[v if i % 2 == 0 else v * 1.2 for i, v in enumerate(row)] for row in zip(*pairs, strict=True)])
    write('curve_metadata.csv', ['curve_id', 'temperature_K', 'long_name', 'units'],
          [[c['curve_id'], c['temperature_K'], c['long_name'], 'A'] for c in sources])
    write('cv_mott_schottky.csv', ['voltage_V', 'capacitance_F', 'inverse_capacitance_squared_F_minus2'],
          [[v, 2e-9/math.sqrt(1.5-v), (1.5-v)/(2e-9)**2] for v in [-1+i*0.02 for i in range(101)]])
    write('arrhenius.csv', ['temperature_K', 'inverse_temperature_1000_per_K', 'rate_per_s'],
          [[t, 1000/t, 1e9*math.exp(-0.32/(8.617333262145e-5*t))] for t in range(180, 341, 10)])
    write('custom_ticks.csv', ['position_V', 'label'], [[-0.5, 'reverse'], [0, 'zero'], [0.25, 'quarter'], [0.9, 'forward']])
    provenance = dict(schema_version=1, project=project.relative_to(ROOT).as_posix(),
                      project_sha256=hashlib.sha256(project.read_bytes()).hexdigest(), curves=sources,
                      synthetic={'cv_mott_schottky.csv': 'C=2e-9/sqrt(1.5-V) F',
                                 'arrhenius.csv': 'rate=1e9*exp(-0.32/(k_B*T)) s^-1; k_B=8.617333262145e-5 eV/K',
                                 'custom_ticks.csv': 'Arbitrary tick labels'},
                      derived={'iv_temperature_second_dataset.csv': 'Measured current multiplied by 1.2; template test only'},
                      units_note='Project units are empty. Source column 2 is assumed A, column 3 is preserved with unknown units; confirm A against instrument metadata before publication.')
    (folder / 'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n', encoding='utf-8')
    return provenance


if __name__ == '__main__':
    print(json.dumps({'status': 'fixtures_only', 'curves': len(prepare()['curves'])}))
