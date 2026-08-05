"""Regression tests against Prototypes/JFM/JFM-projects/HZB-project.yaml.jfm.yaml.

That project holds 16 real dark I(V) measurements (ivd_HZB25_T*) whose
range, prefit, and (Simplex) fit results were curated/verified by hand --
"tam sa poprawnie dopasowane charakterystyki". They are the ground truth
this module checks the pipeline against, in three independent respects:

1. Range detection (manual override AND automatic plateau/threshold
   detection) reproduces the curated fit_range exactly.
2. Prefit parameter values, run with the curated model_id + range, match
   the curated parameters closely.
3. Automatic model/range/branch selection (run_auto_prefit, given only raw
   data) picks the same model family a human previously judged correct.

(3) is the harder bar -- nobody ever ran an algorithm to produce these
labels, a person picked them from the dropdown. As of this writing the
auto-selector matches 13/16; the 3 known gaps are xfail, not hidden.
"""

from __future__ import annotations

from pathlib import Path
import sys

import numpy as np
import pytest
import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from jfm_import.dat_parser import parse_dat
from jfm_prefit.prefit import run_auto_prefit, run_prefit
from jfm_prefit.preprocessing import prepare_sample
from jfm_prefit.range_detection import detect_fit_range
from jfm_prefit.types import PrefitOptions, RangeOptions

PROJECT_PATH = ROOT.parent / "JFM-projects" / "HZB-project.yaml.jfm.yaml"
DATA_DIR = ROOT.parent / "example-data" / "real-measurement" / "dat"

# Known auto-selection gaps against this ground truth (see module docstring).
# T181/T209: auto-select settles on the simpler SD-RsRsh (still a valid fit)
# instead of promoting to the curated SD-RsRsh-alpha-Rsh2.
# T321: auto-select promotes to SD-RsRsh-alpha-Rsh2 where the curated model
# is the simpler SD-RsRsh.
_KNOWN_MODEL_SELECTION_MISMATCHES = {
    "ivd_HZB25_T181_L0",
    "ivd_HZB25_T209_L0",
    "ivd_HZB25_T321_L0",
}


def _load_ground_truth() -> list[dict]:
    if not PROJECT_PATH.exists():
        return []
    with PROJECT_PATH.open(encoding="utf-8") as handle:
        project = yaml.safe_load(handle)
    return [ch for ch in project["characteristics"] if ch["name"].startswith("ivd_HZB25")]


GROUND_TRUTH = _load_ground_truth()
_SKIP_REASON = f"ground truth project not found: {PROJECT_PATH}"


def _params(marks_for: dict[str, object] | None = None):
    marks_for = marks_for or {}
    return [
        pytest.param(ch, id=ch["name"], marks=marks_for.get(ch["name"], ()))
        for ch in GROUND_TRUTH
    ]


def _load_sample(characteristic: dict):
    name = characteristic["name"]
    temperature_k = float(characteristic["temperature_K"])
    result = parse_dat(DATA_DIR / f"{name}.dat")
    data = np.asarray(result["curves"][0]["data"], dtype=float)
    voltage, current = data[:, 0], data[:, 1]
    sample, _ = prepare_sample(voltage, current, temperature_k, "dark")
    return sample


def _range_options(prefit: dict) -> RangeOptions:
    return RangeOptions(
        manual_lower_voltage=prefit.get("manual_lower_voltage"),
        manual_upper_voltage=prefit.get("manual_upper_voltage"),
    )


@pytest.mark.skipif(not GROUND_TRUTH, reason=_SKIP_REASON)
@pytest.mark.parametrize("characteristic", _params())
def test_range_detection_matches_curated_project(characteristic: dict) -> None:
    sample = _load_sample(characteristic)
    prefit = characteristic["prefit"]
    fit_range = detect_fit_range(sample, _range_options(prefit)).fit_range
    expected = prefit["fit_range"]
    assert fit_range.lo == expected["lower_index"]
    assert fit_range.hi == expected["upper_index"]
    assert fit_range.reason == expected["reason"]


@pytest.mark.skipif(not GROUND_TRUTH, reason=_SKIP_REASON)
@pytest.mark.parametrize("characteristic", _params())
def test_prefit_parameters_match_curated_project(characteristic: dict) -> None:
    sample = _load_sample(characteristic)
    prefit = characteristic["prefit"]
    options = PrefitOptions(range_options=_range_options(prefit))
    result = run_prefit(sample, prefit["model"], options)
    for name, expected in prefit["parameters"].items():
        assert result.parameters[name].value == pytest.approx(
            expected["value"], rel=1.0e-3, abs=1.0e-25
        )


@pytest.mark.skipif(not GROUND_TRUTH, reason=_SKIP_REASON)
@pytest.mark.parametrize("characteristic", _params())
def test_fit_parameters_match_curated_project(characteristic: dict) -> None:
    """The curated "fit" section comes from the C++ Simplex engine, which
    this pytest environment cannot run directly (no embedded Python/GPU
    runtime here). In this project the Simplex fit converges to
    essentially the prefit nonlinear_refinement optimum already (values
    coincide to the parameters' own precision -- compare prefit vs fit
    blocks in HZB-project.yaml.jfm.yaml), so re-running prefit and
    comparing against the "fit" parameters is a faithful indirect check."""
    sample = _load_sample(characteristic)
    prefit = characteristic["prefit"]
    fit = characteristic["fit"]
    options = PrefitOptions(range_options=_range_options(prefit))
    result = run_prefit(sample, prefit["model"], options)
    for name, expected in fit["parameters"].items():
        assert result.parameters[name].value == pytest.approx(
            expected["value"], rel=1.0e-3, abs=1.0e-25
        )


@pytest.mark.skipif(not GROUND_TRUTH, reason=_SKIP_REASON)
@pytest.mark.parametrize(
    "characteristic",
    _params({name: pytest.mark.xfail(reason=f"auto model selection gap, see module docstring", strict=True) for name in _KNOWN_MODEL_SELECTION_MISMATCHES}),
)
def test_auto_prefit_selects_curated_model(characteristic: dict) -> None:
    sample = _load_sample(characteristic)
    expected_model = characteristic["prefit"]["model"]

    result = run_auto_prefit(sample)

    assert result.branch == "dark"
    assert result.selected is not None
    assert result.selected.model_id == expected_model
