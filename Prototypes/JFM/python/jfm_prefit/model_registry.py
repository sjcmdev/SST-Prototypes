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
    parent: str | None = None
    adds_rs: bool = False
    adds_rsh: bool = False
    has_i_ph: bool = False
    is_nonlinear_shunt: bool = False


MODEL_REGISTRY: dict[str, ModelSpec] = {
    "SD-0": ModelSpec(
        "SD-0", ("I0", "A"), "dark", single_diode_current, ("semilog",)
    ),
    "SD-Rs": ModelSpec(
        "SD-Rs",
        ("I0", "A", "Rs"),
        "dark",
        single_diode_rs_current,
        ("semilog", "series"),
        parent="SD-0",
        adds_rs=True,
    ),
    "SD-Rsh": ModelSpec(
        "SD-Rsh",
        ("I0", "A", "Rsh"),
        "dark",
        single_diode_rsh_current,
        ("shunt", "semilog"),
        parent="SD-0",
        adds_rsh=True,
    ),
    "SD-RsRsh": ModelSpec(
        "SD-RsRsh",
        ("I0", "A", "Rs", "Rsh"),
        "dark",
        single_diode_rs_rsh_current,
        ("shunt", "semilog", "series"),
        parent="SD-0",
        adds_rs=True,
        adds_rsh=True,
    ),
    "SD-0-I_ph": ModelSpec(
        "SD-0-I_ph",
        ("I0", "A", "I_ph"),
        "light",
        single_diode_current,
        ("semilog",),
        has_i_ph=True,
    ),
    "SD-Rs-I_ph": ModelSpec(
        "SD-Rs-I_ph",
        ("I0", "A", "Rs", "I_ph"),
        "light",
        single_diode_rs_current,
        ("semilog", "series"),
        parent="SD-0-I_ph",
        adds_rs=True,
        has_i_ph=True,
    ),
    "SD-Rsh-I_ph": ModelSpec(
        "SD-Rsh-I_ph",
        ("I0", "A", "Rsh", "I_ph"),
        "light",
        single_diode_rsh_current,
        ("shunt", "semilog"),
        parent="SD-0-I_ph",
        adds_rsh=True,
        has_i_ph=True,
    ),
    "SD-RsRsh-I_ph": ModelSpec(
        "SD-RsRsh-I_ph",
        ("I0", "A", "Rs", "Rsh", "I_ph"),
        "light",
        single_diode_rs_rsh_current,
        ("shunt", "semilog", "series"),
        parent="SD-0-I_ph",
        adds_rs=True,
        adds_rsh=True,
        has_i_ph=True,
    ),
    "SD-RsRsh-alpha-Rsh2": ModelSpec(
        "SD-RsRsh-alpha-Rsh2",
        ("I0", "A", "Rs", "Rsh", "alpha", "Rsh2"),
        "dark",
        single_diode_nonlinear_shunt_current,
        ("shunt", "semilog", "series", "nonlinear_shunt_defaults"),
        {"alpha": 2.0},
        parent="SD-RsRsh",
        adds_rs=True,
        adds_rsh=True,
        is_nonlinear_shunt=True,
    ),
    "SD-RsRsh-alpha-Rsh2-I_ph": ModelSpec(
        "SD-RsRsh-alpha-Rsh2-I_ph",
        ("I0", "A", "Rs", "Rsh", "alpha", "Rsh2", "I_ph"),
        "light",
        single_diode_nonlinear_shunt_current,
        ("shunt", "semilog", "series", "nonlinear_shunt_defaults"),
        {"alpha": 2.0},
        parent="SD-RsRsh-I_ph",
        adds_rs=True,
        adds_rsh=True,
        has_i_ph=True,
        is_nonlinear_shunt=True,
    ),
}

BLOCK_REGISTRY: dict[str, BlockFn] = {
    "shunt": blocks.estimate_shunt_block,
    "series": blocks.estimate_series_block,
    "semilog": blocks.estimate_semilog_block,
    "nonlinear_shunt_defaults": blocks.estimate_nonlinear_shunt_defaults,
}

# Explicit hierarchical transition graph for auto model selection (§4, §9 of
# docs/model-detection.md). Keyed by current model id, values map a
# ResidualProblem side ("low"/"high"/"both") to the child model id. Not
# derived from substring matching on model ids per §12.2.
DARK_MODEL_CHILDREN: dict[str, dict[str, str]] = {
    "SD-0": {"low": "SD-Rsh", "high": "SD-Rs", "both": "SD-RsRsh"},
    "SD-Rs": {"low": "SD-RsRsh", "both": "SD-RsRsh"},
    "SD-Rsh": {"high": "SD-RsRsh", "both": "SD-RsRsh"},
    "SD-RsRsh": {"low": "SD-RsRsh-alpha-Rsh2", "both": "SD-RsRsh-alpha-Rsh2"},
}

LIGHT_MODEL_CHILDREN: dict[str, dict[str, str]] = {
    "SD-0-I_ph": {"low": "SD-Rsh-I_ph", "high": "SD-Rs-I_ph", "both": "SD-RsRsh-I_ph"},
    "SD-Rs-I_ph": {"low": "SD-RsRsh-I_ph", "both": "SD-RsRsh-I_ph"},
    "SD-Rsh-I_ph": {"high": "SD-RsRsh-I_ph", "both": "SD-RsRsh-I_ph"},
    "SD-RsRsh-I_ph": {
        "low": "SD-RsRsh-alpha-Rsh2-I_ph",
        "both": "SD-RsRsh-alpha-Rsh2-I_ph",
    },
}
