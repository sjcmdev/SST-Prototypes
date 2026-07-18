from __future__ import annotations

from dataclasses import replace
import math

import numpy as np
from scipy.optimize import least_squares

from .blocks import estimate_photocurrent
from .current_models import evaluate_current
from .model_registry import BLOCK_REGISTRY, MODEL_REGISTRY
from .range_detection import detect_fit_range
from .types import ParameterEstimate, PrefitContext, PrefitOptions, PrefitResult, Sample


class PrefitError(RuntimeError):
    pass


def run_prefit(sample: Sample, model_id: str, options: PrefitOptions | None = None) -> PrefitResult:
    try:
        spec = MODEL_REGISTRY[model_id]
    except KeyError as error:
        raise PrefitError(f"unknown model_id={model_id!r}; available={sorted(MODEL_REGISTRY)}") from error

    options = options or PrefitOptions()
    working_sample = sample
    photocurrent: ParameterEstimate | None = None
    if "I_ph" in spec.parameter_names:
        photocurrent = estimate_photocurrent(sample)
        working_sample = replace(sample, current=sample.current + photocurrent.value)

    fit_range = detect_fit_range(working_sample, options.range_options)
    context = PrefitContext(working_sample, fit_range.segmentation, options)
    for _ in range(max(1, options.refinement_passes)):
        for block_name in spec.blocks:
            BLOCK_REGISTRY[block_name](context)
    if photocurrent is not None:
        context.estimates["I_ph"] = photocurrent
    for name, value in spec.defaults.items():
        context.estimates.setdefault(name, ParameterEstimate(value, None, "default"))

    missing = [name for name in spec.parameter_names if name not in context.estimates]
    if missing:
        raise PrefitError(f"model {model_id!r} did not estimate {missing}")
    _refine_parameters(context, sample, model_id, spec.parameter_names, fit_range.fit_range.lo, fit_range.fit_range.hi)
    warnings = tuple(dict.fromkeys((*fit_range.warnings, *context.warnings)))
    return PrefitResult(
        model_id,
        {name: context.estimates[name] for name in spec.parameter_names},
        fit_range,
        warnings,
        dict(context.diagnostics),
    )


def _refine_parameters(
    context: PrefitContext,
    sample: Sample,
    model_id: str,
    parameter_names: tuple[str, ...],
    lo: int,
    hi: int,
) -> None:
    if context.options.refinement_passes <= 0:
        return

    names = tuple(name for name in parameter_names if name in context.estimates)
    positive = {"I0", "A", "Rs", "Rsh", "Rsh2"}
    initial_values = {name: max(context.estimates[name].value, 1.0e-300) if name in positive else context.estimates[name].value for name in names}
    x0 = np.asarray([math.log(initial_values[name]) if name in positive else initial_values[name] for name in names], dtype=float)
    voltage = sample.voltage[lo:hi]
    current = sample.current[lo:hi]
    sigma = context.options.noise_model.sigma(current)

    def unpack(values: np.ndarray) -> dict[str, float]:
        return {name: float(math.exp(values[index])) if name in positive else float(values[index]) for index, name in enumerate(names)}

    def residual(values: np.ndarray) -> np.ndarray:
        return (evaluate_current(voltage, sample.temperature_k, model_id, unpack(values)) - current) / sigma

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
        context.estimates[name] = ParameterEstimate(refined[name], None, "nonlinear_refinement")
    context.diagnostics["nonlinear_refinement_cost"] = float(result.cost)
