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
    return i0 * np.exp(np.clip(voltage / vt_a, -745.0, 700.0))


def _series_diode_current(
    voltage: np.ndarray, temperature_k: float, parameters: Mapping[str, float]
) -> tuple[np.ndarray, np.ndarray]:
    vt_a = max(float(parameters["A"]) * thermal_voltage(temperature_k), 1.0e-30)
    i0 = max(abs(float(parameters["I0"])), 1.0e-300)
    rs = max(float(parameters["Rs"]), 0.0)
    if rs <= 0.0:
        return _diode_current(voltage, temperature_k, parameters), voltage
    with np.errstate(divide="ignore", invalid="ignore", over="ignore"):
        argument = (i0 * rs / vt_a) * np.exp(voltage / vt_a)
        I_LW = np.real(lambertw(argument, k=0)) * vt_a / rs
    overflow = ~np.isfinite(I_LW)
    if np.any(overflow):
        log_argument = np.log(i0) + np.log(rs / vt_a) + voltage[overflow] / vt_a
        I_LW[overflow] = wrightomega(log_argument) * vt_a / rs
    return I_LW, voltage - I_LW * rs


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
    thermal = max(float(parameters["A"]) * thermal_voltage(temperature_k), 1.0e-30)
    current = max(abs(float(parameters["I0"])), 1.0e-300) * np.exp(
        values / thermal
    ) + values / float(parameters["Rsh"])
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
    # I0, A, Rs, Rsh = parameters["I0"], parameters["A"], parameters["Rs"], parameters["Rsh"]
    # alpha,Rsh2 = parameters["alpha"],parameters["Rsh2"]
    # AkT = thermal_voltage(temperature_k)
    # x = ((Rs*(voltage-)) + ())/AkT
    # lambertW_argument = I0*Rs/AkT * np.exp(voltage/AkT) * np.exp(-x)
    diode, internal_voltage = _series_diode_current(values, temperature_k, parameters)
    alpha = float(parameters["alpha"])
    with np.errstate(invalid="ignore"):
        nonlinear = np.power(internal_voltage, alpha)
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
