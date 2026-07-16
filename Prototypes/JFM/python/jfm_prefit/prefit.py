from __future__ import annotations

from dataclasses import replace

from .blocks import estimate_photocurrent
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
    warnings = tuple(dict.fromkeys((*fit_range.warnings, *context.warnings)))
    return PrefitResult(
        model_id,
        {name: context.estimates[name] for name in spec.parameter_names},
        fit_range,
        warnings,
        dict(context.diagnostics),
    )
