from __future__ import annotations

from pathlib import Path
from typing import Any
import importlib.util

import numpy as np

from .dat_parser import parse_dat
from .scaps_iv_parser import parse_scaps_iv


def import_file(path: str | Path, options: dict[str, Any] | None = None) -> dict[str, Any]:
    source = Path(path)
    options = dict(options or {})
    parser_name = str(options.get("parser") or "auto")
    if parser_name == "auto":
        parser_name = _parser_name_for(source)
    parser = _load_custom_parser(options) if parser_name == "custom" else (parse_scaps_iv if parser_name == "scaps_iv" else parse_dat)
    result = parser(source, _parse_options(options))
    return {
        "parser": result.get("parser", parser_name),
        "warnings": list(result.get("warnings", [])),
        "characteristics": [
            _to_cpp_characteristic(source, str(result.get("parser", parser_name)), curve, index)
            for index, curve in enumerate(result.get("curves", []))
        ],
    }


def _to_cpp_characteristic(source: Path, parser_name: str, curve: dict[str, Any], record_index: int) -> Any:
    import sst_jfm

    characteristic = sst_jfm.JFMCharacteristic()
    info = sst_jfm.JFMCharacteristicInfo()
    info.name = str(curve.get("name") or source.stem)
    info.measurement = str(curve.get("measurement") or "IV")
    info.source_path = str(source)
    record_key = curve.get("source_record_key")
    info.source_record_key = str(record_index if record_key is None else record_key)
    info.parser = parser_name

    metadata = curve.get("metadata") or {}
    info.metadata = {str(key): "" if value is None else str(value) for key, value in metadata.items()}
    info.temperature_K = metadata.get("temperature_K")
    info.temperature_source = str(metadata.get("temperature_source") or "missing")

    columns = [_to_cpp_column(sst_jfm, column) for column in curve.get("columns", [])]
    info.columns = columns

    sweep = curve.get("sweep") or {}
    cpp_sweep = sst_jfm.JFMSweepInfo()
    cpp_sweep.direction = str(sweep.get("direction") or "unknown")
    cpp_sweep.v_start = sweep.get("v_start")
    cpp_sweep.v_stop = sweep.get("v_stop")
    cpp_sweep.n_points = int(sweep.get("n_points") or 0)
    info.sweep = cpp_sweep

    warnings = [str(warning) for warning in curve.get("warnings", [])]
    qc = sst_jfm.JFMQualityControl()
    qc.warnings = warnings
    qc.has_missing_temperature = info.temperature_K is None
    qc.non_monotonic_sweep = "non_monotonic_sweep" in warnings or "nonmonotonic_sweep" in warnings
    info.qc = qc

    data = np.asarray(curve.get("data"), dtype=float)
    if data.ndim == 1:
        data = data.reshape((-1, 1))
    cpp_data = sst_jfm.JFMCharacteristicData()
    cpp_data.voltage = _column(data, columns, "voltage", 0)
    current_index = _column_index(columns, "current", 1)
    cpp_data.current = _column(data, columns, "current", current_index)
    cpp_data.current_density = _column(data, columns, "current_density", current_index)
    characteristic.info = info
    characteristic.data = cpp_data
    return characteristic


def _to_cpp_column(sst_jfm: Any, column: dict[str, Any]) -> Any:
    cpp_column = sst_jfm.JFMColumn()
    cpp_column.name = str(column.get("name") or "")
    cpp_column.unit = str(column.get("unit") or "")
    cpp_column.role = str(column.get("role") or "")
    return cpp_column


def _column(data: np.ndarray, columns: list[Any], role: str, fallback: int) -> list[float]:
    index = _column_index(columns, role, fallback)
    if data.ndim != 2 or index < 0 or index >= data.shape[1]:
        return []
    return data[:, index].astype(float).tolist()


def _column_index(columns: list[Any], role: str, fallback: int) -> int:
    for index, column in enumerate(columns):
        if getattr(column, "role", None) == role:
            return index
    return fallback


def _parser_name_for(path: Path) -> str:
    return "scaps_iv" if path.suffix.lower() == ".iv" else "dat"


def _parse_options(options: dict[str, Any]) -> dict[str, Any]:
    allowed = {
        "delimiter",
        "decimal_separator",
        "skip_rows",
        "has_header",
        "column_roles",
        "column_units",
        "encoding",
        "strict",
        "name_pattern",
        "short_name_pattern",
        "variable_delimiter",
        "custom_variables",
        "temperature_extraction",
        "user_overrides",
    }
    return {key: value for key, value in options.items() if key in allowed}


def _load_custom_parser(options: dict[str, Any]) -> Any:
    script_path = Path(str(options.get("custom_script_path") or ""))
    if not script_path.is_file():
        raise ValueError(f"Custom parser script not found: {script_path}")
    spec = importlib.util.spec_from_file_location("jfm_custom_parser", script_path)
    if spec is None or spec.loader is None:
        raise ValueError(f"Could not load custom parser script: {script_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    parser = getattr(module, "parse", None)
    if not callable(parser):
        raise ValueError("Custom parser script must export parse(path, options).")
    return parser
