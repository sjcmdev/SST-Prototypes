from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

from .dat_parser import parse_dat
from .scaps_iv_parser import parse_scaps_iv


def import_file(path: str | Path, options: dict[str, Any] | None = None) -> dict[str, Any]:
    source = Path(path)
    parser_name = _parser_name_for(source)
    parser = parse_scaps_iv if parser_name == "scaps_iv" else parse_dat
    result = parser(source, options)
    return {
        "parser": result.get("parser", parser_name),
        "warnings": list(result.get("warnings", [])),
        "characteristics": [_to_cpp_characteristic(source, str(result.get("parser", parser_name)), curve) for curve in result.get("curves", [])],
    }


def _to_cpp_characteristic(source: Path, parser_name: str, curve: dict[str, Any]) -> Any:
    import sst_jfm

    characteristic = sst_jfm.JFMCharacteristic()
    characteristic.name = str(curve.get("name") or source.stem)
    characteristic.measurement = str(curve.get("measurement") or "IV")
    characteristic.source_path = str(source)
    characteristic.parser = parser_name

    metadata = curve.get("metadata") or {}
    characteristic.metadata = {str(key): "" if value is None else str(value) for key, value in metadata.items()}
    characteristic.temperature_K = metadata.get("temperature_K")
    characteristic.temperature_source = str(metadata.get("temperature_source") or "missing")

    columns = [_to_cpp_column(sst_jfm, column) for column in curve.get("columns", [])]
    characteristic.columns = columns

    sweep = curve.get("sweep") or {}
    cpp_sweep = sst_jfm.JFMSweepInfo()
    cpp_sweep.direction = str(sweep.get("direction") or "unknown")
    cpp_sweep.v_start = sweep.get("v_start")
    cpp_sweep.v_stop = sweep.get("v_stop")
    cpp_sweep.n_points = int(sweep.get("n_points") or 0)
    characteristic.sweep = cpp_sweep

    warnings = [str(warning) for warning in curve.get("warnings", [])]
    qc = sst_jfm.JFMQualityControl()
    qc.warnings = warnings
    qc.has_missing_temperature = characteristic.temperature_K is None
    qc.non_monotonic_sweep = "non_monotonic_sweep" in warnings or "nonmonotonic_sweep" in warnings
    characteristic.qc = qc

    data = np.asarray(curve.get("data"), dtype=float)
    if data.ndim == 1:
        data = data.reshape((-1, 1))
    characteristic.voltage = _column(data, columns, "voltage", 0)
    current_index = _column_index(columns, "current", 1)
    characteristic.current = _column(data, columns, "current", current_index)
    characteristic.current_density = _column(data, columns, "current_density", current_index)
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
