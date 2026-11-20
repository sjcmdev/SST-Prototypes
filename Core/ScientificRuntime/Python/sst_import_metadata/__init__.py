"""Filename token extraction shared by JFM and CapLab; no precedence policy."""
from typing import Any

def add_token_variables(metadata: dict[str, Any], key: str, text: str, delimiter: str) -> None:
    if not delimiter:
        return
    for index, part in enumerate(text.split(delimiter)):
        metadata[f"{key}_{index}"] = part


def add_custom_variables(metadata: dict[str, Any], options) -> None:
    delimiter = options.variable_delimiter
    for rule in options.custom_variables:
        if not isinstance(rule, dict):
            continue
        name = str(rule.get("name") or "").strip()
        source = str(rule.get("source") or "stem").strip()
        if not name:
            continue

        value = "" if metadata.get(source) is None else str(metadata.get(source, ""))
        token_index = _int_or_default(rule.get("token_index"), -1)
        if token_index >= 0 and delimiter:
            tokens = value.split(delimiter)
            value = tokens[token_index] if token_index < len(tokens) else ""

        start = max(0, _int_or_default(rule.get("substring_start"), 0))
        length = _int_or_default(rule.get("substring_length"), -1)
        value = value[start:] if length < 0 else value[start : start + max(0, length)]
        metadata[name] = value


def _int_or_default(value: Any, default: int) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


