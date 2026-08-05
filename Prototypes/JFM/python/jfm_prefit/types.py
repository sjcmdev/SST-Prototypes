from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Literal

import numpy as np

CharacteristicType = Literal["dark", "light"]
PhotoBranchMode = Literal["auto", "force_dark", "force_light"]


class ResidualProblem(str, Enum):
    NONE = "none"
    LOW_EDGE = "low_edge"
    HIGH_EDGE = "high_edge"
    BOTH_EDGES = "both_edges"
    INTERIOR = "interior"


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


@dataclass(slots=True, frozen=True)
class PhotoDetectionOptions:
    z_threshold: float = 5.0
    flatness_max: float = 0.35
    sign_coherence_min: float = 0.80
    expected_current_sign: int = -1
    min_points: int = 7
    max_fraction: float = 0.20
    voltage_window_thermal_factor: float = 6.0
    voltage_window_range_fraction: float = 0.05
    sigma_floor: float = 1.0e-30


@dataclass(slots=True, frozen=True)
class PhotoDetectionResult:
    is_light: bool
    initial_i_ph: float
    z_score: float
    flatness: float
    sign_coherence: float
    confidence: Literal["low", "high"]
    warnings: tuple[str, ...] = ()


@dataclass(slots=True, frozen=True)
class ResidualOptions:
    """Config for the moving-window residual analysis used by auto model/range
    selection (§6 of docs/model-detection.md). `local_window_*` control the
    width of the statistics/weights window (exposed in the JFM Numeric
    settings UI and serialized in jfm_user_settings.yaml, see
    JFMAutoPrefitSettings on the C++ side)."""

    local_window_fraction: float = 0.05
    local_window_min: int = 5
    local_window_max: int = 21
    edge_fraction: float = 0.15
    # Real HZB instrument noise near range boundaries (small-current
    # quantization) routinely produces normalized residuals of several
    # sigma even at the human-curated range/parameters -- 1.5x the
    # interior's own error scale flagged the *correct* answer as invalid
    # on 3/16 ground-truth characteristics before this was raised.
    local_rms_threshold: float = 2.5
    local_bias_threshold: float = 0.75
    max_same_sign_run_fraction: float = 0.20
    # A single-diode ideality factor never legitimately leaves this range;
    # without it a degenerate fit (e.g. Rs in the millions of ohms
    # compensating an absurd A) can still show flat residuals over a
    # narrow window and pass every other check (§7 "parametry leżą
    # wewnątrz fizycznych ograniczeń").
    min_ideality_factor: float = 0.5
    max_ideality_factor: float = 5.0


@dataclass(slots=True, frozen=True)
class RegionScores:
    low_error: float
    high_error: float
    interior_error: float
    low_bias: float
    high_bias: float
    interior_bias: float
    max_same_sign_run_fraction: float


@dataclass(slots=True, frozen=True)
class AdaptiveRangeOptions:
    """`jitter_radius_points` controls the bisection-search jitter (§8.3).
    `local_expansion_points` controls how many extra points the final
    locally_expand_range() step (§8.4) is allowed to try on each side."""

    jitter_radius_points: int = 3
    local_expansion_points: int = 3
    max_iterations: int = 20
    min_fit_points: int = 8
    equivalent_range_points: int = 2
    equivalent_range_fraction: float = 0.02


@dataclass(slots=True, frozen=True)
class ModelSelectionOptions:
    min_range_gain_points: int = 3
    min_range_gain_fraction: float = 0.03
    minimum_aicc_improvement: float = 6.0


@dataclass(slots=True, frozen=True)
class AutoPrefitOptions:
    range_options: RangeOptions = field(default_factory=RangeOptions)
    noise_model: NoiseModel = field(default_factory=NoiseModel)
    resistance_options: ResistanceOptions = field(default_factory=ResistanceOptions)
    photo_detection: PhotoDetectionOptions = field(default_factory=PhotoDetectionOptions)
    residual: ResidualOptions = field(default_factory=ResidualOptions)
    adaptive_range: AdaptiveRangeOptions = field(default_factory=AdaptiveRangeOptions)
    selection: ModelSelectionOptions = field(default_factory=ModelSelectionOptions)
    photo_branch_mode: PhotoBranchMode = "auto"


@dataclass(slots=True, frozen=True)
class CandidateSummary:
    model_id: str
    fit_range: RangeWindow
    point_count: int
    coverage_fraction: float
    parameter_count: int
    chi_squared: float
    reduced_chi2: float
    is_valid: bool
    residual_problem: ResidualProblem
    regions: RegionScores
    parameters: dict[str, ParameterEstimate]
    warnings: tuple[str, ...] = ()


@dataclass(slots=True, frozen=True)
class AutoPrefitResult:
    branch: CharacteristicType
    photo_detection: PhotoDetectionResult
    selected: CandidateSummary | None
    tested_candidates: tuple[CandidateSummary, ...]
    selection_reason: str
    warnings: tuple[str, ...] = ()
