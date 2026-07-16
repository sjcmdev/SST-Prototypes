from __future__ import annotations

from collections.abc import Mapping, Sequence

import numpy as np
from scipy.special import lambertw, wrightomega

from .constants import thermal_voltage
from .model_registry import MODEL_REGISTRY


def evaluate_current(
    voltage: Sequence[float],
    temperature_k: float,
    model_id: str,
    parameters: Mapping[str, float],
) -> np.ndarray:
    if model_id not in MODEL_REGISTRY:
        raise ValueError(f"unknown model_id={model_id!r}")
    values = np.asarray(voltage, dtype=float)
    vt_a = max(float(parameters["A"]) * thermal_voltage(float(temperature_k)), 1.0e-30)
    i0 = max(abs(float(parameters["I0"])), 1.0e-300)
    rs = max(float(parameters.get("Rs", 0.0)), 0.0)

    if rs > 1.0e-30:
        log_argument = np.log(i0 * rs / vt_a) + values / vt_a
        principal = np.empty_like(values)
        regular = log_argument <= 700.0
        principal[regular] = np.real(lambertw(np.exp(np.clip(log_argument[regular], -745.0, 700.0))))
        principal[~regular] = np.real(wrightomega(log_argument[~regular]))
        diode_current = principal * vt_a / rs
    else:
        diode_current = i0 * np.expm1(np.clip(values / vt_a, -745.0, 700.0))

    internal_voltage = values - diode_current * rs
    result = diode_current.copy()
    rsh = float(parameters.get("Rsh", np.inf))
    if np.isfinite(rsh) and abs(rsh) > 1.0e-30:
        result += internal_voltage / rsh

    if "alpha" in parameters and "Rsh2" in parameters:
        alpha = float(parameters["alpha"])
        rsh2 = float(parameters["Rsh2"])
        if abs(rsh2) > 1.0e-30:
            rounded = round(alpha)
            nonlinear = np.power(internal_voltage, rounded) if abs(alpha - rounded) < 1.0e-12 else np.power(np.maximum(internal_voltage, 0.0), alpha)
            result += nonlinear / rsh2

    if "I_ph" in parameters:
        result -= float(parameters["I_ph"])
    return np.asarray(result, dtype=float)
