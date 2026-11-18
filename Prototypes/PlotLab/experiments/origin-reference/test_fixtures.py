import csv
import tempfile
import unittest
from pathlib import Path
from prepare_fixtures import prepare

class FixtureTests(unittest.TestCase):
    def test_measurements_and_relations(self):
        build = Path(__file__).resolve().parent / 'build'
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=build) as temporary:
            p = prepare(Path(temporary))
            self.assertEqual(len(p['curves']), 16)
            self.assertEqual(len({c['temperature_K'] for c in p['curves']}), 16)
            def read(name):
                with (Path(temporary)/'fixtures'/name).open(newline='') as stream:
                    return list(csv.DictReader(stream))
            measured = read('iv_temperature_long.csv')
            self.assertEqual(len(measured), sum(c['rows'] for c in p['curves']))
            self.assertEqual(float(measured[0]['current_A']), -2.552701e-7)
            first, second = read('iv_temperature_pairs.csv'), read('iv_temperature_second_dataset.csv')
            self.assertEqual(len(first), len(second))
            for a, b in zip(first, second, strict=True):
                self.assertEqual(len(a), 32)
                for k in a:
                    self.assertAlmostEqual(float(b[k]), float(a[k])*(1.2 if k.startswith('current') else 1))
            for row in read('cv_mott_schottky.csv'):
                self.assertAlmostEqual(float(row['inverse_capacitance_squared_F_minus2'])*float(row['capacitance_F'])**2, 1)
            rates = read('arrhenius.csv')
            self.assertTrue(all(float(a['rate_per_s']) < float(b['rate_per_s']) for a,b in zip(rates,rates[1:])))

if __name__ == '__main__':
    unittest.main()
