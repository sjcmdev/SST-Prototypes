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
    n_sigma_lower: float = 3.0
    margin_points: int = 3
    upper_window: int = 5
    upper_rel_tol: float = 1.0e-3
    min_remaining_points: int = 20
    shunt_ratio_threshold: float = 0.8
    manual_lower_voltage: float | None = None
    manual_upper_voltage: float | None = None


@dataclass(slots=True, frozen=True)
class PrefitOptions:
    range_options: RangeOptions = field(default_factory=RangeOptions)
    noise_model: NoiseModel = field(default_factory=NoiseModel)
    refinement_passes: int = 1


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
