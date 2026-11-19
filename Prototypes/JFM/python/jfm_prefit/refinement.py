from __future__ import annotations

import math

import numpy as np
from scipy.optimize import least_squares

from .blocks import _line_quality
from .constants import thermal_voltage
from .model_registry import ModelSpec
from .types import ParameterEstimate, PrefitContext, Sample

_POSITIVE_PARAMETERS = {"I0", "A", "Rs", "Rsh", "Rsh2"}


def refine_parameters(
    context: PrefitContext,
    sample: Sample,
    spec: ModelSpec,
    lo: int,
    hi: int,
) -> None:
    """Refine the diode on [lo:hi], seeding failed coarse estimates from the
    low-voltage shunt first. Shared by manual prefit and automatic range search.
    """
    if context.options.refinement_passes <= 0:
        return

    names = tuple(name for name in spec.parameter_names if name in context.estimates)
    initial_values = {
        name: (
            max(context.estimates[name].value, 1.0e-300)
            if name in _POSITIVE_PARAMETERS
            else context.estimates[name].value
        )
        for name in names
    }
    staged = ("Rsh" in names and "alpha" not in names and
              (not 0.5 <= initial_values.get("A", 1.) <= 5. or
               context.estimates["A"].source.endswith("_fallback")))
    bounded_rs = staged or ("Rs" in names and context.estimates["Rs"].value == 0.)
    positive = _POSITIVE_PARAMETERS - {"Rs"} if bounded_rs else _POSITIVE_PARAMETERS
    fixed_shunt = False
    # Drobiazg: estimate the low-voltage shunt before the diode/series terms.
    # Coarse segmentation by point count can put its middle window in the Rs tail.
    if staged:
        v, i = sample.voltage, sample.current
        vt = thermal_voltage(sample.temperature_k)
        low = slice(0, max(8, min(v.size // 5, int(np.searchsorted(v, 6*vt)))))
        slope, intercept = np.polyfit(v[low], i[low], 1)
        if slope > 0:
            initial_values["Rsh"] = float(1. / slope)
            # Positive-only data may already contain diode current: do not freeze that slope.
            _, quality = _line_quality(v[low], i[low])
            fixed_shunt = (np.count_nonzero(v[low] < 0.) >= context.options.resistance_options.min_points
                           and quality >= 1. - context.options.resistance_options.degradation_r_squared)
        if "I_ph" in names:
            initial_values["I_ph"] = max(0., float(-intercept))
        shifted = i + initial_values.get("I_ph", 0.)
        junction = v - max(0., initial_values.get("Rs", 0.)) * shifted
        diode = shifted - junction / initial_values["Rsh"]
        region = (junction >= 3*vt) & (junction <= 12*vt) & (diode > 0.)
        if np.count_nonzero(region) >= 4:
            m, b = np.polyfit(junction[region], np.log(diode[region]), 1)
            if m > 0.:
                initial_values["A"] = 1. / (m*vt)
                initial_values["I0"] = math.exp(float(np.clip(b, -690., 690.)))
        initial_values["A"] = float(np.clip(initial_values["A"], .5, 5.))

    fit_names = tuple(name for name in names if not (fixed_shunt and name == "Rsh"))
    rs_scale = max(initial_values.get("Rs", 0.), 1.e-6)
    x0 = np.asarray(
        [
            math.log(initial_values[name])
            if name in positive
            else initial_values[name] / rs_scale if name == "Rs" else initial_values[name]
            for name in fit_names
        ],
        dtype=float,
    )
    voltage = sample.voltage[lo:hi]
    current = sample.current[lo:hi]
    sigma = context.options.noise_model.sigma(current)

    def unpack(values: np.ndarray) -> dict[str, float]:
        return {**initial_values, **{
            name: (
                float(math.exp(values[index]))
                if name in positive
                else float(values[index]) * rs_scale if name == "Rs" else float(values[index])
            )
            for index, name in enumerate(fit_names)
        }}

    def residual(values: np.ndarray) -> np.ndarray:
        predicted = spec.current(
            voltage,
            sample.temperature_k,
            unpack(values),
            spec.characteristic_type == "dark",
        )
        return (predicted - current) / sigma

    try:
        if not bounded_rs:
            result = least_squares(residual, x0, max_nfev=4000)
        else:
            lower = [-690. if name in positive else 0. if name == "Rs" else -np.inf for name in fit_names]
            upper = [690. if name in positive else np.inf for name in fit_names]
            result = least_squares(residual, x0, bounds=(lower, upper), max_nfev=4000)
    except (ValueError, RuntimeError, OverflowError, FloatingPointError):
        context.warnings.append("nonlinear_refinement_failed")
        return
    if not result.success:
        context.warnings.append("nonlinear_refinement_failed")
        return

    if bounded_rs and "Rs" in names:
        index = fit_names.index("Rs")
        if result.active_mask[index] == -1:
            boundary = result.x.copy()
            boundary[index] = 0.
            boundary_residual = residual(boundary)
            if np.dot(boundary_residual, boundary_residual) <= 2*result.cost + 1.e-10:
                result.x = boundary
    refined = unpack(result.x)
    for name in names:
        context.estimates[name] = ParameterEstimate(
            refined[name], None, "low_voltage_shunt" if fixed_shunt and name == "Rsh" else "nonlinear_refinement"
        )
    context.diagnostics["nonlinear_refinement_cost"] = float(result.cost)
