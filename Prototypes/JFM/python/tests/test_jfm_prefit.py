from __future__ import annotations

from pathlib import Path
import sys

import numpy as np
import pytest
from scipy.special import lambertw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from jfm_constants import K_BOLTZMANN_EV
from jfm_prefit.cpp_adapter import prefit_characteristic
from jfm_prefit.current_models import evaluate_current
from jfm_prefit.model_registry import MODEL_REGISTRY
from jfm_prefit.range_detection import _tail_plateau_bound
from jfm_prefit.types import RangeOptions


def _sample() -> tuple[np.ndarray, np.ndarray]:
    voltage = np.linspace(-0.1, 0.9, 180)
    parameters = {"I0": 2.0e-9, "A": 1.6, "Rs": 1.2, "Rsh": 2.0e4}
    return voltage, evaluate_current(voltage, 300.0, "SD-RsRsh", parameters)


def test_prefit_returns_every_model_parameter_and_curve() -> None:
    voltage, current = _sample()
    for model_id, spec in MODEL_REGISTRY.items():
        result = prefit_characteristic(
            voltage, current, 300.0, model_id, spec.characteristic_type
        )
        assert set(result["parameters"]) == set(spec.parameter_names)
        assert len(result["voltage"]) == len(result["prefit_current"])
        prefit_current = np.asarray(result["prefit_current"])
        if "alpha" in spec.parameter_names:
            lo, hi = result["fit_range"]["lo"], result["fit_range"]["hi"]
            prefit_current = prefit_current[lo:hi]
        assert np.all(np.isfinite(prefit_current))


def test_cpp_adapter_returns_plain_float_prefit_curve() -> None:
    voltage, current = _sample()

    result = prefit_characteristic(voltage, current, 300.0, "SD-RsRsh")

    assert all(type(value) is float for value in result["prefit_current"])


def test_light_prefit_keeps_photocurrent_on_original_data_scale() -> None:
    voltage = np.linspace(-0.2, 0.8, 400)
    parameters = {"I0": 1.0e-10, "A": 1.5, "I_ph": 0.03}
    current = evaluate_current(voltage, 300.0, "SD-0-I_ph", parameters)
    result = prefit_characteristic(voltage, current, 300.0, "SD-0-I_ph", "light")

    assert result["parameters"]["I_ph"]["value"] == pytest.approx(
        parameters["I_ph"], rel=0.02
    )
    assert np.interp(0.0, result["voltage"], result["prefit_current"]) == pytest.approx(
        np.interp(0.0, voltage, current), rel=0.02
    )


@pytest.mark.parametrize(
    ("model_id", "parameters", "parameter_name", "relative_tolerance"),
    (
        ("SD-Rs", {"I0": 1.0e-10, "A": 1.5, "Rs": 2.0}, "Rs", 0.12),
        ("SD-Rsh", {"I0": 1.0e-10, "A": 1.5, "Rsh": 1.0e5}, "Rsh", 0.20),
    ),
)
def test_resistance_range_scan_recovers_noisy_synthetic_parameter(
    model_id: str,
    parameters: dict[str, float],
    parameter_name: str,
    relative_tolerance: float,
) -> None:
    voltage = np.linspace(-0.2, 0.8, 1001)
    clean = evaluate_current(voltage, 300.0, model_id, parameters)
    generator = np.random.Generator(np.random.MT19937(42))
    noisy = clean + generator.normal(size=clean.size) * clean * 0.05

    result = prefit_characteristic(
        voltage, noisy, 300.0, model_id, options={"refinement_passes": 0}
    )
    estimate = result["parameters"][parameter_name]

    assert estimate["source"].endswith("_range_scan")
    assert estimate["value"] == pytest.approx(
        parameters[parameter_name], rel=relative_tolerance
    )


def test_manual_range_is_respected() -> None:
    voltage, current = _sample()
    result = prefit_characteristic(
        voltage,
        current,
        300.0,
        "SD-RsRsh",
        options={
            "range_options": {"manual_lower_voltage": 0.2, "manual_upper_voltage": 0.65}
        },
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
        options={
            "range_options": {
                "manual_lower_voltage": 0.4,
                "manual_upper_voltage": 0.405,
            }
        },
    )
    fit_range = result["fit_range"]
    for window in result["segmentation"].values():
        assert fit_range["lo"] <= window["lo"] < window["hi"] <= fit_range["hi"]
    assert np.all(np.isfinite(result["prefit_current"]))


def test_tail_plateau_bound_matches_legacy_reverse_average_algorithm() -> None:
    voltage = np.arange(100, dtype=float)
    current = np.concatenate((np.linspace(0.0, 0.9, 90), np.ones(10)))

    hi, detected = _tail_plateau_bound(voltage, current, 0, RangeOptions())

    assert detected
    assert hi == 89


def test_preprocessing_sorts_filters_and_averages_duplicates() -> None:
    voltage, current = _sample()
    dirty_voltage = np.concatenate((voltage[::-1], [np.nan, voltage[20]]))
    dirty_current = np.concatenate((current[::-1], [1.0, current[20]]))
    result = prefit_characteristic(dirty_voltage, dirty_current, 300.0, "SD-RsRsh")
    assert result["voltage"] == sorted(set(voltage.tolist()))
    assert {
        "voltage_sorted",
        "non_finite_points_removed",
        "duplicate_voltage_averaged",
    } <= set(result["warnings"])


def test_current_models_match_documented_lambert_w_equations() -> None:
    voltage = np.linspace(0.05, 0.65, 32)
    temperature_k = 300.0
    parameters = {"I0": 2.0e-9, "A": 1.6, "Rs": 1.2, "Rsh": 2.0e4}
    avt = parameters["A"] * K_BOLTZMANN_EV * temperature_k
    argument = parameters["I0"] * parameters["Rs"] / avt * np.exp(voltage / avt)
    diode = np.real(lambertw(argument)) * avt / parameters["Rs"]
    internal_voltage = voltage - diode * parameters["Rs"]
    expected = diode + internal_voltage / parameters["Rsh"]
    np.testing.assert_allclose(
        evaluate_current(voltage, temperature_k, "SD-RsRsh", parameters), expected
    )

    nonlinear_parameters = {**parameters, "alpha": 2.0, "Rsh2": 8.0e4}
    nonlinear_expected = (
        expected
        + np.power(internal_voltage, nonlinear_parameters["alpha"])
        / nonlinear_parameters["Rsh2"]
    )
    np.testing.assert_allclose(
        evaluate_current(
            voltage, temperature_k, "SD-RsRsh-alpha-Rsh2", nonlinear_parameters
        ),
        nonlinear_expected,
    )


def test_missing_resistance_terms_are_removed() -> None:
    voltage = np.linspace(0.0, 0.3, 8)
    temperature_k = 300.0
    parameters = {"I0": 2.0e-9, "A": 1.6, "Rs": 1.2, "Rsh": 2.0e4}
    avt = parameters["A"] * K_BOLTZMANN_EV * temperature_k
    no_series = parameters["I0"] * np.exp(voltage / avt)
    argument = parameters["I0"] * parameters["Rs"] / avt * np.exp(voltage / avt)
    with_series = np.real(lambertw(argument)) * avt / parameters["Rs"]

    np.testing.assert_allclose(
        evaluate_current(voltage, temperature_k, "SD-0", parameters), no_series
    )
    np.testing.assert_allclose(
        evaluate_current(voltage, temperature_k, "SD-Rsh", parameters),
        no_series + voltage / parameters["Rsh"],
    )
    np.testing.assert_allclose(
        evaluate_current(voltage, temperature_k, "SD-Rs", parameters), with_series
    )
