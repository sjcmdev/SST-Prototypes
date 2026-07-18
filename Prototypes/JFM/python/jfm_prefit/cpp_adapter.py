from __future__ import annotations

from collections.abc import Sequence
from typing import Any, cast

import numpy as np

from .current_models import evaluate_current
from .prefit import run_prefit
from .preprocessing import prepare_sample
from .types import (
    CharacteristicType,
    NoiseModel,
    PrefitOptions,
    RangeOptions,
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
