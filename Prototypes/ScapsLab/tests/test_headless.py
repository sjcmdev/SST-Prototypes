import csv
import math
from pathlib import Path
import subprocess
import tempfile
import scapslab

session = scapslab.Session()
loaded = session.execute({'operation': 'load', 'path': project_path})
assert loaded['ok'], loaded
ran = session.execute({'operation': 'run'})
assert ran['ok'], ran
points = ran['result']['points']
assert points and all('bias_id' in p for p in points)
assert any(p['J_A_m2'] is not None for p in points)
headless = Path(headless_path)
with tempfile.TemporaryDirectory(prefix='scapslab-headless-') as output:
    subprocess.run([str(headless), '--project', project_path, '--output', output], check=True)
    with open(Path(output) / 'iv.csv') as f:
        reference = list(csv.DictReader(f))
    assert len(reference) == len(points)
    maximum_relative_error = 0.0
    for i, (actual, expected) in enumerate(zip(points, reference)):
        assert actual['voltage_V'] == float(expected['voltage_V'])
        current = float(expected['current_density_A_m2'])
        if math.isnan(current):
            assert actual['J_A_m2'] is None
        else:
            error = abs(actual['J_A_m2'] - current)
            assert error <= abs(current) * 1e-10, (i, actual, expected)
            maximum_relative_error = max(maximum_relative_error, error / abs(current) if current else 0)
        with open(Path(output) / f'profiles/profile_{i:03}.csv') as f:
            mesh = [float(row['x_m']) for row in csv.DictReader(f)]
        assert mesh == actual['x_m']
    saved = str(Path(output) / 'roundtrip.yaml')
    assert session.execute({'operation': 'save', 'path': saved})['ok']
    assert session.execute({'operation': 'load', 'path': saved})['document'] == loaded['document']
    print(f'Python command run + scaps_headless CSV: {len(points)} points, max relative J error {maximum_relative_error:g}, mesh identical', flush=True)
assert not session.execute({'operation':'edit','patch':{'temperature_K':-1}})['ok']
assert not session.execute({'operation':'unknown'})['ok']

