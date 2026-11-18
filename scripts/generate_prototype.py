#!/usr/bin/env python
from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TEMPLATE_DIR = ROOT / "PrototypeTemplate"
PROTOTYPES_DIR = ROOT / "Prototypes"
ACTIVE_PROTOTYPE_FILE = ROOT / ".sst-prototype"
PROTOTYPE_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate an executable prototype from PrototypeTemplate.")
    parser.add_argument("name", help="Prototype name. Existing .sst-prototype selection is preserved.")
    parser.add_argument("--force", action="store_true", help="Deprecated: existing prototypes are never overwritten")
    return parser.parse_args()


def replace_placeholders(path: Path, replacements: dict[str, str]) -> None:
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return

    for key, value in replacements.items():
        text = text.replace(key, value)
    path.write_text(text, encoding="utf-8", newline="\n")


def rename_placeholders(root: Path, replacements: dict[str, str]) -> None:
    for path in sorted(root.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        new_name = path.name
        for key, value in replacements.items():
            new_name = new_name.replace(key, value)

        if new_name != path.name:
            path.rename(path.with_name(new_name))


def main() -> int:
    args = parse_args()
    prototype_name = args.name

    if not PROTOTYPE_NAME_RE.fullmatch(prototype_name) or prototype_name.upper() in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}:
        print("Prototype name must start with a letter and contain only letters, digits, or underscores.")
        return 2

    if not TEMPLATE_DIR.is_dir():
        print(f"Missing template directory: {TEMPLATE_DIR}")
        return 1

    target_dir = PROTOTYPES_DIR / prototype_name
    if target_dir.exists():
        print(f"Prototype already exists; refusing to delete existing data: {target_dir}")
        return 1

    shutil.copytree(TEMPLATE_DIR, target_dir)
    replacements = {
        "__PROTOTYPE_NAME__": prototype_name,
        "__PROTOTYPE_NAME_UPPER__": prototype_name.upper(),
    }

    for path in target_dir.rglob("*"):
        if path.is_file():
            replace_placeholders(path, replacements)
    rename_placeholders(target_dir, replacements)

    if not ACTIVE_PROTOTYPE_FILE.exists():
        ACTIVE_PROTOTYPE_FILE.write_text(prototype_name + "\n", encoding="utf-8")

    print(f"Generated prototype: {target_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

