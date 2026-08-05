from __future__ import annotations

import math

import numpy as np
from scipy.optimize import least_squares

from .model_registry import ModelSpec
from .types import ParameterEstimate, PrefitContext, Sample

_POSITIVE_PARAMETERS = {"I0", "A", "Rs", "Rsh", "Rsh2"}


def refine_parameters(
    context: PrefitContext,
    sample: Sample,
    spec: ModelSpec,
    lo: int,
    hi: int,
) -> None:
    """Shared nonlinear least-squares refinement step, restricted to
    `sample.voltage/current[lo:hi]`. Used both by the manual run_prefit()
    path and by the auto-selection search in adaptive_range.py."""
    if context.options.refinement_passes <= 0:
        return

    names = tuple(name for name in spec.parameter_names if name in context.estimates)
    initial_values = {
        name: (
            max(context.estimates[name].value, 1.0e-300)
            if name in _POSITIVE_PARAMETERS
            else context.estimates[name].value
        )
        for name in names
    }
    x0 = np.asarray(
        [
            math.log(initial_values[name])
            if name in _POSITIVE_PARAMETERS
            else initial_values[name]
            for name in names
        ],
        dtype=float,
    )
    voltage = sample.voltage[lo:hi]
    current = sample.current[lo:hi]
    sigma = context.options.noise_model.sigma(current)

    def unpack(values: np.ndarray) -> dict[str, float]:
        return {
            name: (
                float(math.exp(values[index]))
                if name in _POSITIVE_PARAMETERS
                else float(values[index])
            )
            for index, name in enumerate(names)
        }

    def residual(values: np.ndarray) -> np.ndarray:
        predicted = spec.current(
            voltage,
            sample.temperature_k,
            unpack(values),
            spec.characteristic_type == "dark",
        )
        return (predicted - current) / sigma

    try:
        result = least_squares(residual, x0, max_nfev=4000)
    except (ValueError, RuntimeError, OverflowError, FloatingPointError):
        context.warnings.append("nonlinear_refinement_failed")
        return
    if not result.success:
        context.warnings.append("nonlinear_refinement_failed")
        return

    refined = unpack(result.x)
    for name in names:
        context.estimates[name] = ParameterEstimate(
            refined[name], None, "nonlinear_refinement"
        )
    context.diagnostics["nonlinear_refinement_cost"] = float(result.cost)
