from __future__ import annotations

from pathlib import Path
import sys

import numpy as np
from scipy.special import lambertw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from jfm_prefit.cpp_adapter import prefit_characteristic
from jfm_prefit.current_models import evaluate_current
from jfm_prefit.model_registry import MODEL_REGISTRY


def _sample() -> tuple[np.ndarray, np.ndarray]:
    voltage = np.linspace(-0.1, 0.9, 180)
    parameters = {"I0": 2.0e-9, "A": 1.6, "Rs": 1.2, "Rsh": 2.0e4}
    return voltage, evaluate_current(voltage, 300.0, "SD-RsRsh", parameters)


def test_prefit_returns_every_model_parameter_and_curve() -> None:
    voltage, current = _sample()
    for model_id, spec in MODEL_REGISTRY.items():
        result = prefit_characteristic(voltage, current, 300.0, model_id, spec.characteristic_type)
        assert set(result["parameters"]) == set(spec.parameter_names)
        assert len(result["voltage"]) == len(result["prefit_current"])
        assert np.all(np.isfinite(result["prefit_current"]))


def test_manual_range_is_respected() -> None:
    voltage, current = _sample()
    result = prefit_characteristic(
        voltage,
        current,
        300.0,
        "SD-RsRsh",
        options={"range_options": {"manual_lower_voltage": 0.2, "manual_upper_voltage": 0.65}},
    )
    assert result["fit_range"]["reason"] == "manual"
    assert result["fit_range"]["voltage_min"] >= 0.19
    assert result["fit_range"]["voltage_max"] <= 0.66


def test_very_narrow_manual_range_stays_inside_selected_data() -> None:
    voltage, current = _sample()
    result = prefit_characteristic(
        voltage,
        current,
        300.0,
        "SD-RsRsh",
        options={"range_options": {"manual_lower_voltage": 0.4, "manual_upper_voltage": 0.405}},
    )
    fit_range = result["fit_range"]
    for window in result["segmentation"].values():
        assert fit_range["lo"] <= window["lo"] < window["hi"] <= fit_range["hi"]
    assert np.all(np.isfinite(result["prefit_current"]))


def test_automatic_range_cuts_saturated_current_tail() -> None:
    voltage = np.linspace(0.0, 1.0, 160)
    rising = 1.0e-10 * np.exp(16.0 * (voltage - 0.25))
    plateau = rising[np.searchsorted(voltage, 0.64)]
    current = np.minimum(rising, plateau)
    result = prefit_characteristic(voltage, current, 300.0, "SD-RsRsh")
    assert 0.50 < result["fit_range"]["voltage_max"] < 0.78


def test_preprocessing_sorts_filters_and_averages_duplicates() -> None:
    voltage, current = _sample()
    dirty_voltage = np.concatenate((voltage[::-1], [np.nan, voltage[20]]))
    dirty_current = np.concatenate((current[::-1], [1.0, current[20]]))
    result = prefit_characteristic(dirty_voltage, dirty_current, 300.0, "SD-RsRsh")
    assert result["voltage"] == sorted(set(voltage.tolist()))
    assert {"voltage_sorted", "non_finite_points_removed", "duplicate_voltage_averaged"} <= set(result["warnings"])


def test_current_models_match_documented_lambert_w_equations() -> None:
    voltage = np.linspace(0.05, 0.65, 32)
    temperature_k = 300.0
    parameters = {"I0": 2.0e-9, "A": 1.6, "Rs": 1.2, "Rsh": 2.0e4}
    avt = parameters["A"] * 8.6e-5 * temperature_k
    argument = parameters["I0"] * parameters["Rs"] / avt * np.exp(voltage / avt)
    diode = np.real(lambertw(argument)) * avt / parameters["Rs"]
    internal_voltage = voltage - diode * parameters["Rs"]
    expected = diode + internal_voltage / parameters["Rsh"]
    np.testing.assert_allclose(evaluate_current(voltage, temperature_k, "SD-RsRsh", parameters), expected)

    nonlinear_parameters = {**parameters, "alpha": 2.0, "Rsh2": 8.0e4}
    nonlinear_expected = expected + np.power(internal_voltage, nonlinear_parameters["alpha"]) / nonlinear_parameters["Rsh2"]
    np.testing.assert_allclose(
        evaluate_current(voltage, temperature_k, "SD-RsRsh-alpha-Rsh2", nonlinear_parameters),
        nonlinear_expected,
    )
