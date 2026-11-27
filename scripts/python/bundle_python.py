"""Copy the active project interpreter and packages into an app's portable runtime."""
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import sys
import sysconfig


def bundle_python(destination: Path, base: Path, packages: Path) -> None:
    if not (base / "python.exe").is_file() or not (base / "Lib" / "encodings").is_dir():
        raise ValueError(f"Incomplete Python runtime: {base}")
    destination.mkdir(parents=True, exist_ok=True)
    for pattern in ("python*.exe", "python*.dll", "python*.zip", "LICENSE*"):
        for source in base.glob(pattern):
            shutil.copy2(source, destination / source.name)
    ignore = shutil.ignore_patterns("__pycache__", "*.pyc", "site-packages")
    for folder in ("Lib", "DLLs", "tcl"):
        if (base / folder).is_dir():
            shutil.copytree(base / folder, destination / folder, dirs_exist_ok=True, ignore=ignore)
    shutil.copytree(packages, destination / "Lib" / "site-packages", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", required=True, type=Path)
    args = parser.parse_args()
    if sys.prefix == sys.base_prefix:
        parser.error("Run this script with the project's uv venv interpreter.")
    bundle_python(args.destination, Path(sys.base_prefix), Path(sysconfig.get_path("purelib")))
