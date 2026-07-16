from __future__ import annotations

import math

import numpy as np

from .constants import thermal_voltage
from .linear_fit import fit_line_lm, numerical_derivative
from .types import ParameterEstimate, PrefitContext, Sample


def estimate_photocurrent(sample: Sample) -> ParameterEstimate:
    value = float(np.interp(0.0, sample.voltage, sample.current))
    extrapolated = not (sample.voltage[0] <= 0.0 <= sample.voltage[-1])
    return ParameterEstimate(value, None, "photocurrent_interp", ("I_ph_extrapolated",) if extrapolated else ())


def estimate_shunt_block(ctx: PrefitContext) -> None:
    window = ctx.segmentation.shunt
    if window is None or window.hi - window.lo < 2:
        ctx.estimates["Rsh"] = ParameterEstimate(1.0e30, None, "shunt_block_fallback", ("shunt_window_unavailable",))
        return
    try:
        fit = fit_line_lm(ctx.sample.voltage[window.lo : window.hi], ctx.sample.current[window.lo : window.hi])
        if abs(fit.slope) < 1.0e-30:
            raise ValueError("zero shunt slope")
        value = abs(1.0 / fit.slope)
        stderr = value * abs(fit.slope_stderr / fit.slope)
        ctx.estimates["Rsh"] = ParameterEstimate(value, stderr, "shunt_block")
        ctx.diagnostics["shunt_r_squared"] = fit.r_squared
    except (ValueError, RuntimeError):
        ctx.estimates["Rsh"] = ParameterEstimate(1.0e30, None, "shunt_block_fallback", ("shunt_fit_failed",))
        ctx.warnings.append("shunt_fit_failed")


def estimate_series_block(ctx: PrefitContext) -> None:
    window = ctx.segmentation.series
    if window is None:
        ctx.estimates["Rs"] = ParameterEstimate(0.0, None, "series_block_fallback", ("series_window_unavailable",))
        return
    voltage = ctx.sample.voltage[window.lo : window.hi]
    current = ctx.sample.current[window.lo : window.hi]
    valid = current > max(ctx.options.noise_model.sigma_floor, 0.0)
    voltage = voltage[valid]
    current = current[valid]
    try:
        if current.size < 4:
            raise ValueError("too few positive series points")
        derivative = numerical_derivative(voltage, np.log(current))
        fit = fit_line_lm(current[1:-1], derivative[1:-1])
        ctx.estimates["Rs"] = ParameterEstimate(max(0.0, fit.slope), fit.slope_stderr, "series_block")
        ctx.diagnostics["A_cheung_crosscheck"] = fit.intercept / thermal_voltage(ctx.sample.temperature_k)
        ctx.diagnostics["series_r_squared"] = fit.r_squared
    except (ValueError, RuntimeError):
        ctx.estimates["Rs"] = ParameterEstimate(0.0, None, "series_block_fallback", ("series_fit_failed",))
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
        saturation = math.exp(float(np.clip(log_anchor - anchor / (ideality * vt), -700.0, 700.0)))
        ctx.estimates["A"] = ParameterEstimate(ideality, abs(ideality * fit.slope_stderr / fit.slope), "semilog_block")
        ctx.estimates["I0"] = ParameterEstimate(saturation, None, "semilog_block")
        ctx.diagnostics["semilog_r_squared"] = fit.r_squared
        ctx.diagnostics["bias_correction_source"] = "identity_unvalidated"
    except (ValueError, RuntimeError, OverflowError):
        fallback = max(float(np.min(np.abs(current))) if current.size else minimum, minimum)
        ctx.estimates["A"] = ParameterEstimate(1.5, None, "semilog_block_fallback", ("semilog_fit_failed",))
        ctx.estimates["I0"] = ParameterEstimate(fallback, None, "semilog_block_fallback", ("semilog_fit_failed",))
        ctx.warnings.append("semilog_fit_failed")


def estimate_nonlinear_shunt_defaults(ctx: PrefitContext) -> None:
    rsh = ctx.estimates.get("Rsh")
    value = rsh.value if rsh is not None and math.isfinite(rsh.value) else 1.0e6
    ctx.estimates["Rsh2"] = ParameterEstimate(value, None, "nonlinear_shunt_defaults_block")
