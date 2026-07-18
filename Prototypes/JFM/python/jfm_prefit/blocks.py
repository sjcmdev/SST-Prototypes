from __future__ import annotations

import math

import numpy as np

from .constants import thermal_voltage
from .linear_fit import fit_line_lm
from .types import (
    LinearFitResult,
    ParameterEstimate,
    PrefitContext,
    ResistanceOptions,
    Sample,
)


def estimate_photocurrent(sample: Sample) -> ParameterEstimate:
    value = -float(np.interp(0.0, sample.voltage, sample.current))
    extrapolated = not (sample.voltage[0] <= 0.0 <= sample.voltage[-1])
    return ParameterEstimate(
        value,
        None,
        "photocurrent_interp",
        ("I_ph_extrapolated",) if extrapolated else (),
    )


def _line_quality(x: np.ndarray, y: np.ndarray) -> tuple[float, float]:
    centered_x = x - np.mean(x)
    denominator = float(np.dot(centered_x, centered_x))
    if denominator <= 0.0:
        raise ValueError("constant resistance fit axis")
    slope = float(np.dot(centered_x, y - np.mean(y)) / denominator)
    residual = y - (slope * x + float(np.mean(y) - slope * np.mean(x)))
    total = y - np.mean(y)
    total_squared = float(np.dot(total, total))
    r_squared = (
        1.0 - float(np.dot(residual, residual)) / total_squared
        if total_squared > 0.0
        else 1.0
    )
    return slope, r_squared


def _estimate_resistance(
    x: np.ndarray,
    y: np.ndarray,
    options: ResistanceOptions,
    *,
    inverse_slope: bool,
) -> tuple[ParameterEstimate, LinearFitResult, int, int]:
    finite = np.isfinite(x) & np.isfinite(y)
    x = np.asarray(x[finite], dtype=float)
    y = np.asarray(y[finite], dtype=float)
    minimum = max(4, options.min_points)
    if x.size < minimum:
        raise ValueError("too few resistance points")

    step = max(1, options.scan_step, x.size // 64)
    best: tuple[float, int, int] | None = None
    for lo in range(0, x.size - minimum + 1, step):
        best_r_squared = -math.inf
        degraded = 0
        endpoints = list(range(lo + minimum, x.size + 1, step))
        if not endpoints or endpoints[-1] != x.size:
            endpoints.append(x.size)
        for hi in endpoints:
            slope, r_squared = _line_quality(x[lo:hi], y[lo:hi])
            if slope <= 0.0 or not math.isfinite(r_squared):
                continue
            coverage = (hi - lo) / x.size
            score = max(0.0, r_squared) * coverage ** max(0.0, options.width_exponent)
            if best is None or score > best[0]:
                best = (score, lo, hi)
            if r_squared < best_r_squared - options.degradation_r_squared:
                degraded += 1
                if degraded >= max(1, options.degradation_patience):
                    break
            else:
                best_r_squared = max(best_r_squared, r_squared)
                degraded = 0

    if best is None:
        raise ValueError("no positive resistance slope")
    _, lo, hi = best
    fit = fit_line_lm(x[lo:hi], y[lo:hi])
    if fit.slope <= 0.0:
        raise ValueError("non-positive resistance slope")
    value = 1.0 / fit.slope if inverse_slope else fit.slope
    stderr = (
        value * abs(fit.slope_stderr / fit.slope) if inverse_slope else fit.slope_stderr
    )
    return ParameterEstimate(value, stderr, "resistance_range_scan"), fit, lo, hi


def _moving_average(values: np.ndarray, window: int) -> np.ndarray:
    return np.convolve(values, np.ones(window, dtype=float) / window, mode="valid")


def estimate_shunt_block(ctx: PrefitContext) -> None:
    lower = ctx.segmentation.shunt
    upper = ctx.segmentation.series
    range_lo = lower.lo if lower is not None else 0
    range_hi = upper.lo if upper is not None else ctx.sample.voltage.size
    if range_hi - range_lo < 4:
        ctx.estimates["Rsh"] = ParameterEstimate(
            1.0e30, None, "shunt_block_fallback", ("shunt_window_unavailable",)
        )
        return
    try:
        estimate, fit, selected_lo, selected_hi = _estimate_resistance(
            ctx.sample.voltage[range_lo:range_hi],
            ctx.sample.current[range_lo:range_hi],
            ctx.options.resistance_options,
            inverse_slope=True,
        )
        ctx.estimates["Rsh"] = ParameterEstimate(
            estimate.value, estimate.stderr, "shunt_range_scan"
        )
        ctx.diagnostics["shunt_r_squared"] = fit.r_squared
        ctx.diagnostics["shunt_voltage_min"] = float(
            ctx.sample.voltage[range_lo + selected_lo]
        )
        ctx.diagnostics["shunt_voltage_max"] = float(
            ctx.sample.voltage[range_lo + selected_hi - 1]
        )
    except (ValueError, RuntimeError):
        ctx.estimates["Rsh"] = ParameterEstimate(
            1.0e30, None, "shunt_block_fallback", ("shunt_fit_failed",)
        )
        ctx.warnings.append("shunt_fit_failed")


def estimate_series_block(ctx: PrefitContext) -> None:
    lower = ctx.segmentation.shunt
    upper = ctx.segmentation.series
    lo = lower.lo if lower is not None else 0
    hi = upper.hi if upper is not None else ctx.sample.voltage.size
    if hi - lo < 4:
        ctx.estimates["Rs"] = ParameterEstimate(
            0.0, None, "series_block_fallback", ("series_window_unavailable",)
        )
        return
    voltage = ctx.sample.voltage[lo:hi]
    current = ctx.sample.current[lo:hi]
    valid = current > max(ctx.options.noise_model.sigma_floor, 0.0)
    voltage = voltage[valid]
    current = current[valid]
    try:
        if current.size < max(4, ctx.options.resistance_options.min_points):
            raise ValueError("too few positive series points")
        maximum_window = max(1, ctx.options.resistance_options.smoothing_window)
        smoothing_window = min(maximum_window, max(3, current.size // 20))
        if smoothing_window % 2 == 0:
            smoothing_window -= 1
        if smoothing_window >= 3:
            voltage = _moving_average(voltage, smoothing_window)
            log_current = _moving_average(np.log(current), smoothing_window)
            current = np.exp(log_current)
        else:
            log_current = np.log(current)
        log_slope = np.gradient(log_current, voltage)
        valid = np.isfinite(log_slope) & (log_slope > 0.0)
        voltage = voltage[valid]
        current = current[valid]
        derivative = 1.0 / log_slope[valid]
        estimate, fit, selected_lo, selected_hi = _estimate_resistance(
            current,
            derivative,
            ctx.options.resistance_options,
            inverse_slope=False,
        )
        ctx.estimates["Rs"] = ParameterEstimate(
            estimate.value, estimate.stderr, "series_range_scan"
        )
        ctx.diagnostics["A_cheung_crosscheck"] = fit.intercept / thermal_voltage(
            ctx.sample.temperature_k
        )
        ctx.diagnostics["series_r_squared"] = fit.r_squared
        ctx.diagnostics["series_voltage_min"] = float(voltage[selected_lo])
        ctx.diagnostics["series_voltage_max"] = float(voltage[selected_hi - 1])
    except (ValueError, RuntimeError):
        ctx.estimates["Rs"] = ParameterEstimate(
            0.0, None, "series_block_fallback", ("series_fit_failed",)
        )
        ctx.warnings.append("series_fit_failed")


def estimate_semilog_block(ctx: PrefitContext) -> None:
    window = ctx.segmentation.exponential
    if window is None:
        raise ValueError("exponential window unavailable")
    voltage = ctx.sample.voltage[window.lo : window.hi]
    current = ctx.sample.current[window.lo : window.hi].copy()
    rsh = ctx.estimates.get("Rsh")
    if rsh is not None and math.isfinite(rsh.value) and rsh.value != 0.0:
        current -= voltage / rsh.value
    minimum = max(ctx.options.noise_model.sigma_floor, 1.0e-30)
    valid = np.abs(current) > minimum
    voltage = voltage[valid]
    current = current[valid]
    try:
        if current.size < 3:
            raise ValueError("too few exponential points")
        weights = (current / ctx.options.noise_model.sigma(current)) ** 2
        fit = fit_line_lm(voltage, np.log(np.abs(current)), weights)
        vt = thermal_voltage(ctx.sample.temperature_k)
        if fit.slope <= 0.0:
            raise ValueError("non-positive semilog slope")
        ideality = 1.0 / (fit.slope * vt)
        anchor = float(np.mean(voltage))
        log_anchor = fit.slope * anchor + fit.intercept
        saturation = math.exp(
            float(np.clip(log_anchor - anchor / (ideality * vt), -700.0, 700.0))
        )
        ctx.estimates["A"] = ParameterEstimate(
            ideality, abs(ideality * fit.slope_stderr / fit.slope), "semilog_block"
        )
        ctx.estimates["I0"] = ParameterEstimate(saturation, None, "semilog_block")
        ctx.diagnostics["semilog_r_squared"] = fit.r_squared
        ctx.diagnostics["bias_correction_source"] = "identity_unvalidated"
    except (ValueError, RuntimeError, OverflowError):
        fallback = max(
            float(np.min(np.abs(current))) if current.size else minimum, minimum
        )
        ctx.estimates["A"] = ParameterEstimate(
            1.5, None, "semilog_block_fallback", ("semilog_fit_failed",)
        )
        ctx.estimates["I0"] = ParameterEstimate(
            fallback, None, "semilog_block_fallback", ("semilog_fit_failed",)
        )
        ctx.warnings.append("semilog_fit_failed")


def estimate_nonlinear_shunt_defaults(ctx: PrefitContext) -> None:
    rsh = ctx.estimates.get("Rsh")
    value = rsh.value if rsh is not None and math.isfinite(rsh.value) else 1.0e6
    ctx.estimates["Rsh2"] = ParameterEstimate(
        value, None, "nonlinear_shunt_defaults_block"
    )
