from __future__ import annotations

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

from .constants import thermal_voltage
from .types import FitRangeResult, PrefitSegmentation, RangeOptions, RangeWindow, Sample


def _rolling_median(values: np.ndarray, window: int) -> np.ndarray:
    """Centered rolling median, edge-padded. Detection-only smoothing: never
    mutates the sample data returned to the caller, only the internal signal
    used to decide where the automatic range should end."""
    n = values.size
    window = max(1, min(window, n))
    if window <= 1 or n < 3:
        return values.astype(float, copy=True)
    if window % 2 == 0:
        window += 1
    half = window // 2
    padded = np.pad(values, (half, half), mode="edge")
    return np.nanmedian(sliding_window_view(padded, window), axis=-1)


def _robust_sigma(values: np.ndarray) -> float:
    """Median-absolute-deviation based robust standard-deviation estimate.
    Scale-invariant by construction (works identically whether currents are
    ~1e-12 A or ~1e-1 A), and insensitive to a minority of outlier points --
    unlike a plain mean/stdev, a handful of spikes cannot inflate it."""
    finite = values[np.isfinite(values)]
    if finite.size == 0:
        return 0.0
    median = np.median(finite)
    mad = np.median(np.abs(finite - median))
    return float(1.4826 * mad)


def _tail_plateau_bound(
    voltage: np.ndarray, current: np.ndarray, lo: int, options: RangeOptions
) -> tuple[int, bool]:
    """Find where the high-voltage tail of [lo, count) becomes indistinguishable
    from a flat plateau (instrument compliance limit / saturation) and should be
    excluded from the automatic fit range.

    Design notes (see accompanying explanation for the full rationale):
    - The "is this point flat" threshold is derived from the data itself (a
      robust noise estimate of the slope in the trailing anchor region), not
      from a fixed absolute or globally-relative magic number, so the same
      defaults work whether currents are ~1e-12 A or ~1e-1 A.
    - A trailing-window *majority vote* decides where the plateau starts,
      instead of stopping at the very first point that fails a flatness test.
      A single noisy sample can no longer defeat detection.
    - A sanity check compares the tail's noise level against the curve's peak
      slope: if the "tail" is not meaningfully quieter than the steepest part
      of the curve, there is probably no genuine plateau at all (e.g. the
      characteristic is still legitimately rising at the last measured point),
      and we correctly report "not detected" rather than over-truncating.

    Returns (hi, detected). When detected is False, hi == count (no plateau
    truncation is applied) and the caller should fall back accordingly.
    """
    count = voltage.size
    span = count - lo
    window = max(2, min(options.upper_window, span))
    if span < max(window * 2, options.min_remaining_points):
        return count, False

    segment_v = voltage[lo:]
    segment_i = current[lo:]

    dv = np.diff(segment_v)
    di = np.diff(segment_i)
    with np.errstate(divide="ignore", invalid="ignore"):
        raw_slope = np.abs(np.where(dv != 0.0, di / dv, np.nan))
    # smooth the *slope* itself (not the current before differencing): smoothing
    # current first and then differencing turns a median filter's exact-repeat
    # runs into spurious step-jumps in the derivative, which is the opposite of
    # what smoothing is meant to achieve here
    slope = (
        _rolling_median(raw_slope, options.smoothing_window)
        if options.smoothing_enabled
        else raw_slope
    )
    finite_slope = slope[np.isfinite(slope)]
    if finite_slope.size < window:
        return count, False

    peak_slope = float(np.max(finite_slope))
    if peak_slope <= 0.0:
        return count, False

    # a wider anchor than the vote window avoids a degenerate zero-MAD read
    # (common right at a quantized compliance limit, where a small sample can
    # easily be >50% exact repeats)
    anchor = max(window * 3, min(slope.size, 12))
    tail_sigma = _robust_sigma(slope[-anchor:])
    if tail_sigma > options.plateau_relative_quietness * peak_slope:
        # the trailing region isn't meaningfully quieter than the curve's
        # steepest part -- most likely there is no genuine saturation plateau
        return count, False

    scale = max(float(np.max(np.abs(segment_i))), 1.0e-300)
    floor = options.upper_rel_tol * scale
    threshold = max(tail_sigma * options.plateau_sigma_multiplier, floor)
    is_flat = np.isfinite(slope) & (slope <= threshold)

    vote = np.convolve(is_flat.astype(float), np.ones(window) / window, mode="valid")
    fails = vote < options.plateau_vote_fraction
    if fails.size == 0 or fails[-1]:
        return count, False
    fail_indices = np.flatnonzero(fails)
    boundary = int(fail_indices[-1]) + 1 if fail_indices.size else 0

    hi = min(lo + boundary + 1, count)
    if hi - lo < options.min_remaining_points:
        return count, False
    return hi, True


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

    lower_voltage = options.n_sigma_lower * thermal_voltage(sample.temperature_k)
    eligible = voltage >= lower_voltage
    if sample.characteristic_type == "dark":
        eligible &= current >= 0.0
    candidates = np.flatnonzero(eligible)
    lo = int(candidates[0]) if candidates.size else 0
    if sample.characteristic_type == "dark":
        lo = _stable_lower_bound(voltage, current, lo, options)
    lo = min(lo + max(0, options.margin_points), max(0, count - 4))

    hi, detected = _tail_plateau_bound(voltage, current, lo, options)
    if not detected:
        warnings.append("plateau_not_detected_full_upper_range_used")

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


def _slope_ratio_boundary(
    voltage: np.ndarray, current: np.ndarray, lo: int, hi: int, threshold: float
) -> tuple[int, int, bool]:
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

    series_start = max(
        shunt_end + minimum_window, hi - max(minimum_window, (hi - lo) // 4)
    )
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

    series_start = max(
        shunt_end + minimum_window, min(series_start, hi - minimum_window)
    )
    return shunt_end, series_start, detected_shunt and detected_series


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
        reason = "manual"

    if hi - lo < 9:
        warnings.append("segmentation_fallback_small_range")
        fallback = RangeWindow(lo, hi, "small_range_fallback")
        segmentation = PrefitSegmentation(
            shunt=fallback, exponential=fallback, series=fallback
        )
    else:
        shunt_end, series_start, fully_detected = _slope_ratio_boundary(
            sample.voltage, sample.current, lo, hi, options.shunt_ratio_threshold
        )
        if not fully_detected:
            warnings.append("segmentation_fallback")
        segmentation = PrefitSegmentation(
            shunt=RangeWindow(lo, shunt_end, "shunt_ratio_threshold"),
            exponential=RangeWindow(
                shunt_end, series_start, "between_shunt_and_series"
            ),
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
