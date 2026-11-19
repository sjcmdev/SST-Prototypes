from __future__ import annotations

from pathlib import Path
import sys

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from jfm_prefit.cpp_adapter import auto_prefit_characteristic
from jfm_prefit.current_models import evaluate_current
from jfm_prefit.model_selection import select_child_model
from jfm_prefit.prefit import run_auto_prefit
from jfm_prefit.residual_diagnostics import window_size
from jfm_prefit.adaptive_range import fit_and_diagnose, locally_expand_range
from jfm_prefit.preprocessing import prepare_sample
from jfm_prefit.types import (
    AdaptiveRangeOptions,
    AutoPrefitOptions,
    RangeWindow,
    ResidualOptions,
    ResidualProblem,
)


def _add_noise(current: np.ndarray, seed: int) -> np.ndarray:
    # Relative noise exercises the same residual checks as real measurements.
    generator = np.random.Generator(np.random.MT19937(seed))
    return current + generator.normal(size=current.size) * np.abs(current) * 0.003


def _dark_sample():
    voltage = np.linspace(-0.1, 0.9, 220)
    parameters = {"I0": 2.0e-9, "A": 1.6, "Rs": 1.2, "Rsh": 2.0e4}
    current = _add_noise(evaluate_current(voltage, 300.0, "SD-RsRsh", parameters), 7)
    sample, _ = prepare_sample(voltage, current, 300.0, "dark")
    return sample


def _light_sample():
    voltage = np.linspace(-0.3, 0.8, 260)
    parameters = {"I0": 1.5e-10, "A": 1.5, "Rs": 0.8, "Rsh": 5.0e4, "I_ph": 0.05}
    current = _add_noise(
        evaluate_current(voltage, 300.0, "SD-RsRsh-I_ph", parameters), 11
    )
    sample, _ = prepare_sample(voltage, current, 300.0, "light")
    return sample


def test_auto_prefit_detects_dark_branch() -> None:
    result = run_auto_prefit(_dark_sample())
    assert result.branch == "dark"
    assert result.selected is not None
    assert result.selected.coverage_fraction > 0.5


def test_auto_prefit_detects_light_branch() -> None:
    result = run_auto_prefit(_light_sample())
    assert result.branch == "light"
    assert result.selected is not None
    assert "I_ph" in result.selected.parameters
    assert result.selected.parameters["I_ph"].value == pytest.approx(0.05, rel=0.2)


def test_photo_branch_mode_force_overrides_detection() -> None:
    forced_dark = run_auto_prefit(
        _light_sample(), AutoPrefitOptions(photo_branch_mode="force_dark")
    )
    assert forced_dark.branch == "dark"

    forced_light = run_auto_prefit(
        _dark_sample(), AutoPrefitOptions(photo_branch_mode="force_light")
    )
    assert forced_light.branch == "light"


@pytest.mark.parametrize(
    ("model_id", "problem", "expected_child"),
    (
        ("SD-0", ResidualProblem.HIGH_EDGE, "SD-Rs"),
        ("SD-0", ResidualProblem.LOW_EDGE, "SD-Rsh"),
        ("SD-Rs", ResidualProblem.LOW_EDGE, "SD-RsRsh"),
        ("SD-Rsh", ResidualProblem.HIGH_EDGE, "SD-RsRsh"),
        ("SD-RsRsh", ResidualProblem.LOW_EDGE, "SD-RsRsh-alpha-Rsh2"),
        ("SD-RsRsh", ResidualProblem.BOTH_EDGES, "SD-RsRsh-alpha-Rsh2"),
        ("SD-RsRsh-alpha-Rsh2", ResidualProblem.LOW_EDGE, None),
        ("SD-RsRsh-I_ph", ResidualProblem.LOW_EDGE, "SD-RsRsh-alpha-Rsh2-I_ph"),
        ("SD-0", ResidualProblem.NONE, None),
    ),
)
def test_select_child_model_follows_hierarchy(model_id, problem, expected_child) -> None:
    assert select_child_model(model_id, problem) == expected_child


def test_residual_window_size_respects_configured_bounds() -> None:
    narrow = ResidualOptions(local_window_min=3, local_window_max=7, local_window_fraction=0.5)
    wide = ResidualOptions(local_window_min=9, local_window_max=9, local_window_fraction=0.5)
    assert window_size(200, narrow) <= 7
    assert window_size(200, wide) == 9
    assert window_size(200, narrow) % 2 == 1


def test_local_expansion_points_bounds_range_growth() -> None:
    sample = _dark_sample()
    options = AutoPrefitOptions(adaptive_range=AdaptiveRangeOptions(local_expansion_points=2))
    lo, hi = 40, 160
    best = fit_and_diagnose(
        sample, "SD-RsRsh", RangeWindow(lo, hi, "test"), {}, options
    )
    expanded = locally_expand_range(sample, "SD-RsRsh", RangeWindow(lo, hi, "test"), best, options)
    assert expanded.point_count <= (hi - lo) + 2 * options.adaptive_range.local_expansion_points


def test_auto_prefit_characteristic_adapter_runs_end_to_end() -> None:
    voltage = np.linspace(-0.1, 0.9, 220)
    current = _add_noise(
        evaluate_current(
            voltage, 300.0, "SD-RsRsh", {"I0": 2.0e-9, "A": 1.6, "Rs": 1.2, "Rsh": 2.0e4}
        ),
        3,
    )
    result = auto_prefit_characteristic(voltage, current, 300.0)
    assert result["branch"] == "dark"
    assert result["selected"] is not None
    assert len(result["voltage"]) == len(result["prefit_current"])


def test_residual_runs_require_resolved_amplitude() -> None:
    from jfm_prefit.residual_diagnostics import _longest_same_sign_run_fraction
    assert _longest_same_sign_run_fraction(np.full(100, .01), .75) == 0.
    assert _longest_same_sign_run_fraction(np.ones(100), .75) == 1.
    assert _longest_same_sign_run_fraction(np.zeros(100)) == 0.
    assert _longest_same_sign_run_fraction(np.array([1., 1., 0., 1.]), .75) == .5


def test_smooth_local_diode_fit_is_valid() -> None:
    voltage = np.linspace(.2, .4, 81)
    current = evaluate_current(voltage, 300., "SD-0", {"I0": 2.e-10, "A": 1.88})
    sample, _ = prepare_sample(voltage, current, 300.)
    candidate = fit_and_diagnose(sample, "SD-0", RangeWindow(0, 81, "smooth"), {}, AutoPrefitOptions())
    assert candidate.is_valid
    assert candidate.parameters["A"].value == pytest.approx(1.88, rel=1.e-6)


def test_scaps_light_keeps_wide_five_parameter_fit() -> None:
    with np.load(ROOT.parent / "tests/fixtures/scaps-gold1/curves.npz") as curves:
        values = curves["phase-4-light-rs1-rsh100"].copy()
    # Phase 4 declares the same 1 cm2 normalization as the golden tests.
    result = auto_prefit_characteristic(values[:, 0], values[:, 1]*1.e-4, 300.)
    selected = result["selected"]
    assert selected is not None and selected["is_valid"]
    assert selected["model_id"] == "SD-RsRsh-I_ph"
    lo, hi = selected["fit_range"]["lo"], selected["fit_range"]["hi"]
    assert values[hi-1, 0]-values[lo, 0] >= .95*values[-1, 0]


@pytest.mark.parametrize("rs_ohm_cm2", [1, 10])
def test_scaps_dark_search_continues_past_short_diode_patch(rs_ohm_cm2) -> None:
    with np.load(ROOT.parent / "tests/fixtures/scaps-gold1/curves.npz") as curves:
        values = curves[f"phase-4-dark-rs{rs_ohm_cm2}-rsh100"].copy()
    result = auto_prefit_characteristic(values[:, 0], values[:, 1]*1.e-4, 300.)
    selected = result["selected"]
    assert selected is not None and selected["is_valid"]
    assert selected["model_id"] == "SD-RsRsh"
    lo, hi = selected["fit_range"]["lo"], selected["fit_range"]["hi"]
    assert values[hi-1, 0]-values[lo, 0] >= .95*values[-1, 0]
