from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

from .common import (
    ParseOptions,
    make_columns,
    make_curve,
    make_result,
    normalize_options,
    parse_numeric_line,
    split_columns,
)


def parse_scaps_iv(path: str | Path, options: ParseOptions | dict[str, Any] | None = None) -> dict[str, Any]:
    """Parse SCAPS IV output, including batch files with multiple numeric blocks."""
    parse_options = normalize_options(options)
    source = Path(path)
    lines = source.read_text(encoding=parse_options.encoding, errors="replace").splitlines()
    blocks, header_lines, warnings = _split_numeric_blocks(lines, parse_options)
    if not blocks:
        raise ValueError("No SCAPS numeric IV blocks found.")

    curves: list[dict[str, Any]] = []
    for index, block in enumerate(blocks, start=1):
        data = np.asarray(block["rows"], dtype=float)
        columns = make_columns(block["header_columns"], data.shape[1], parse_options)
        metadata = {"batch_index": str(index)}
        curve = make_curve(
            source,
            "scaps_iv",
            data,
            columns,
            [*header_lines, *block["header_lines"]],
            [*warnings, *block["warnings"]],
            parse_options,
            metadata,
        )
        if len(blocks) > 1:
            curve["name"] = f"{curve['name']}_{index:03d}"
        curves.append(curve)

    all_warnings = [warning for curve in curves for warning in curve.get("warnings", [])]
    return make_result(source, "scaps_iv", curves, all_warnings)


def _split_numeric_blocks(lines: list[str], options: ParseOptions) -> tuple[list[dict[str, Any]], list[str], list[str]]:
    blocks: list[dict[str, Any]] = []
    global_header: list[str] = []
    current_rows: list[list[float]] = []
    current_header: list[str] = []
    current_header_columns: list[str] = []
    current_warnings: list[str] = []
    warnings: list[str] = []

    def flush() -> None:
        nonlocal current_rows, current_header, current_header_columns, current_warnings
        if current_rows:
            blocks.append(
                {
                    "rows": current_rows,
                    "header_lines": current_header,
                    "header_columns": current_header_columns,
                    "warnings": current_warnings,
                }
            )
        current_rows = []
        current_header = []
        current_header_columns = []
        current_warnings = []

    for line_number, raw_line in enumerate(lines[options.skip_rows :], start=options.skip_rows + 1):
        line = raw_line.strip()
        if not line:
            flush()
            continue
        if line.startswith(("#", "//", "%")):
            target = current_header if current_rows else global_header
            target.append(line.lstrip("#/% "))
            continue

        parsed = parse_numeric_line(line, options)
        if parsed is None:
            if not current_rows and any(character.isalpha() for character in line):
                current_header.append(line)
                current_header_columns = split_columns(line, options)
                continue

            current_warnings.append(f"line_{line_number}_non_numeric")
            if options.strict:
                raise ValueError(f"Non-numeric line at {line_number}: {line}")
            continue

        if current_rows and len(parsed) != len(current_rows[0]):
            current_warnings.append(f"line_{line_number}_inconsistent_column_count")
            if options.strict:
                raise ValueError(f"Inconsistent column count at line {line_number}")
            continue

        current_rows.append(parsed)

    flush()
    if len(blocks) > 1:
        warnings.append("multiple_curves")
    return blocks, global_header, warnings
