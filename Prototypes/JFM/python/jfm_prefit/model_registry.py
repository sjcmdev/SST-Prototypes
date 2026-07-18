from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

from . import blocks
from .current_models import (
    CurrentFn,
    single_diode_current,
    single_diode_nonlinear_shunt_current,
    single_diode_rs_current,
    single_diode_rs_rsh_current,
    single_diode_rsh_current,
)
from .types import CharacteristicType, PrefitContext

BlockFn = Callable[[PrefitContext], None]


@dataclass(slots=True, frozen=True)
class ModelSpec:
    model_id: str
    parameter_names: tuple[str, ...]
    characteristic_type: CharacteristicType
    current: CurrentFn
    blocks: tuple[str, ...]
    defaults: dict[str, float] = field(default_factory=dict)


MODEL_REGISTRY: dict[str, ModelSpec] = {
    "SD-0": ModelSpec("SD-0", ("I0", "A"), "dark", single_diode_current, ("semilog",)),
    "SD-Rs": ModelSpec(
        "SD-Rs",
        ("I0", "A", "Rs"),
        "dark",
        single_diode_rs_current,
        ("semilog", "series"),
    ),
    "SD-Rsh": ModelSpec(
        "SD-Rsh",
        ("I0", "A", "Rsh"),
        "dark",
        single_diode_rsh_current,
        ("shunt", "semilog"),
    ),
    "SD-RsRsh": ModelSpec(
        "SD-RsRsh",
        ("I0", "A", "Rs", "Rsh"),
        "dark",
        single_diode_rs_rsh_current,
        ("shunt", "semilog", "series"),
    ),
    "SD-0-I_ph": ModelSpec(
        "SD-0-I_ph", ("I0", "A", "I_ph"), "light", single_diode_current, ("semilog",)
    ),
    "SD-Rs-I_ph": ModelSpec(
        "SD-Rs-I_ph",
        ("I0", "A", "Rs", "I_ph"),
        "light",
        single_diode_rs_current,
        ("semilog", "series"),
    ),
    "SD-Rsh-I_ph": ModelSpec(
        "SD-Rsh-I_ph",
        ("I0", "A", "Rsh", "I_ph"),
        "light",
        single_diode_rsh_current,
        ("shunt", "semilog"),
    ),
    "SD-RsRsh-I_ph": ModelSpec(
        "SD-RsRsh-I_ph",
        ("I0", "A", "Rs", "Rsh", "I_ph"),
        "light",
        single_diode_rs_rsh_current,
        ("shunt", "semilog", "series"),
    ),
    "SD-RsRsh-alpha-Rsh2": ModelSpec(
        "SD-RsRsh-alpha-Rsh2",
        ("I0", "A", "Rs", "Rsh", "alpha", "Rsh2"),
        "dark",
        single_diode_nonlinear_shunt_current,
        ("shunt", "semilog", "series", "nonlinear_shunt_defaults"),
        {"alpha": 2.0},
    ),
    "SD-RsRsh-alpha-Rsh2-I_ph": ModelSpec(
        "SD-RsRsh-alpha-Rsh2-I_ph",
        ("I0", "A", "Rs", "Rsh", "alpha", "Rsh2", "I_ph"),
        "light",
        single_diode_nonlinear_shunt_current,
        ("shunt", "semilog", "series", "nonlinear_shunt_defaults"),
        {"alpha": 2.0},
    ),
}

BLOCK_REGISTRY: dict[str, BlockFn] = {
    "shunt": blocks.estimate_shunt_block,
    "series": blocks.estimate_series_block,
    "semilog": blocks.estimate_semilog_block,
    "nonlinear_shunt_defaults": blocks.estimate_nonlinear_shunt_defaults,
}
