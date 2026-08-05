from __future__ import annotations

import math

from .model_registry import DARK_MODEL_CHILDREN, LIGHT_MODEL_CHILDREN, MODEL_REGISTRY
from .types import (
    AdaptiveRangeOptions,
    CandidateSummary,
    ModelSelectionOptions,
    ResidualProblem,
)

_PROBLEM_SIDE = {
    ResidualProblem.LOW_EDGE: "low",
    ResidualProblem.HIGH_EDGE: "high",
    ResidualProblem.BOTH_EDGES: "both",
}


def select_child_model(current_model_id: str, residual_problem: ResidualProblem) -> str | None:
    side = _PROBLEM_SIDE.get(residual_problem)
    if side is None:
        return None
    table = (
        LIGHT_MODEL_CHILDREN
        if MODEL_REGISTRY[current_model_id].has_i_ph
        else DARK_MODEL_CHILDREN
    )
    return table.get(current_model_id, {}).get(side)


def _aicc(candidate: CandidateSummary) -> float:
    n = candidate.point_count
    k = candidate.parameter_count
    aic = candidate.chi_squared + 2 * k
    denominator = n - k - 1
    if denominator <= 0:
        return math.inf
    return aic + (2 * k * (k + 1)) / denominator


def should_promote_to_child(
    parent: CandidateSummary, child: CandidateSummary, options: ModelSelectionOptions
) -> bool:
    """§9.3: a child model must earn its extra parameter -- lower chi2 alone
    is not enough."""
    if not child.is_valid:
        return False

    range_gain_points = child.point_count - parent.point_count
    range_gain_fraction = range_gain_points / max(1, parent.point_count)
    if (
        range_gain_points >= options.min_range_gain_points
        or range_gain_fraction >= options.min_range_gain_fraction
    ):
        return True

    if parent.residual_problem != ResidualProblem.NONE and child.residual_problem == ResidualProblem.NONE:
        return True

    if parent.fit_range.lo == child.fit_range.lo and parent.fit_range.hi == child.fit_range.hi:
        if _aicc(child) - _aicc(parent) <= -options.minimum_aicc_improvement:
            return True

    return False


def _residual_structure_score(candidate: CandidateSummary) -> float:
    regions = candidate.regions
    return max(regions.low_error, regions.high_error, regions.interior_error) + max(
        abs(regions.low_bias), abs(regions.high_bias), abs(regions.interior_bias)
    )


def _ranges_comparable(
    a: CandidateSummary, b: CandidateSummary, options: AdaptiveRangeOptions
) -> bool:
    diff = abs(a.point_count - b.point_count)
    fraction = diff / max(a.point_count, b.point_count, 1)
    return diff <= options.equivalent_range_points or fraction <= options.equivalent_range_fraction


def choose_better_candidate(
    current: CandidateSummary | None,
    candidate: CandidateSummary,
    options: AdaptiveRangeOptions,
) -> CandidateSummary | None:
    """§10 selection order: reject invalid, prefer larger range, then fewer
    parameters, then less residual structure, then lower reduced chi2 --
    only among candidates with comparable range coverage."""
    if not candidate.is_valid:
        return current
    if current is None:
        return candidate

    if _ranges_comparable(current, candidate, options):
        if candidate.parameter_count != current.parameter_count:
            return candidate if candidate.parameter_count < current.parameter_count else current
        candidate_structure = _residual_structure_score(candidate)
        current_structure = _residual_structure_score(current)
        if not math.isclose(candidate_structure, current_structure, rel_tol=1e-9):
            return candidate if candidate_structure < current_structure else current
        return candidate if candidate.reduced_chi2 < current.reduced_chi2 else current

    return candidate if candidate.point_count > current.point_count else current
