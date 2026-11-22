import json
import math
from pathlib import Path

import numpy as np
import pytest
import sst_caplab as kernel

from caplab.analysis import analyze
from test_m7_walter import options, sweep


@pytest.mark.parametrize('compat', [False, True])
@pytest.mark.parametrize('ef', [0., 2.])
def test_bulk_matches_scalar_bits_and_quality_on_fixture_grids(compat, ef):
    fixture = Path(__file__).parents[2] / 'tests/fixtures'
    grids = [np.loadtxt(fixture / 'cf_100K_7.dat')[:, 0]]
    with np.load(fixture / 'scaps-gold1/curves.npz') as data:
        grids.extend(v[:, 0] for name, v in data.items() if name.startswith('phase-3-cf-'))
    grids.append(np.geomspace(.001, 1e10, 1201))
    for frequency in grids:
        omega = np.ascontiguousarray(2 * math.pi * frequency)
        derivative = np.linspace(-3e-7, 3e-7, len(omega))
        bulk = kernel.walter_spectrum(omega, derivative, 250., 8e7, .8, ef, 2e-6, compat)
        assert isinstance(bulk['energy_eV'], np.ndarray)
        for i, (w, d) in enumerate(zip(omega, derivative)):
            scalar = json.loads(kernel.walter_point(float(w), 250., 8e7, .8, ef, 2e-6, float(d), compat))
            assert scalar['quality'] == bulk['quality'][i]
            for key in ('energy_eV', 'field_factor_V_m_C', 'density_cm3_eV'):
                value = bulk[key][i]
                if scalar[key] is None:
                    assert np.isnan(value)
                else:
                    assert np.float64(scalar[key]).view(np.uint64) == value.view(np.uint64)
        lo, hi = float(min(bulk['energy_eV'])), float(max(bulk['energy_eV']))
        density = bulk['density_cm3_eV']
        legacy = json.loads(kernel.walter_integral(bulk['energy_eV'].tolist(),
            [float(v) if np.isfinite(v) else None for v in density], lo, hi, compat))
        native = kernel.walter_integral_values(bulk['energy_eV'], density, lo, hi, compat)
        assert native == legacy
        if native['density_cm3'] is not None:
            assert np.float64(native['density_cm3']).view(np.uint64) == np.float64(legacy['density_cm3']).view(np.uint64)


def test_analysis_uses_one_spectrum_call_and_no_scalar_or_json_integral(monkeypatch):
    calls = []
    original = kernel.walter_measurement
    def bulk(*args):
        calls.append(len(args[0]))
        return original(*args)
    def unexpected(*args):
        pytest.fail('legacy scalar/JSON kernel called by analysis')
    monkeypatch.setattr(kernel, 'walter_measurement', bulk)
    monkeypatch.setattr(kernel, 'walter_spectrum', unexpected)
    monkeypatch.setattr(kernel, 'walter_point', unexpected)
    monkeypatch.setattr(kernel, 'walter_integral', unexpected)
    assert analyze(sweep(), options())['status'] == 'valid'
    assert calls == [3]
    with pytest.raises(ValueError):
        original(np.ones(3), np.ones(2), 250., 8e7, .8, 0., 2e-6, False, [-1e6, 1e6], None)


@pytest.mark.parametrize('compat', [False, True])
def test_measurement_bulk_preserves_selected_indices_and_integral_bits(compat):
    omega = np.geomspace(1., 1e6, 21)
    derivative = np.linspace(-1e-6, 1e-6, len(omega))
    spectrum = kernel.walter_spectrum(omega, derivative, 300., 1e8, .8, 0., 2e-6, compat)
    limits = sorted([float(spectrum['energy_eV'][3]), float(spectrum['energy_eV'][-4])])
    keep = [i for i, v in enumerate(spectrum['energy_eV']) if limits[0] <= v <= limits[1]]
    result = kernel.walter_measurement(omega, derivative, 300., 1e8, .8, 0., 2e-6, compat, limits, None)
    assert result['selected_indices'] == keep
    for key in ('energy_eV', 'density_cm3_eV', 'field_factor_V_m_C'):
        assert np.array_equal(result[key].view(np.uint64), spectrum[key][keep].view(np.uint64))
    expected = json.loads(kernel.walter_integral(result['energy_eV'].tolist(), result['density_cm3_eV'].tolist(), *limits, compat))
    assert result['integral'] == expected
    if expected['density_cm3'] is not None:
        assert np.float64(result['integral']['density_cm3']).view(np.uint64) == np.float64(expected['density_cm3']).view(np.uint64)
