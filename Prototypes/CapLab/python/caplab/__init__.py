"""Laboratory AS import; no document or UI mutation."""
import csv
import hashlib
import json
import math
import re


def parse_lab_as(text, source="", decimal=".", cancelled=lambda: False, progress=lambda value: None):
    if decimal not in (".", ","):
        raise ValueError("Decimal separator must be . or ,")
    columns, source_rows, temperature = [], [], None
    header_lines = []
    lines = text.splitlines()
    for number, line in enumerate(lines, 1):
        if cancelled():
            raise RuntimeError("Import cancelled")
        progress(number / max(1, len(lines)))
        line = line.strip()
        if not line:
            continue
        comment = line.startswith(("#", ";", "//"))
        numeric = re.match(r"[+\-0-9.,]|(?:nan|inf)(?:\s|[,;]|$)", line, re.I)
        if comment or not numeric:
            if columns and not comment:
                raise ValueError(f"Invalid data at row {number}")
            header_lines.append(line)
            matches = re.findall(r"(?:T(?:emperature)?\s*[=:]?\s*)([+\-]?[0-9]+(?:[.,][0-9]+)?(?:[Ee][+\-]?[0-9]+)?)\s*K", line, re.I)
            for value in matches:
                value = float(value.replace(",", ".") if decimal == "," else value)
                if temperature is not None and temperature != value:
                    raise ValueError(f"Conflicting header temperatures at row {number}")
                temperature = value
            continue
        line = line.split("#", 1)[0].strip()
        delimiter = ";" if ";" in line else ("," if decimal == "." and "," in line else None)
        fields = next(csv.reader([line], delimiter=delimiter)) if delimiter else line.split()
        try:
            values = [float(v.replace(",", ".") if decimal == "," else v) for v in fields]
        except ValueError as error:
            raise ValueError(f"Invalid numeric data at row {number}") from error
        if len(values) < 3 or (columns and len(values) != len(columns)):
            raise ValueError(f"Expected consistent f, Cp, Rp columns at row {number}")
        if not all(math.isfinite(v) for v in values) or values[0] <= 0 or values[2] <= 0:
            raise ValueError(f"Nonfinite data or nonpositive f/Rp at row {number}")
        if not columns:
            columns = [[] for _ in values]
        for column, value in zip(columns, values):
            column.append(value)
        source_rows.append(number)
    if not columns:
        raise ValueError("No laboratory AS data rows")
    if temperature is not None and (not math.isfinite(temperature) or temperature <= 0):
        raise ValueError("Header temperature must be positive")
    suggestion = re.search(r"(?:^|[_\s])([0-9]+(?:\.[0-9]+)?)K(?:[_ .]|$)", source, re.I)
    suggested = float(suggestion[1]) if suggestion else None
    return {"columns": columns, "source_rows": source_rows, "temperature_K": temperature,
            "suggested_temperature_K": suggested, "header": "\n".join(header_lines),
            "source": source, "sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
            "profile": "lab_as_cp_rp", "decimal": decimal}


def preview_json(text, source, decimal, cancelled=lambda: False, progress=lambda value: None):
    return json.dumps(parse_lab_as(text, source, decimal, cancelled, progress), allow_nan=False)


def project_from_preview(preview, temperature=None):
    """Exercise the actual embedded C++ model and validation."""
    import sst_caplab
    return json.loads(sst_caplab.project_from_preview(json.dumps(preview), temperature))
