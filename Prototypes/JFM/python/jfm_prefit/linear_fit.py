from __future__ import annotations

import warnings

import numpy as np
from scipy.optimize import OptimizeWarning, curve_fit

from .types import LinearFitResult


def fit_line_lm(x: np.ndarray, y: np.ndarray, weights: np.ndarray | None = None) -> LinearFitResult:
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    finite = np.isfinite(x) & np.isfinite(y)
    if weights is not None:
        weights = np.asarray(weights, dtype=float)
        finite &= np.isfinite(weights) & (weights > 0.0)
        weights = weights[finite]
    x = x[finite]
    y = y[finite]
    if x.size < 2 or np.ptp(x) == 0.0:
        raise ValueError("line fit needs at least two distinct x values")

    slope, intercept = np.polyfit(x, y, 1)
    sigma = None if weights is None else 1.0 / np.sqrt(weights)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", OptimizeWarning)
        params, covariance = curve_fit(
            lambda values, m, b: m * values + b,
            x,
            y,
            p0=(slope, intercept),
            sigma=sigma,
            absolute_sigma=sigma is not None,
            method="lm",
            maxfev=200,
        )
    fitted = params[0] * x + params[1]
    residual = y - fitted
    total = y - np.mean(y)
    denominator = float(np.dot(total, total))
    r_squared = 1.0 - float(np.dot(residual, residual)) / denominator if denominator > 0.0 else 1.0
    errors = np.sqrt(np.maximum(np.diag(covariance), 0.0))
    return LinearFitResult(
        slope=float(params[0]),
        intercept=float(params[1]),
        slope_stderr=float(errors[0]),
        intercept_stderr=float(errors[1]),
        r_squared=r_squared,
        n_points=int(x.size),
    )


def numerical_derivative(x: np.ndarray, y: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=float)
    y = np.asarray(y, dtype=float)
    if x.size < 3:
        raise ValueError("derivative needs at least three points")
    return np.gradient(x, y)
