from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Literal

import numpy as np

CharacteristicType = Literal["dark", "light"]


@dataclass(slots=True, frozen=True)
class Sample:
    voltage: np.ndarray
    current: np.ndarray
    temperature_k: float
    characteristic_type: CharacteristicType = "dark"
    current_unit: str = "A"


@dataclass(slots=True, frozen=True)
class RangeWindow:
    lo: int
    hi: int
    reason: str


@dataclass(slots=True, frozen=True)
class PrefitSegmentation:
    shunt: RangeWindow | None
    exponential: RangeWindow | None
    series: RangeWindow | None


@dataclass(slots=True, frozen=True)
class FitRangeResult:
    fit_range: RangeWindow
    segmentation: PrefitSegmentation
    warnings: tuple[str, ...] = ()
    diagnostics: dict[str, Any] = field(default_factory=dict)


@dataclass(slots=True, frozen=True)
class LinearFitResult:
    slope: float
    intercept: float
    slope_stderr: float
    intercept_stderr: float
    r_squared: float
    n_points: int


@dataclass(slots=True, frozen=True)
class ParameterEstimate:
    value: float
    stderr: float | None
    source: str
    warnings: tuple[str, ...] = ()


@dataclass(slots=True, frozen=True)
class NoiseModel:
    sigma_floor: float = 1.0e-12
    sigma_rel: float = 0.01

    def sigma(self, current: np.ndarray) -> np.ndarray:
        return np.maximum(self.sigma_floor, self.sigma_rel * np.abs(current))


@dataclass(slots=True, frozen=True)
class RangeOptions:
    """Configuration for automatic fit-range (and prefit-segmentation) detection.

    NUMERICAL-SETTINGS SURFACE
    ---------------------------
    Every field below is a user-facing numerical setting: it must eventually be
    exposed in the application's numerical settings panel (see
    JFM_functional_specification.md, e.g. §8.4/§12.2 conventions for numeric
    knobs), not hidden as an internal implementation constant. None of these
    values encode a magic number tuned to one dataset; the automatic-range
    detector in range_detection.py derives its actual thresholds from the data
    itself (robust local noise estimates), and only uses these fields as
    multipliers/fractions/window sizes around that estimate.

    lo (lower bound) settings
    --------------------------
    n_sigma_lower       -- exclude V < n_sigma_lower * kT/q (thermal-voltage margin)
    margin_points       -- extra points to skip past the detected lower boundary

    hi (upper bound / plateau) settings
    -------------------------------------
    smoothing_enabled        -- median-smooth current before slope/plateau analysis
                                 (detection-only; never mutates the returned Sample)
    smoothing_window         -- window size (points) for the median smoothing
    upper_window              -- trailing window size (points) used for the
                                 plateau majority-vote walk
    upper_rel_tol             -- numerical safety floor only, relative to the
                                 local |I| scale (guards against a degenerate
                                 zero noise estimate on near-noiseless data);
                                 NOT the primary flatness criterion
    plateau_sigma_multiplier  -- how many robust noise-sigmas of slope count as
                                 "statistically flat"
    plateau_relative_quietness -- the trailing anchor region must have a slope
                                 noise level below this fraction of the curve's
                                 peak slope before a plateau is trusted at all
                                 (guards against mistaking a still-rising curve
                                 for a saturated one)
    plateau_vote_fraction     -- fraction of points within `upper_window` that
                                 must be flat for that window to count as
                                 "inside the plateau" (robust to isolated noise
                                 spikes, unlike a single-point break test)
    min_remaining_points      -- minimum points required inside [lo, hi) before
                                 an automatic boundary is trusted; otherwise the
                                 detector backs off to the full range

    manual override
    ----------------
    manual_lower_voltage, manual_upper_voltage
    """

    n_sigma_lower: float = 3.0
    margin_points: int = 3
    smoothing_enabled: bool = True
    smoothing_window: int = 5
    upper_window: int = 5
    upper_rel_tol: float = 1.0e-4
    plateau_sigma_multiplier: float = 5.0
    plateau_relative_quietness: float = 0.05
    plateau_vote_fraction: float = 0.7
    min_remaining_points: int = 20
    manual_lower_voltage: float | None = None
    manual_upper_voltage: float | None = None


@dataclass(slots=True, frozen=True)
class ResistanceOptions:
    min_points: int = 8
    scan_step: int = 4
    degradation_patience: int = 4
    degradation_r_squared: float = 0.02
    width_exponent: float = 0.15
    smoothing_window: int = 31


@dataclass(slots=True, frozen=True)
class PrefitOptions:
    range_options: RangeOptions = field(default_factory=RangeOptions)
    noise_model: NoiseModel = field(default_factory=NoiseModel)
    refinement_passes: int = 1
    resistance_options: ResistanceOptions = field(default_factory=ResistanceOptions)


@dataclass(slots=True, frozen=True)
class PrefitResult:
    model_id: str
    parameters: dict[str, ParameterEstimate]
    fit_range: FitRangeResult
    warnings: tuple[str, ...] = ()
    diagnostics: dict[str, Any] = field(default_factory=dict)


@dataclass(slots=True)
class PrefitContext:
    sample: Sample
    segmentation: PrefitSegmentation
    options: PrefitOptions
    estimates: dict[str, ParameterEstimate] = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)
    diagnostics: dict[str, Any] = field(default_factory=dict)
