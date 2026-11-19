from __future__ import annotations

from dataclasses import replace

from .adaptive_range import optimize_range_for_model
from .blocks import estimate_photocurrent
from .model_registry import BLOCK_REGISTRY, MODEL_REGISTRY
from .model_selection import (
    choose_better_candidate,
    fallback_next_model,
    select_child_model,
    should_promote_to_child,
)
from .photo_detection import detect_photo_branch
from .range_detection import detect_fit_range
from .refinement import refine_parameters
from .types import (
    AutoPrefitOptions,
    AutoPrefitResult,
    ParameterEstimate,
    PrefitContext,
    PrefitOptions,
    PrefitResult,
    Sample,
)


class PrefitError(RuntimeError):
    pass


def run_auto_prefit(
    sample: Sample, options: AutoPrefitOptions | None = None
) -> AutoPrefitResult:
    """Automatic branch/model/range selection (docs/model-detection.md §11).

    Every candidate fit in the search is a fast, pure-Python prefit + scipy
    refinement (see adaptive_range.fit_and_diagnose) -- the C++ Simplex/GPU
    fit engine is never invoked here. Callers run that once, afterwards, on
    the returned `selected.model_id` + `selected.fit_range`.
    """
    options = options or AutoPrefitOptions()

    photo = detect_photo_branch(sample, options.photo_detection)
    if options.photo_branch_mode == "force_dark":
        branch = "dark"
    elif options.photo_branch_mode == "force_light":
        branch = "light"
    else:
        branch = "light" if photo.is_light else "dark"

    branch_sample = replace(sample, characteristic_type=branch)
    eligible_range = detect_fit_range(branch_sample, options.range_options).fit_range
    current_model: str | None = "SD-0-I_ph" if branch == "light" else "SD-0"
    initial_parameters: dict[str, float] = {}
    if branch == "light":
        initial_parameters["I_ph"] = photo.initial_i_ph

    tested: list = []
    best = None

    while current_model is not None:
        candidate, structural_problem = optimize_range_for_model(
            branch_sample, current_model, initial_parameters, options
        )
        tested.append(candidate)
        if candidate.is_valid:
            best = choose_better_candidate(best, candidate, options.adaptive_range)

        next_model = select_child_model(current_model, structural_problem)
        # A valid narrow patch must still compete with a wider child fit.
        if next_model is None and (not candidate.is_valid
                or candidate.fit_range.lo > eligible_range.lo
                or candidate.fit_range.hi < eligible_range.hi):
            next_model = fallback_next_model(current_model)
        if next_model is None:
            break

        child_parameters = {
            name: estimate.value for name, estimate in candidate.parameters.items()
        }
        if branch == "light":
            child_parameters.setdefault("I_ph", photo.initial_i_ph)
        child_candidate, _ = optimize_range_for_model(
            branch_sample, next_model, child_parameters, options
        )
        tested.append(child_candidate)

        if not candidate.is_valid:
            # Nothing valid yet to protect -- keep climbing the hierarchy
            # unconditionally. should_promote_to_child()'s "must earn the
            # extra parameter" rule only makes sense once there's a working
            # simpler baseline to compare against.
            current_model = next_model
            if child_candidate.is_valid:
                best = choose_better_candidate(best, child_candidate, options.adaptive_range)
        elif should_promote_to_child(candidate, child_candidate, options.selection):
            current_model = next_model
            if child_candidate.is_valid:
                best = choose_better_candidate(best, child_candidate, options.adaptive_range)
        else:
            break

    if best is not None:
        reason = (
            f"Selected {branch} branch (confidence={photo.confidence}); "
            f"model {best.model_id} covers {best.point_count} points "
            f"({best.coverage_fraction:.0%}), reduced chi2={best.reduced_chi2:.4g}."
        )
    else:
        reason = (
            f"Selected {branch} branch (confidence={photo.confidence}); "
            "no candidate model/range met the validity criteria."
        )

    return AutoPrefitResult(
        branch=branch,
        photo_detection=photo,
        selected=best,
        tested_candidates=tuple(tested),
        selection_reason=reason,
    )


def run_prefit(
    sample: Sample, model_id: str, options: PrefitOptions | None = None
) -> PrefitResult:
    try:
        spec = MODEL_REGISTRY[model_id]
    except KeyError as error:
        raise PrefitError(
            f"unknown model_id={model_id!r}; available={sorted(MODEL_REGISTRY)}"
        ) from error

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
    refine_parameters(
        context, sample, spec, fit_range.fit_range.lo, fit_range.fit_range.hi
    )
    warnings = tuple(dict.fromkeys((*fit_range.warnings, *context.warnings)))
    return PrefitResult(
        model_id,
        {name: context.estimates[name] for name in spec.parameter_names},
        fit_range,
        warnings,
        dict(context.diagnostics),
    )
