from __future__ import annotations

import numpy as np

from .constants import thermal_voltage
from .types import FitRangeResult, PrefitSegmentation, RangeOptions, RangeWindow, Sample


def _tail_plateau_bound(
    voltage: np.ndarray, current: np.ndarray, lo: int, options: RangeOptions
) -> tuple[int, bool]:
    del voltage, lo, options
    copy = current[::-1]
    count = int(copy.size)
    if count == 0:
        return 0, False

    window = min(5, count)
    for i, item in enumerate(copy):
        last = min(i + window, count)
        average = float(np.mean(copy[i:last])) if i < last else 0.0
        if (item - average) < 0.0001:
            continue
        if count - i - window < 20:
            return count - window, True
        return count - i - window, True

    return count - window, True


def _stable_lower_bound(
    voltage: np.ndarray, current: np.ndarray, lo: int, options: RangeOptions
) -> int:
    """Given a candidate lower boundary `lo` (first index passing the eligibility
    mask), require a short run of consistently eligible points immediately
    afterwards so a single noisy point right at the boundary can't shift it.
    Falls back to `lo` unchanged if the run can't be confirmed (e.g. very short
    arrays)."""
    count = voltage.size
    window = max(2, min(options.upper_window, count - lo))
    if count - lo < window:
        return lo
    scale = max(float(np.max(np.abs(current))), 1.0e-300)
    zero_floor = -options.upper_rel_tol * scale
    eligible_run = current[lo : lo + window] >= zero_floor
    if np.mean(eligible_run) >= options.plateau_vote_fraction:
        return lo
    # the immediate neighbourhood is not consistently eligible (likely a lone
    # noisy point) -- search forward for the first index that starts a stable run
    for candidate in range(lo, min(lo + 4 * window, count - window)):
        run = current[candidate : candidate + window] >= zero_floor
        if np.mean(run) >= options.plateau_vote_fraction:
            return candidate
    return lo


def _automatic_bounds(
    sample: Sample, options: RangeOptions
) -> tuple[int, int, list[str]]:
    voltage = sample.voltage
    current = sample.current
    count = voltage.size
    warnings: list[str] = []

    lower_voltage = max(options.n_sigma_lower * thermal_voltage(sample.temperature_k), np.nextafter(0.0, 1.0))
    eligible = voltage >= lower_voltage
    if sample.characteristic_type == "dark":
        eligible &= current >= 0.0
    candidates = np.flatnonzero(eligible)
    if not candidates.size:
        candidates = np.flatnonzero(voltage > 0.0)
        if not candidates.size:
            raise ValueError("No positive voltage points in automatic fit range")
        warnings.append("automatic_range_fallback_positive_voltage")
    lo = int(candidates[0])
    if sample.characteristic_type == "dark":
        lo = _stable_lower_bound(voltage, current, lo, options)
    lo = min(lo + max(0, options.margin_points), max(0, count - 4))

    hi, detected = _tail_plateau_bound(voltage, current, lo, options)
    if not detected:
        warnings.append("plateau_not_detected_full_upper_range_used")

    if hi - lo < 4:
        lo, hi = int(candidates[0]), count
        if hi - lo < 4:
            raise ValueError("Too few positive voltage points in automatic fit range")
        warnings.append("automatic_range_fallback_positive_range")
    return lo, hi, warnings


def _manual_bounds(sample: Sample, options: RangeOptions) -> tuple[int, int] | None:
    if options.manual_lower_voltage is None or options.manual_upper_voltage is None:
        return None
    lower, upper = options.manual_lower_voltage, options.manual_upper_voltage
    if not np.isfinite(lower) or not np.isfinite(upper) or upper <= lower:
        raise ValueError("Invalid manual fit range: bounds must be finite and increasing")
    if not np.any((sample.voltage >= lower) & (sample.voltage <= upper)):
        raise ValueError("Empty manual fit range")
    lo = int(np.searchsorted(sample.voltage, lower, side="left"))
    hi = int(np.searchsorted(sample.voltage, upper, side="right"))
    lo = max(0, min(lo, sample.voltage.size - 4))
    hi = max(lo + 4, min(hi, sample.voltage.size))
    return lo, hi


def _coarse_segmentation(lo: int, hi: int) -> PrefitSegmentation:
    quarter = max(3, (hi - lo) // 4)
    shunt_end = min(lo + quarter, hi - 6)
    series_start = max(shunt_end + 3, hi - quarter)
    return PrefitSegmentation(
        shunt=RangeWindow(lo, shunt_end, "coarse_lower_quarter"),
        exponential=RangeWindow(shunt_end, series_start, "coarse_middle_half"),
        series=RangeWindow(series_start, hi, "coarse_upper_quarter"),
    )


def detect_fit_range(
    sample: Sample, options: RangeOptions | None = None
) -> FitRangeResult:
    options = options or RangeOptions()
    manual = _manual_bounds(sample, options)
    if manual is None:
        lo, hi, warnings = _automatic_bounds(sample, options)
        reason = "automatic"
    else:
        lo, hi = manual
        warnings = []
        if sample.voltage[lo] < options.manual_lower_voltage or sample.voltage[hi - 1] > options.manual_upper_voltage:
            warnings.append("manual_range_expanded_to_minimum_four_points")
        reason = "manual"

    if hi - lo < 9:
        warnings.append("segmentation_fallback_small_range")
        fallback = RangeWindow(lo, hi, "small_range_fallback")
        segmentation = PrefitSegmentation(
            shunt=fallback, exponential=fallback, series=fallback
        )
    else:
        segmentation = _coarse_segmentation(lo, hi)
    return FitRangeResult(
        fit_range=RangeWindow(lo, hi, reason),
        segmentation=segmentation,
        warnings=tuple(warnings),
        diagnostics={
            "fit_voltage_min": float(sample.voltage[lo]),
            "fit_voltage_max": float(sample.voltage[hi - 1]),
        },
    )
