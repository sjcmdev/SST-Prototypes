from __future__ import annotations

from dataclasses import replace

import numpy as np
from scipy.stats import theilslopes

from .constants import thermal_voltage
from .types import PhotoDetectionOptions, PhotoDetectionResult, Sample


def _detection_window(sample: Sample, options: PhotoDetectionOptions) -> np.ndarray:
    voltage = sample.voltage
    count = voltage.size
    delta_v0 = min(
        options.voltage_window_thermal_factor * thermal_voltage(sample.temperature_k),
        options.voltage_window_range_fraction * (voltage[-1] - voltage[0]),
    )
    mask = np.abs(voltage) <= delta_v0

    order = np.argsort(np.abs(voltage), kind="stable")
    min_points = min(options.min_points, count)
    if int(np.count_nonzero(mask)) < min_points:
        mask = np.zeros(count, dtype=bool)
        mask[order[:min_points]] = True

    max_points = max(min_points, int(round(options.max_fraction * count)))
    if int(np.count_nonzero(mask)) > max_points:
        mask = np.zeros(count, dtype=bool)
        mask[order[:max_points]] = True
    return mask


def detect_photo_branch(
    sample: Sample, options: PhotoDetectionOptions | None = None
) -> PhotoDetectionResult:
    options = options or PhotoDetectionOptions()
    voltage = sample.voltage
    current = sample.current
    warnings: list[str] = []

    window = _detection_window(sample, options)
    window_voltage = voltage[window]
    window_current = current[window]
    n0 = int(window_current.size)

    i0 = float(np.median(window_current))
    mad = float(np.median(np.abs(window_current - i0)))
    s0 = 1.4826 * mad
    s_mean = max(s0 / max(1, n0) ** 0.5, options.sigma_floor)
    z_ph = abs(i0) / s_mean

    if n0 >= 2 and np.ptp(window_voltage) > 0.0:
        slope, intercept, _low, _high = theilslopes(window_current, window_voltage)
    else:
        slope, intercept = 0.0, i0
        warnings.append("photo_detection_window_degenerate")
    delta_v_window = float(np.ptp(window_voltage)) if n0 else 0.0
    flatness = abs(slope) * delta_v_window / max(abs(intercept), 3.0 * s0, options.sigma_floor)

    sign_matches = np.sign(window_current) == np.sign(i0)
    sign_coherence = float(np.mean(sign_matches)) if n0 else 0.0

    is_sign_valid = np.sign(i0) == options.expected_current_sign
    is_light = bool(
        is_sign_valid
        and z_ph >= options.z_threshold
        and flatness <= options.flatness_max
        and sign_coherence >= options.sign_coherence_min
    )

    if not is_light and n0 > options.min_points:
        # A series-limited illuminated curve need not be flat over the wide window.
        return detect_photo_branch(sample, replace(options, max_fraction=options.min_points / voltage.size))

    covers_zero = bool(voltage[0] <= 0.0 <= voltage[-1])
    if not covers_zero:
        warnings.append("photo_detection_window_excludes_zero_voltage")
    confidence = "high" if covers_zero and not warnings else "low"

    initial_i_ph = max(0.0, -i0)

    return PhotoDetectionResult(
        is_light=is_light,
        initial_i_ph=initial_i_ph,
        z_score=z_ph,
        flatness=flatness,
        sign_coherence=sign_coherence,
        confidence=confidence,
        warnings=tuple(warnings),
    )
