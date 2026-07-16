from __future__ import annotations

import numpy as np

from .types import CharacteristicType, Sample


def prepare_sample(
    voltage: np.ndarray,
    current: np.ndarray,
    temperature_k: float,
    characteristic_type: CharacteristicType = "dark",
    current_unit: str = "A",
) -> tuple[Sample, tuple[str, ...]]:
    voltage = np.asarray(voltage, dtype=float).reshape(-1)
    current = np.asarray(current, dtype=float).reshape(-1)
    if voltage.size != current.size:
        raise ValueError("voltage and current must have the same length")
    if voltage.size < 4:
        raise ValueError("at least four points are required for prefit")

    warnings: list[str] = []
    finite = np.isfinite(voltage) & np.isfinite(current)
    if not np.all(finite):
        voltage = voltage[finite]
        current = current[finite]
        warnings.append("non_finite_points_removed")
    if voltage.size < 4:
        raise ValueError("fewer than four finite points remain")

    order = np.argsort(voltage, kind="stable")
    if not np.array_equal(order, np.arange(voltage.size)):
        voltage = voltage[order]
        current = current[order]
        warnings.append("voltage_sorted")

    unique_voltage, first, counts = np.unique(voltage, return_index=True, return_counts=True)
    if np.any(counts > 1):
        current = np.add.reduceat(current, first) / counts
        voltage = unique_voltage
        warnings.append("duplicate_voltage_averaged")
    if voltage.size < 4:
        raise ValueError("fewer than four unique voltage points remain")

    if characteristic_type not in ("dark", "light"):
        raise ValueError(f"unsupported characteristic_type={characteristic_type!r}")
    if not np.isfinite(temperature_k) or temperature_k <= 0.0:
        raise ValueError("temperature_k must be finite and positive")

    return Sample(voltage, current, float(temperature_k), characteristic_type, current_unit), tuple(warnings)
