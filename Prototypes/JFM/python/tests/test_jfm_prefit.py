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
    assert "manual_range_expanded_to_minimum_four_points" in result["warnings"]
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


@pytest.mark.parametrize("lower,upper", [(0.4,0.2), (0.3,0.3), (2.,3.), (float("nan"),0.8)])
def test_invalid_manual_range_fails_loudly(lower: float, upper: float) -> None:
    voltage, current = _sample()
    with pytest.raises(ValueError, match="fit range"):
        prefit_characteristic(voltage, current, 300., "SD-RsRsh", options={
            "range_options": {"manual_lower_voltage": lower, "manual_upper_voltage": upper}})


def test_automatic_range_does_not_fall_back_to_negative_voltage() -> None:
    from jfm_prefit.range_detection import detect_fit_range
    from jfm_prefit.types import Sample
    with pytest.raises(ValueError, match="positive voltage"):
        detect_fit_range(Sample(np.linspace(-1., -0.1, 20), np.ones(20)*1e-5, 300.))


def test_series_diode_stays_finite_above_exponential_overflow() -> None:
    voltage = np.array([40., 400.])
    parameters = {"I0": 1.e-8, "A": 1.5, "Rs": 10.}
    current = evaluate_current(voltage, 300., "SD-Rs", parameters)
    assert np.all(np.isfinite(current))
    # Same Drobiazg equation, evaluated in logarithms without exp(V/AkT).
    junction = voltage-current*parameters["Rs"]
    np.testing.assert_allclose(np.log(current/parameters["I0"]), junction/(parameters["A"]*K_BOLTZMANN_EV*300.), rtol=1.e-10)


def test_refinement_accepts_zero_series_resistance_boundary() -> None:
    from jfm_prefit.refinement import refine_parameters
    from jfm_prefit.range_detection import _coarse_segmentation
    from jfm_prefit.preprocessing import prepare_sample
    from jfm_prefit.types import ParameterEstimate, PrefitContext, PrefitOptions
    voltage = np.linspace(.08, .4, 81)
    parameters = {"I0": 1.e-8, "A": 1.5, "Rs": 0., "Rsh": 1.e4}
    sample, _ = prepare_sample(voltage, evaluate_current(voltage, 300., "SD-RsRsh", parameters), 300.)
    context = PrefitContext(sample, _coarse_segmentation(0, 81), PrefitOptions())
    context.estimates = {name: ParameterEstimate(value, None, "test") for name, value in parameters.items()}
    refine_parameters(context, sample, MODEL_REGISTRY["SD-RsRsh"], 0, 81)
    assert "nonlinear_refinement_failed" not in context.warnings
    assert context.estimates["Rs"].value == 0.


@pytest.mark.parametrize("cell", ["cigs", "cdte"])
@pytest.mark.parametrize("temperature_k", range(200, 341, 20))
def test_scaps_off_resistances_are_device_slopes(cell, temperature_k) -> None:
    import json
    fixture = ROOT.parent / "tests/fixtures/scaps-gold1"
    reference = json.loads((fixture / "reference.json").read_text())["phase9_reference"][cell]
    expected = next(row for row in reference["dark_effective_parameters"] if row["temperature_K"] == temperature_k)
    values = np.loadtxt(fixture / f"{cell}-dark-T{temperature_k}K.tsv", skiprows=1)
    voltage, current_a = values.T
    area_m2 = 1.e-4
    for key, voltage_key, inverse in [("Rsh_slope_Ohm_m2", "Rsh_voltage_range_V", True),
                                      ("Rs_high_slope_Ohm_m2", "Rs_voltage_range_V", False)]:
        lower, upper = expected[voltage_key]
        mask = (voltage >= lower-1.e-10) & (voltage <= upper+1.e-10)
        slope = np.polyfit(voltage[mask], current_a[mask], 1)[0] if inverse else np.polyfit(current_a[mask], voltage[mask], 1)[0]
        resistance_ohm = 1./slope if inverse else slope
        # External resistors are off; these finite slopes include SCAPS transport.
        assert resistance_ohm*area_m2 == pytest.approx(expected[key], rel=1.e-6)


@pytest.mark.parametrize("minimum_v,fixed", [(-.2, True), (.08, False)])
def test_staged_shunt_requires_reverse_bias_support(minimum_v, fixed) -> None:
    from jfm_prefit.refinement import refine_parameters
    from jfm_prefit.range_detection import _coarse_segmentation
    from jfm_prefit.preprocessing import prepare_sample
    from jfm_prefit.types import ParameterEstimate, PrefitContext, PrefitOptions
    voltage = np.linspace(minimum_v, .4, 201)
    parameters = {"I0": 1.e-10, "A": 1.5, "Rs": 2., "Rsh": 1.e4}
    sample, _ = prepare_sample(voltage, evaluate_current(voltage, 300., "SD-RsRsh", parameters), 300.)
    context = PrefitContext(sample, _coarse_segmentation(0, 201), PrefitOptions())
    context.estimates = {name: ParameterEstimate(value, None, "test") for name, value in parameters.items()}
    context.estimates["A"] = ParameterEstimate(10., None, "test")
    lo = int(np.searchsorted(voltage, .08))
    refine_parameters(context, sample, MODEL_REGISTRY["SD-RsRsh"], lo, 201)
    assert "nonlinear_refinement_failed" not in context.warnings
    assert context.estimates["Rsh"].value == pytest.approx(1.e4, rel=1.e-4)
    assert (context.estimates["Rsh"].source == "low_voltage_shunt") == fixed


@pytest.mark.parametrize("rs_ohm,rsh_ohm", [(0., 100.), (1., 1.e4), (10., 100.), (10., 1.e10)])
def test_drobiazg_external_resistance_mapping(rs_ohm, rsh_ohm) -> None:
    # Independent implicit single-diode circuit, including -I0 and the full
    # terminal current through Rs; the product's Drobiazg definition is unchanged.
    voltage = np.linspace(-.2, 1.2, 101)
    i0, a, iph = 1.e-8, 1.5, .04
    scale = 1.+rs_ohm/rsh_ohm
    thermal = a*K_BOLTZMANN_EV*300.
    mapped = dict(I0=i0*np.exp(rs_ohm*(i0+iph)/(thermal*scale)), A=a*scale,
                  Rs=rs_ohm, Rsh=rsh_ohm+rs_ohm, I_ph=(i0+iph)/scale)
    current = evaluate_current(voltage, 300., "SD-RsRsh-I_ph", mapped)
    junction = voltage-rs_ohm*current
    implicit = i0*np.expm1(junction/thermal)+junction/rsh_ohm-iph
    np.testing.assert_allclose(current, implicit, rtol=1.e-10, atol=1.e-13)
