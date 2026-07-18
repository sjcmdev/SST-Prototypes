from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Callable

import numpy as np
from scipy.special import lambertw, wrightomega

from .constants import thermal_voltage

CurrentFn = Callable[[Sequence[float], float, Mapping[str, float], bool], np.ndarray]


def _diode_current(
    voltage: np.ndarray, temperature_k: float, parameters: Mapping[str, float]
) -> np.ndarray:
    vt_a = max(float(parameters["A"]) * thermal_voltage(temperature_k), 1.0e-30)
    i0 = max(abs(float(parameters["I0"])), 1.0e-300)
    return i0 * np.expm1(np.clip(voltage / vt_a, -745.0, 700.0))


def _series_diode_current(
    voltage: np.ndarray, temperature_k: float, parameters: Mapping[str, float]
) -> tuple[np.ndarray, np.ndarray]:
    vt_a = max(float(parameters["A"]) * thermal_voltage(temperature_k), 1.0e-30)
    i0 = max(abs(float(parameters["I0"])), 1.0e-300)
    rs = max(float(parameters["Rs"]), 0.0)
    if rs <= 1.0e-30:
        diode = _diode_current(voltage, temperature_k, parameters)
        return diode, voltage

    log_argument = np.log(i0 * rs / vt_a) + voltage / vt_a
    principal = np.empty_like(voltage)
    regular = log_argument <= 700.0
    principal[regular] = np.real(
        lambertw(np.exp(np.clip(log_argument[regular], -745.0, 700.0)))
    )
    principal[~regular] = np.real(wrightomega(log_argument[~regular]))
    diode = principal * vt_a / rs
    return diode, voltage - diode * rs


def _with_photocurrent(
    current: np.ndarray, parameters: Mapping[str, float], dark: bool
) -> np.ndarray:
    return current if dark else current - float(parameters["I_ph"])


def single_diode_current(
    voltage: Sequence[float],
    temperature_k: float,
    parameters: Mapping[str, float],
    dark: bool,
) -> np.ndarray:
    values = np.asarray(voltage, dtype=float)
    return _with_photocurrent(
        _diode_current(values, temperature_k, parameters), parameters, dark
    )


def single_diode_rs_current(
    voltage: Sequence[float],
    temperature_k: float,
    parameters: Mapping[str, float],
    dark: bool,
) -> np.ndarray:
    values = np.asarray(voltage, dtype=float)
    diode, _ = _series_diode_current(values, temperature_k, parameters)
    return _with_photocurrent(diode, parameters, dark)


def single_diode_rsh_current(
    voltage: Sequence[float],
    temperature_k: float,
    parameters: Mapping[str, float],
    dark: bool,
) -> np.ndarray:
    values = np.asarray(voltage, dtype=float)
    current = _diode_current(values, temperature_k, parameters) + values / float(
        parameters["Rsh"]
    )
    return _with_photocurrent(current, parameters, dark)


def single_diode_rs_rsh_current(
    voltage: Sequence[float],
    temperature_k: float,
    parameters: Mapping[str, float],
    dark: bool,
) -> np.ndarray:
    values = np.asarray(voltage, dtype=float)
    diode, internal_voltage = _series_diode_current(values, temperature_k, parameters)
    current = diode + internal_voltage / float(parameters["Rsh"])
    return _with_photocurrent(current, parameters, dark)


def single_diode_nonlinear_shunt_current(
    voltage: Sequence[float],
    temperature_k: float,
    parameters: Mapping[str, float],
    dark: bool,
) -> np.ndarray:
    values = np.asarray(voltage, dtype=float)
    diode, internal_voltage = _series_diode_current(values, temperature_k, parameters)
    alpha = float(parameters["alpha"])
    rounded = round(alpha)
    nonlinear = (
        np.power(internal_voltage, rounded)
        if abs(alpha - rounded) < 1.0e-12
        else np.power(np.maximum(internal_voltage, 0.0), alpha)
    )
    current = (
        diode
        + internal_voltage / float(parameters["Rsh"])
        + nonlinear / float(parameters["Rsh2"])
    )
    return _with_photocurrent(current, parameters, dark)


def evaluate_current(
    voltage: Sequence[float],
    temperature_k: float,
    model_id: str,
    parameters: Mapping[str, float],
) -> np.ndarray:
    from .model_registry import MODEL_REGISTRY

    try:
        spec = MODEL_REGISTRY[model_id]
    except KeyError as error:
        raise ValueError(f"unknown model_id={model_id!r}") from error
    return spec.current(
        voltage, float(temperature_k), parameters, spec.characteristic_type == "dark"
    )
