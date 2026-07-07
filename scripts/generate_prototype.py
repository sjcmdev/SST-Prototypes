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
    parser.add_argument("name", help="Prototype name, for example JFMPrototype")
    parser.add_argument("--force", action="store_true", help="Overwrite an existing generated prototype")
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

    if not PROTOTYPE_NAME_RE.fullmatch(prototype_name):
        print("Prototype name must start with a letter and contain only letters, digits, or underscores.")
        return 2

    if not TEMPLATE_DIR.is_dir():
        print(f"Missing template directory: {TEMPLATE_DIR}")
        return 1

    target_dir = PROTOTYPES_DIR / prototype_name
    if target_dir.exists():
        if not args.force:
            print(f"Prototype already exists: {target_dir}")
            return 1
        shutil.rmtree(target_dir)

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

