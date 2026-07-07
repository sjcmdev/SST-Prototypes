from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import re
from typing import Any

import numpy as np


@dataclass(slots=True)
class ParseOptions:
    delimiter: str | None = None
    decimal_separator: str = "."
    skip_rows: int = 0
    has_header: bool | None = None
    column_roles: list[str] | None = None
    column_units: list[str | None] | None = None
    encoding: str = "utf-8"
    strict: bool = False
    name_pattern: str = "{stem}"
    user_overrides: dict[str, str] = field(default_factory=dict)


_TEMP_PATTERNS = [
    re.compile(r"(?P<temp>\d+(?:[.,]\d+)?)\s*K\b", re.IGNORECASE),
    re.compile(r"T(?:emp(?:erature)?)?\s*[_:= -]?\s*(?P<temp>\d+(?:[.,]\d+)?)", re.IGNORECASE),
]


def normalize_options(options: ParseOptions | dict[str, Any] | None) -> ParseOptions:
    if options is None:
        return ParseOptions()
    if isinstance(options, ParseOptions):
        return options
    return ParseOptions(**options)


def detect_temperature(path: str | Path, header_lines: list[str]) -> tuple[float | None, str, list[str]]:
    warnings: list[str] = []
    filename = Path(path).name
    filename_temperature = _find_temperature(filename)
    if filename_temperature is not None:
        return filename_temperature, "filename", warnings

    for line in header_lines:
        header_temperature = _find_temperature(line)
        if header_temperature is not None:
            return header_temperature, "header", warnings

    warnings.append("temperature_missing")
    return None, "missing", warnings


def _find_temperature(text: str) -> float | None:
    for pattern in _TEMP_PATTERNS:
        match = pattern.search(text)
        if match:
            return float(match.group("temp").replace(",", "."))
    return None


def build_curve_name(
    metadata: dict[str, Any],
    pattern: str,
    user_overrides: dict[str, str] | None = None,
) -> str:
    values = {key: "" if value is None else str(value) for key, value in metadata.items()}
    if user_overrides:
        values.update(user_overrides)
    try:
        return pattern.format(**values)
    except KeyError:
        return values.get("stem") or values.get("source_path") or "curve"


def split_numeric_rows(
    lines: list[str],
    options: ParseOptions,
) -> tuple[list[str], list[list[float]], list[str], list[str]]:
    header_lines: list[str] = []
    rows: list[list[float]] = []
    warnings: list[str] = []
    header_columns: list[str] = []

    for index, raw_line in enumerate(lines[options.skip_rows :], start=options.skip_rows):
        line = raw_line.strip()
        if not line:
            continue
        if line.startswith(("#", "//", "%")):
            header_lines.append(line.lstrip("#/% "))
            continue

        parsed = parse_numeric_line(line, options)
        if parsed is None:
            if not rows and any(character.isalpha() for character in line):
                header_lines.append(line)
                header_columns = split_columns(line, options)
                continue
            warnings.append(f"line_{index + 1}_non_numeric")
            if options.strict:
                raise ValueError(f"Non-numeric line at {index + 1}: {line}")
            continue

        if rows and len(parsed) != len(rows[0]):
            warnings.append(f"line_{index + 1}_inconsistent_column_count")
            if options.strict:
                raise ValueError(f"Inconsistent column count at line {index + 1}")
            continue
        rows.append(parsed)

    if not rows:
        raise ValueError("No numeric rows found.")
    return header_lines, rows, header_columns, warnings


def parse_numeric_line(line: str, options: ParseOptions) -> list[float] | None:
    values: list[float] = []
    for item in split_columns(line, options):
        normalized = item.strip()
        if not normalized:
            continue
        if options.decimal_separator == "," or _looks_like_decimal_comma(normalized):
            normalized = normalized.replace(",", ".")
        try:
            values.append(float(normalized))
        except ValueError:
            return None
    return values if values else None


def split_columns(line: str, options: ParseOptions) -> list[str]:
    if options.delimiter is not None:
        return [part for part in line.split(options.delimiter) if part != ""]
    if ";" in line:
        return [part for part in line.split(";") if part != ""]
    if "\t" in line:
        return [part for part in line.split("\t") if part != ""]
    return [part for part in re.split(r"\s+", line.strip()) if part != ""]


def make_columns(header_columns: list[str], n_columns: int, options: ParseOptions) -> list[dict[str, str | None]]:
    roles = options.column_roles or []
    units = options.column_units or []
    columns: list[dict[str, str | None]] = []
    for index in range(n_columns):
        name = header_columns[index] if index < len(header_columns) else f"col_{index}"
        role = roles[index] if index < len(roles) else infer_role(name, index)
        unit = units[index] if index < len(units) else infer_unit(name)
        columns.append({"name": name, "unit": unit, "role": role})
    return columns


def infer_role(name: str, index: int) -> str:
    lowered = name.lower()
    if index == 0 or "volt" in lowered or lowered in {"v", "u"}:
        return "voltage"
    if "j" in lowered or "density" in lowered:
        return "current_density"
    if "current" in lowered or lowered in {"i", "a"}:
        return "current"
    return "value"


def infer_unit(name: str) -> str | None:
    match = re.search(r"\((?P<unit>[^)]+)\)|\[(?P<bracket>[^\]]+)\]", name)
    if not match:
        return None
    return match.group("unit") or match.group("bracket")


def make_curve(
    path: str | Path,
    parser: str,
    data: np.ndarray,
    columns: list[dict[str, str | None]],
    header_lines: list[str],
    warnings: list[str],
    options: ParseOptions,
    extra_metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    source = Path(path)
    temperature, temperature_source, temperature_warnings = detect_temperature(source, header_lines)
    all_warnings = [*warnings, *temperature_warnings]
    voltage_column = _find_column_index(columns, "voltage", 0)
    voltage = data[:, voltage_column] if data.size else np.array([], dtype=float)
    direction = "unknown"
    if voltage.size >= 2:
        delta = np.diff(voltage)
        if np.all(delta > 0):
            direction = "increasing"
        elif np.all(delta < 0):
            direction = "decreasing"
        elif np.sign(delta[0]) != np.sign(delta[-1]):
            direction = "loop"
            all_warnings.append("non_monotonic_sweep")
        else:
            direction = "non_monotonic"
            all_warnings.append("non_monotonic_sweep")

    metadata = {
        "stem": source.stem,
        "source_path": str(source),
        "parser": parser,
        "temperature_K": temperature,
        "temperature_source": temperature_source,
    }
    if extra_metadata:
        metadata.update(extra_metadata)

    return {
        "name": build_curve_name(metadata, options.name_pattern, options.user_overrides),
        "measurement": "IV",
        "columns": columns,
        "data": data,
        "sweep": {
            "direction": direction,
            "v_start": float(voltage[0]) if voltage.size else None,
            "v_stop": float(voltage[-1]) if voltage.size else None,
            "n_points": int(data.shape[0]),
        },
        "metadata": metadata,
        "warnings": all_warnings,
    }


def make_result(
    path: str | Path,
    parser: str,
    curves: list[dict[str, Any]],
    warnings: list[str],
) -> dict[str, Any]:
    header_temperature = curves[0]["metadata"].get("temperature_K") if curves else None
    temperature_source = curves[0]["metadata"].get("temperature_source", "missing") if curves else "missing"
    return {
        "ok": True,
        "parser": parser,
        "source_path": str(path),
        "temperature_K": header_temperature,
        "temperature_source": temperature_source,
        "curves": curves,
        "warnings": warnings,
    }


def _find_column_index(columns: list[dict[str, str | None]], role: str, fallback: int) -> int:
    for index, column in enumerate(columns):
        if column.get("role") == role:
            return index
    return fallback


def _looks_like_decimal_comma(value: str) -> bool:
    return "," in value and "." not in value and re.match(r"^[+-]?\d+,\d+(?:[eE][+-]?\d+)?$", value) is not None
