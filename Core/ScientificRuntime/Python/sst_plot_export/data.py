"""Lab-table file edge: UTF-8, round-trip float precision, explicit rows/masks."""
import csv
import math
import os
from pathlib import Path
import tempfile
from .files import commit_files


def export_data(snapshot, destination, format, cancelled=lambda: False):
    """Headers are names then units. Preserve adds a boolean mask column per data column.

    DAT is tab-separated. Missing is an empty cell or literal NaN, as selected.
    Rows are zero-based source indices in exactly the requested order.
    """
    delimiter = {"csv": ",", "tsv": "\t", "dat": "\t"}.get(format.lower())
    if delimiter is None: raise ValueError("Data formats: CSV, TSV, DAT")
    mask_policy, missing = snapshot["mask_policy"], snapshot["missing_policy"]
    if mask_policy not in ("preserve", "missing", "omit_rows") or missing not in ("empty", "nan"):
        raise ValueError("Select explicit mask and missing policies")
    columns, rows = snapshot["columns"], snapshot["rows"]
    if not columns or len({c["id"] for c in columns}) != len(columns): raise ValueError("Select distinct columns")
    count = len(columns[0]["values"])
    if any(len(c["values"]) != count or len(c["mask"]) != count for c in columns): raise ValueError("Column/mask lengths differ")
    if any(type(r) is not int or r < 0 or r >= count for r in rows) or len(set(rows)) != len(rows):
        raise ValueError("Select distinct valid source rows")
    destination = Path(destination)
    if destination.resolve() in {Path(p).resolve() for p in snapshot.get("source_paths", [])}:
        raise ValueError("Data export cannot overwrite its source")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="", dir=destination.parent,
                                         prefix=".sst-data-", delete=False) as stream:
            temporary = Path(stream.name)
            writer = csv.writer(stream, delimiter=delimiter, lineterminator="\n")
            preserve = mask_policy == "preserve"
            writer.writerow([v for c in columns for v in ([c["name"], c["name"]+"__masked"] if preserve else [c["name"]])])
            writer.writerow([v for c in columns for v in ([c["unit"], ""] if preserve else [c["unit"]])])
            for row in rows:
                if cancelled(): raise RuntimeError("Export cancelled")
                if mask_policy == "omit_rows" and any(c["mask"][row] for c in columns): continue
                output = []
                for c in columns:
                    value, masked = c["values"][row], bool(c["mask"][row])
                    if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isinf(value):
                        raise ValueError("Infinite table value; use missing values explicitly")
                    absent = value is None or (isinstance(value, float) and math.isnan(value)) or (masked and mask_policy == "missing")
                    output.append(("NaN" if missing == "nan" else "") if absent else repr(value) if isinstance(value, float) else value)
                    if preserve: output.append(int(masked))
                writer.writerow(output)
            stream.flush(); os.fsync(stream.fileno())
        commit_files([(temporary, destination)], cancelled)
    finally:
        if temporary is not None: temporary.unlink(missing_ok=True)
