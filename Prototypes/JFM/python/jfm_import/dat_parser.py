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
    split_numeric_rows,
)


def parse_dat(path: str | Path, options: ParseOptions | dict[str, Any] | None = None) -> dict[str, Any]:
    """Parse a generic IV .dat file into the unified JFM import schema."""
    parse_options = normalize_options(options)
    source = Path(path)
    text = source.read_text(encoding=parse_options.encoding, errors="replace")
    header_lines, rows, header_columns, warnings = split_numeric_rows(text.splitlines(), parse_options)
    data = np.asarray(rows, dtype=float)
    columns = make_columns(header_columns, data.shape[1], parse_options)
    curve = make_curve(source, "dat", data, columns, header_lines, warnings, parse_options)
    return make_result(source, "dat", [curve], curve["warnings"])
