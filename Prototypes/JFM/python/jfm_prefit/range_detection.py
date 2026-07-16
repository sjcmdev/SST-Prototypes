from __future__ import annotations

import numpy as np

from .constants import thermal_voltage
from .types import FitRangeResult, PrefitSegmentation, RangeOptions, RangeWindow, Sample


def _automatic_bounds(sample: Sample, options: RangeOptions) -> tuple[int, int, list[str]]:
    voltage = sample.voltage
    current = sample.current
    count = voltage.size
    warnings: list[str] = []

    lower_voltage = options.n_sigma_lower * thermal_voltage(sample.temperature_k)
    eligible = voltage >= lower_voltage
    if sample.characteristic_type == "dark":
        eligible &= current >= 0.0
    candidates = np.flatnonzero(eligible)
    lo = int(candidates[0]) if candidates.size else 0
    lo = min(lo + max(0, options.margin_points), max(0, count - 4))

    hi = count
    window = max(2, min(options.upper_window, count - lo))
    if count - lo >= max(window + 2, options.min_remaining_points):
        plateau_start = count
        for index in range(count - window, lo, -1):
            local = current[index : index + window]
            scale = max(abs(float(current[index])), 1.0e-12)
            flat = abs(float(current[index]) - float(np.mean(local))) <= options.upper_rel_tol * scale
            if not flat:
                break
            plateau_start = index
        if plateau_start < count and plateau_start - lo >= options.min_remaining_points:
            hi = plateau_start
        elif plateau_start < count:
            warnings.append("rolloff_ignored_too_few_points")

    if hi - lo < 4:
        lo, hi = 0, count
        warnings.append("automatic_range_fallback_full")
    return lo, hi, warnings


def _manual_bounds(sample: Sample, options: RangeOptions) -> tuple[int, int] | None:
    if options.manual_lower_voltage is None or options.manual_upper_voltage is None:
        return None
    lower, upper = sorted((options.manual_lower_voltage, options.manual_upper_voltage))
    lo = int(np.searchsorted(sample.voltage, lower, side="left"))
    hi = int(np.searchsorted(sample.voltage, upper, side="right"))
    lo = max(0, min(lo, sample.voltage.size - 4))
    hi = max(lo + 4, min(hi, sample.voltage.size))
    return lo, hi


def _slope_ratio_boundary(voltage: np.ndarray, current: np.ndarray, lo: int, hi: int, threshold: float) -> tuple[int, int, bool]:
    minimum_window = 3
    shunt_end = min(lo + max(minimum_window, (hi - lo) // 4), hi - minimum_window * 2)
    detected_shunt = False
    for index in range(lo + 2, hi - minimum_window * 2):
        local_dv = voltage[index + 1] - voltage[index - 1]
        secant_dv = voltage[index] - voltage[lo]
        if local_dv == 0.0 or secant_dv == 0.0:
            continue
        local = (current[index + 1] - current[index - 1]) / local_dv
        secant = (current[index] - current[lo]) / secant_dv
        if secant != 0.0 and abs(local / secant) < threshold:
            shunt_end = index
            detected_shunt = True
            break

    series_start = max(shunt_end + minimum_window, hi - max(minimum_window, (hi - lo) // 4))
    detected_series = False
    for index in range(hi - 3, shunt_end + minimum_window, -1):
        local_dv = voltage[index + 1] - voltage[index - 1]
        secant_dv = voltage[hi - 1] - voltage[index]
        if local_dv == 0.0 or secant_dv == 0.0:
            continue
        local = (current[index + 1] - current[index - 1]) / local_dv
        secant = (current[hi - 1] - current[index]) / secant_dv
        if secant != 0.0 and abs(local / secant) < threshold:
            series_start = index
            detected_series = True
            break

    series_start = max(shunt_end + minimum_window, min(series_start, hi - minimum_window))
    return shunt_end, series_start, detected_shunt and detected_series


def detect_fit_range(sample: Sample, options: RangeOptions | None = None) -> FitRangeResult:
    options = options or RangeOptions()
    manual = _manual_bounds(sample, options)
    if manual is None:
        lo, hi, warnings = _automatic_bounds(sample, options)
        reason = "automatic"
    else:
        lo, hi = manual
        warnings = []
        reason = "manual"

    if hi - lo < 9:
        warnings.append("segmentation_fallback_small_range")
        fallback = RangeWindow(lo, hi, "small_range_fallback")
        segmentation = PrefitSegmentation(shunt=fallback, exponential=fallback, series=fallback)
    else:
        shunt_end, series_start, fully_detected = _slope_ratio_boundary(
            sample.voltage, sample.current, lo, hi, options.shunt_ratio_threshold
        )
        if not fully_detected:
            warnings.append("segmentation_fallback")
        segmentation = PrefitSegmentation(
            shunt=RangeWindow(lo, shunt_end, "shunt_ratio_threshold"),
            exponential=RangeWindow(shunt_end, series_start, "between_shunt_and_series"),
            series=RangeWindow(series_start, hi, "series_ratio_threshold"),
        )
    return FitRangeResult(
        fit_range=RangeWindow(lo, hi, reason),
        segmentation=segmentation,
        warnings=tuple(warnings),
        diagnostics={
            "fit_voltage_min": float(sample.voltage[lo]),
            "fit_voltage_max": float(sample.voltage[hi - 1]),
        },
    )
