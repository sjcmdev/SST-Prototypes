from __future__ import annotations

from collections.abc import Sequence
from typing import Any, cast

import numpy as np

from .current_models import evaluate_current
from .prefit import run_auto_prefit, run_prefit
from .preprocessing import prepare_sample
from .types import (
    AdaptiveRangeOptions,
    AutoPrefitOptions,
    CandidateSummary,
    CharacteristicType,
    ModelSelectionOptions,
    NoiseModel,
    PhotoDetectionOptions,
    PrefitOptions,
    RangeOptions,
    ResidualOptions,
    ResistanceOptions,
)


def prefit_characteristic(
    voltage: Sequence[float],
    current: Sequence[float],
    temperature_k: float,
    model_id: str,
    characteristic_type: str = "dark",
    current_unit: str = "A",
    options: dict[str, Any] | None = None,
) -> dict[str, Any]:
    sample, preprocessing_warnings = prepare_sample(
        np.asarray(voltage, dtype=float),
        np.asarray(current, dtype=float),
        float(temperature_k),
        cast(CharacteristicType, characteristic_type),
        current_unit,
    )
    result = run_prefit(sample, model_id, _options_from_dict(options or {}))
    parameter_values = {
        name: estimate.value for name, estimate in result.parameters.items()
    }
    predicted = evaluate_current(
        sample.voltage, sample.temperature_k, model_id, parameter_values
    )
    fit_window = result.fit_range.fit_range
    return {
        "model_id": result.model_id,
        "parameters": {
            name: {
                "value": estimate.value,
                "stderr": estimate.stderr,
                "source": estimate.source,
                "warnings": list(estimate.warnings),
            }
            for name, estimate in result.parameters.items()
        },
        "fit_range": {
            "lo": fit_window.lo,
            "hi": fit_window.hi,
            "voltage_min": float(sample.voltage[fit_window.lo]),
            "voltage_max": float(sample.voltage[fit_window.hi - 1]),
            "reason": fit_window.reason,
        },
        "segmentation": {
            name: (
                None
                if window is None
                else {"lo": window.lo, "hi": window.hi, "reason": window.reason}
            )
            for name, window in (
                ("shunt", result.fit_range.segmentation.shunt),
                ("exponential", result.fit_range.segmentation.exponential),
                ("series", result.fit_range.segmentation.series),
            )
        },
        "voltage": sample.voltage.tolist(),
        "current": sample.current.tolist(),
        "prefit_current": predicted.tolist(),
        "warnings": list(dict.fromkeys((*preprocessing_warnings, *result.warnings))),
        "diagnostics": result.diagnostics,
    }


def _options_from_dict(raw: dict[str, Any]) -> PrefitOptions:
    range_options = RangeOptions(**raw.get("range_options", {}))
    noise_model = NoiseModel(**raw.get("noise_model", {}))
    resistance_options = ResistanceOptions(**raw.get("resistance_options", {}))
    return PrefitOptions(
        range_options=range_options,
        noise_model=noise_model,
        refinement_passes=int(raw.get("refinement_passes", 1)),
        resistance_options=resistance_options,
    )


def auto_prefit_characteristic(
    voltage: Sequence[float],
    current: Sequence[float],
    temperature_k: float,
    current_unit: str = "A",
    options: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Automatic branch/model/range selection entry point (docs/model-detection.md).

    Unlike prefit_characteristic(), the caller does not pick a model_id or
    characteristic_type -- both are detected. Every candidate evaluated
    internally is a fast pure-Python fit (see adaptive_range.py); no C++
    Simplex/GPU call happens inside this function.
    """
    raw = options or {}
    sample, preprocessing_warnings = prepare_sample(
        np.asarray(voltage, dtype=float),
        np.asarray(current, dtype=float),
        float(temperature_k),
        "dark",
        current_unit,
    )
    auto_options = AutoPrefitOptions(
        range_options=RangeOptions(**raw.get("range_options", {})),
        noise_model=NoiseModel(**raw.get("noise_model", {})),
        resistance_options=ResistanceOptions(**raw.get("resistance_options", {})),
        photo_detection=PhotoDetectionOptions(**raw.get("photo_detection", {})),
        residual=ResidualOptions(**raw.get("residual", {})),
        adaptive_range=AdaptiveRangeOptions(**raw.get("adaptive_range", {})),
        selection=ModelSelectionOptions(**raw.get("selection", {})),
        photo_branch_mode=raw.get("photo_branch_mode", "auto"),
    )
    result = run_auto_prefit(sample, auto_options)

    def _candidate_dict(candidate: CandidateSummary) -> dict[str, Any]:
        return {
            "model_id": candidate.model_id,
            "fit_range": {
                "lo": candidate.fit_range.lo,
                "hi": candidate.fit_range.hi,
                "reason": candidate.fit_range.reason,
            },
            "point_count": candidate.point_count,
            "coverage_fraction": candidate.coverage_fraction,
            "parameter_count": candidate.parameter_count,
            "reduced_chi2": candidate.reduced_chi2,
            "is_valid": candidate.is_valid,
            "residual_problem": candidate.residual_problem.value,
            "parameters": {
                name: {
                    "value": estimate.value,
                    "stderr": estimate.stderr,
                    "source": estimate.source,
                }
                for name, estimate in candidate.parameters.items()
            },
            "warnings": list(candidate.warnings),
        }

    selected = result.selected
    predicted = (
        evaluate_current(
            sample.voltage,
            sample.temperature_k,
            selected.model_id,
            {name: estimate.value for name, estimate in selected.parameters.items()},
        ).tolist()
        if selected is not None
        else []
    )

    return {
        "branch": result.branch,
        "photo_detection": {
            "is_light": result.photo_detection.is_light,
            "initial_i_ph": result.photo_detection.initial_i_ph,
            "z_score": result.photo_detection.z_score,
            "flatness": result.photo_detection.flatness,
            "sign_coherence": result.photo_detection.sign_coherence,
            "confidence": result.photo_detection.confidence,
            "warnings": list(result.photo_detection.warnings),
        },
        "selected": _candidate_dict(selected) if selected is not None else None,
        "tested_candidates": [_candidate_dict(c) for c in result.tested_candidates],
        "selection_reason": result.selection_reason,
        "voltage": sample.voltage.tolist(),
        "current": sample.current.tolist(),
        "prefit_current": predicted,
        "warnings": list(dict.fromkeys((*preprocessing_warnings, *result.warnings))),
    }
