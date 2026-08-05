from __future__ import annotations

import math

import numpy as np

from .model_registry import ModelSpec
from .types import NoiseModel, RegionScores, ResidualOptions, ResidualProblem, Sample


def compute_normalized_residuals(
    sample: Sample,
    spec: ModelSpec,
    parameters: dict[str, float],
    lo: int,
    hi: int,
    noise_model: NoiseModel,
) -> np.ndarray:
    voltage = sample.voltage[lo:hi]
    current = sample.current[lo:hi]
    predicted = spec.current(
        voltage, sample.temperature_k, parameters, spec.characteristic_type == "dark"
    )
    sigma = noise_model.sigma(current)
    return (predicted - current) / sigma


def window_size(point_count: int, options: ResidualOptions) -> int:
    raw = round(options.local_window_fraction * point_count)
    clamped = int(np.clip(raw, options.local_window_min, options.local_window_max))
    clamped = min(clamped, point_count if point_count % 2 == 1 else point_count - 1)
    clamped = max(1, clamped)
    return clamped if clamped % 2 == 1 else clamped + 1


def compute_local_error_profile(
    residuals: np.ndarray, window_size: int
) -> tuple[np.ndarray, np.ndarray]:
    n = residuals.size
    if n == 0:
        return np.zeros(0), np.zeros(0)
    half = window_size // 2
    triangle = 1.0 + half - np.abs(np.arange(-half, half + 1, dtype=float))
    error = np.empty(n)
    bias = np.empty(n)
    for i in range(n):
        lo = max(0, i - half)
        hi = min(n, i + half + 1)
        weights = triangle[lo - i + half : hi - i + half]
        window = residuals[lo:hi]
        weight_sum = float(np.sum(weights))
        error[i] = math.sqrt(float(np.sum(weights * window * window)) / weight_sum)
        bias[i] = float(np.sum(weights * window)) / weight_sum
    return error, bias


def _longest_same_sign_run_fraction(residuals: np.ndarray) -> float:
    n = residuals.size
    if n == 0:
        return 0.0
    signs = np.sign(residuals)
    signs[signs == 0.0] = 1.0
    longest = current = 1
    for i in range(1, n):
        if signs[i] == signs[i - 1]:
            current += 1
            longest = max(longest, current)
        else:
            current = 1
    return longest / n


def compute_region_scores(
    residuals: np.ndarray,
    error: np.ndarray,
    bias: np.ndarray,
    window_size: int,
    options: ResidualOptions,
) -> RegionScores:
    n = residuals.size
    edge_points = max(window_size, math.ceil(options.edge_fraction * n)) if n else 0
    edge_points = min(edge_points, n // 2) if n >= 2 else 0
    low = slice(0, edge_points)
    high = slice(n - edge_points, n)
    interior = slice(edge_points, n - edge_points)

    def _mean_or_zero(values: np.ndarray, region: slice) -> float:
        chunk = values[region]
        return float(np.mean(chunk)) if chunk.size else 0.0

    return RegionScores(
        low_error=_mean_or_zero(error, low),
        high_error=_mean_or_zero(error, high),
        interior_error=_mean_or_zero(error, interior),
        low_bias=_mean_or_zero(bias, low),
        high_bias=_mean_or_zero(bias, high),
        interior_bias=_mean_or_zero(bias, interior),
        max_same_sign_run_fraction=_longest_same_sign_run_fraction(residuals),
    )


def global_residual_scale(error: np.ndarray) -> float:
    """Typical local error magnitude across the whole fit range (median of
    the per-point windowed RMS `error`), floored at 1.0. Region error/bias
    scores are judged *relative to this*, not against an absolute constant
    -- the assumed NoiseModel sigma is just a modeling convention (e.g.
    sigma_rel=0.01) and real measurement noise is often larger than it, so
    an absolute threshold false-flags genuinely good fits whenever real
    noise exceeds the assumed sigma. The floor at 1.0 keeps this a no-op
    when the assumed sigma already matches (or overestimates) the real
    noise -- i.e. it only ever *raises* the effective threshold to match
    reality, never tightens it below the original calibrated-sigma case."""
    if error.size == 0:
        return 1.0
    return max(float(np.median(error)), 1.0)


def classify_residual_problem(
    regions: RegionScores, global_scale: float, options: ResidualOptions
) -> ResidualProblem:
    error_threshold = options.local_rms_threshold * global_scale
    bias_threshold = options.local_bias_threshold * global_scale

    def _bad(error: float, bias: float) -> bool:
        return error > error_threshold or abs(bias) > bias_threshold

    low_bad = _bad(regions.low_error, regions.low_bias)
    high_bad = _bad(regions.high_error, regions.high_bias)
    interior_bad = _bad(regions.interior_error, regions.interior_bias)

    if low_bad and high_bad:
        return ResidualProblem.BOTH_EDGES
    if low_bad:
        return ResidualProblem.LOW_EDGE
    if high_bad:
        return ResidualProblem.HIGH_EDGE
    if interior_bad:
        return ResidualProblem.INTERIOR
    return ResidualProblem.NONE
