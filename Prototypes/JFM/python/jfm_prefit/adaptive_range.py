from __future__ import annotations

from dataclasses import replace
import math

import numpy as np

from .blocks import estimate_photocurrent
from .model_registry import BLOCK_REGISTRY, MODEL_REGISTRY
from .range_detection import _coarse_segmentation, detect_fit_range
from .refinement import refine_parameters
from .residual_diagnostics import (
    classify_residual_problem,
    compute_local_error_profile,
    compute_normalized_residuals,
    compute_region_scores,
    global_residual_scale,
    window_size,
)
from .types import (
    AutoPrefitOptions,
    CandidateSummary,
    ParameterEstimate,
    PrefitContext,
    PrefitOptions,
    RangeWindow,
    RegionScores,
    ResidualProblem,
    Sample,
)

_POSITIVE_PARAMETERS = {"I0", "A", "Rs", "Rsh", "Rsh2"}


def _invalid_candidate(model_id: str, fit_range: RangeWindow, reason: str) -> CandidateSummary:
    empty_regions = RegionScores(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
    return CandidateSummary(
        model_id=model_id,
        fit_range=fit_range,
        point_count=fit_range.hi - fit_range.lo,
        coverage_fraction=0.0,
        parameter_count=len(MODEL_REGISTRY[model_id].parameter_names),
        chi_squared=math.inf,
        reduced_chi2=math.inf,
        is_valid=False,
        residual_problem=ResidualProblem.INTERIOR,
        regions=empty_regions,
        parameters={},
        warnings=(reason,),
    )


def fit_and_diagnose(
    sample: Sample,
    model_id: str,
    fit_range: RangeWindow,
    initial_parameters: dict[str, float],
    options: AutoPrefitOptions,
) -> CandidateSummary:
    """Pure-Python prefit + nonlinear refinement restricted to `fit_range`.

    Used by the auto model/range search loop, which calls this dozens of
    times per characteristic. Deliberately never invokes the C++
    Simplex/GPU fit engine -- that only runs once, on the final selected
    model+range, after this search has finished."""
    spec = MODEL_REGISTRY[model_id]
    lo, hi = fit_range.lo, fit_range.hi
    point_count = hi - lo
    if point_count < options.adaptive_range.min_fit_points or point_count <= len(
        spec.parameter_names
    ):
        return _invalid_candidate(model_id, fit_range, "too_few_points")

    # The shunt/series/semilog blocks assume a dark-like, mostly-positive
    # exponential current (like run_prefit's `working_sample`); for I_ph
    # models they must see the photocurrent-shifted curve, not the raw
    # light curve, or they misread the offset as e.g. a shunt.
    block_sample = sample
    if "I_ph" in spec.parameter_names:
        i_ph_guess = initial_parameters.get("I_ph")
        if i_ph_guess is None:
            i_ph_guess = estimate_photocurrent(sample).value
        block_sample = replace(sample, current=sample.current + i_ph_guess)

    segmentation = _coarse_segmentation(lo, hi)
    context = PrefitContext(
        block_sample,
        segmentation,
        PrefitOptions(
            range_options=options.range_options,
            noise_model=options.noise_model,
            refinement_passes=1,
            resistance_options=options.resistance_options,
        ),
    )
    for block_name in spec.blocks:
        BLOCK_REGISTRY[block_name](context)
    for name, value in spec.defaults.items():
        context.estimates.setdefault(name, ParameterEstimate(value, None, "default"))
    if "I_ph" in spec.parameter_names:
        context.estimates.setdefault(
            "I_ph", ParameterEstimate(i_ph_guess, None, "photocurrent_interp")
        )
    for name, value in initial_parameters.items():
        if name not in spec.parameter_names:
            continue
        if name not in context.estimates or context.estimates[name].source.endswith(
            "_fallback"
        ):
            context.estimates[name] = ParameterEstimate(value, None, "warm_start")

    missing = [name for name in spec.parameter_names if name not in context.estimates]
    if missing:
        return _invalid_candidate(model_id, fit_range, f"missing_parameters:{missing}")

    refine_parameters(context, sample, spec, lo, hi)
    refine_ok = "nonlinear_refinement_failed" not in context.warnings

    parameters = {name: context.estimates[name] for name in spec.parameter_names}
    values = {name: estimate.value for name, estimate in parameters.items()}
    parameters_finite = all(
        math.isfinite(value) and (name not in _POSITIVE_PARAMETERS or value > 0.0)
        for name, value in values.items()
    )

    residuals = compute_normalized_residuals(
        sample, spec, values, lo, hi, options.noise_model
    )
    window = window_size(point_count, options.residual)
    error, bias = compute_local_error_profile(residuals, window)
    regions = compute_region_scores(residuals, error, bias, window, options.residual)
    problem = classify_residual_problem(regions, global_residual_scale(error), options.residual)

    dof = max(1, point_count - len(spec.parameter_names))
    chi_squared = float(np.sum(residuals**2))
    reduced_chi2 = chi_squared / dof

    is_valid = (
        refine_ok
        and parameters_finite
        and problem == ResidualProblem.NONE
        and regions.max_same_sign_run_fraction <= options.residual.max_same_sign_run_fraction
    )

    return CandidateSummary(
        model_id=model_id,
        fit_range=fit_range,
        point_count=point_count,
        coverage_fraction=point_count / sample.voltage.size,
        parameter_count=len(spec.parameter_names),
        chi_squared=chi_squared,
        reduced_chi2=reduced_chi2,
        is_valid=is_valid,
        residual_problem=problem,
        regions=regions,
        parameters=parameters,
        warnings=tuple(context.warnings),
    )


def _parameter_values(candidate: CandidateSummary) -> dict[str, float]:
    return {name: estimate.value for name, estimate in candidate.parameters.items()}


def _search_bound(
    sample: Sample,
    model_id: str,
    lo: int,
    hi: int,
    side: str,
    parameters: dict[str, float],
    options: AutoPrefitOptions,
) -> tuple[int, int, CandidateSummary]:
    """Bisect+jitter the `side` ('lo' or 'hi') boundary until the fit at that
    boundary stops showing a problem on that side (§8.3), then jitter around
    the found boundary by `jitter_radius_points`."""
    min_points = options.adaptive_range.min_fit_points
    bad_problem = ResidualProblem.LOW_EDGE if side == "lo" else ResidualProblem.HIGH_EDGE

    if side == "hi":
        good, bad = lo + min_points, hi
    else:
        good, bad = hi - min_points, lo
    if good == bad:
        return lo, hi, fit_and_diagnose(
            sample, model_id, RangeWindow(lo, hi, "search"), parameters, options
        )

    def _evaluate(boundary: int) -> CandidateSummary:
        window = RangeWindow(lo, boundary, "search") if side == "hi" else RangeWindow(
            boundary, hi, "search"
        )
        return fit_and_diagnose(sample, model_id, window, parameters, options)

    best_good = _evaluate(good)
    while good != bad:
        midpoint = (good + bad) // 2
        if midpoint == good or midpoint == bad:
            break
        candidate = _evaluate(midpoint)
        if candidate.residual_problem in (bad_problem, ResidualProblem.BOTH_EDGES):
            bad = midpoint
        else:
            good = midpoint
            best_good = candidate

    jitter = options.adaptive_range.jitter_radius_points
    best_boundary = good
    for offset in range(-jitter, jitter + 1):
        candidate_boundary = good + offset
        if side == "hi":
            if not (lo + min_points <= candidate_boundary <= hi):
                continue
        else:
            if not (lo <= candidate_boundary <= hi - min_points):
                continue
        candidate = _evaluate(candidate_boundary)
        if not candidate.is_valid:
            continue
        better = (
            (side == "hi" and candidate_boundary > best_boundary)
            or (side == "lo" and candidate_boundary < best_boundary)
            or best_good.is_valid is False
        )
        if better:
            best_boundary = candidate_boundary
            best_good = candidate

    if side == "hi":
        return lo, best_boundary, best_good
    return best_boundary, hi, best_good


def locally_expand_range(
    sample: Sample,
    model_id: str,
    fit_range: RangeWindow,
    best: CandidateSummary,
    options: AutoPrefitOptions,
) -> CandidateSummary:
    """Try extending the found range by up to `local_expansion_points` on
    each side (§8.4), keeping the extension only while the fit stays valid."""
    lo, hi = fit_range.lo, fit_range.hi
    n_points = sample.voltage.size
    expansion = options.adaptive_range.local_expansion_points

    for _ in range(expansion):
        if hi >= n_points:
            break
        candidate = fit_and_diagnose(
            sample,
            model_id,
            RangeWindow(lo, hi + 1, "local_expansion"),
            _parameter_values(best),
            options,
        )
        if not candidate.is_valid:
            break
        hi += 1
        best = candidate

    for _ in range(expansion):
        if lo <= 0:
            break
        candidate = fit_and_diagnose(
            sample,
            model_id,
            RangeWindow(lo - 1, hi, "local_expansion"),
            _parameter_values(best),
            options,
        )
        if not candidate.is_valid:
            break
        lo -= 1
        best = candidate

    return best


def optimize_range_for_model(
    sample: Sample,
    model_id: str,
    initial_parameters: dict[str, float],
    options: AutoPrefitOptions,
) -> tuple[CandidateSummary, ResidualProblem]:
    """Returns (best-effort candidate at the range this model can actually
    support, structural_problem). `structural_problem` is the residual
    diagnosis at the *initial* wide range -- it drives which parameter
    select_child_model() adds next. Using the post-bisection diagnosis for
    that decision would be wrong: a too-simple model that fails almost
    everywhere collapses under bisection to a tiny, unrepresentative
    leftover window (see docs/model-detection.md §9) whose local diagnosis
    no longer reflects which term is missing."""
    initial = detect_fit_range(sample, options.range_options).fit_range
    lo, hi = initial.lo, initial.hi

    result = fit_and_diagnose(
        sample, model_id, RangeWindow(lo, hi, initial.reason), initial_parameters, options
    )
    structural_problem = result.residual_problem
    parameters = _parameter_values(result) or initial_parameters

    previous_range: tuple[int, int] | None = None
    for _ in range(options.adaptive_range.max_iterations):
        if result.is_valid or (lo, hi) == previous_range:
            break
        previous_range = (lo, hi)
        problem = result.residual_problem
        if problem == ResidualProblem.LOW_EDGE:
            lo, hi, result = _search_bound(sample, model_id, lo, hi, "lo", parameters, options)
        elif problem == ResidualProblem.HIGH_EDGE:
            lo, hi, result = _search_bound(sample, model_id, lo, hi, "hi", parameters, options)
        elif problem == ResidualProblem.BOTH_EDGES:
            side = "lo" if result.regions.low_error >= result.regions.high_error else "hi"
            lo, hi, result = _search_bound(sample, model_id, lo, hi, side, parameters, options)
        else:
            break
        parameters = _parameter_values(result) or parameters

    best = locally_expand_range(
        sample, model_id, RangeWindow(lo, hi, result.fit_range.reason), result, options
    )
    return best, structural_problem
