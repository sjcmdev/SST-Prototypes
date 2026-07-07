from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np

from .common import build_curve_name


def export_curves(
    result: dict[str, Any],
    output_dir: str | Path,
    name_pattern: str = "{stem}_{index}",
    format: str = "dat",
) -> list[Path]:
    """Export imported curves as TSV-like .dat/.tsv files."""
    target_dir = Path(output_dir)
    target_dir.mkdir(parents=True, exist_ok=True)
    suffix = ".tsv" if format.lower() == "tsv" else ".dat"
    written: list[Path] = []

    for index, curve in enumerate(result.get("curves", []), start=1):
        metadata = dict(curve.get("metadata", {}))
        metadata["index"] = f"{index:03d}"
        name = build_curve_name(metadata, name_pattern).replace("/", "_").replace("\\", "_")
        path = target_dir / f"{name}{suffix}"
        columns = curve.get("columns", [])
        header = "\t".join(column.get("name", f"col_{i}") for i, column in enumerate(columns))
        data = np.asarray(curve["data"], dtype=float)
        np.savetxt(path, data, delimiter="\t", header=header, comments="")
        written.append(path)

    return written
