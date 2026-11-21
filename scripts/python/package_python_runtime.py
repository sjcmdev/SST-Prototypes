"""Build a self-contained app Python runtime: <app>/install/app/python/<platform>.

Base interpreter (from .venv/pyvenv.cfg, the uv-managed CPython) + locked runtime packages from uv.lock.
Needs no global Python at run time. Usage: python scripts/python/package_python_runtime.py <app_dir>
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SKIP_DIRS = {"__pycache__", "test", "tests", "idlelib", "turtledemo", "ensurepip", "site-packages"}


def base_home(venv: Path) -> Path:
    for line in (venv / "pyvenv.cfg").read_text(encoding="utf-8").splitlines():
        if line.startswith("home"):
            return Path(line.split("=", 1)[1].strip())
    raise SystemExit(f"no 'home' in {venv / 'pyvenv.cfg'}")


def ignore(_dir: str, names: list[str]) -> list[str]:
    return [n for n in names if n in SKIP_DIRS or n.endswith(".pyc") or n.endswith(".lib")]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("app_dir", type=Path, help="folder that holds the app exe")
    ap.add_argument("--venv", type=Path, default=ROOT / ".venv")
    args = ap.parse_args()

    tag = "windows" if os.name == "nt" else ("macos" if sys.platform == "darwin" else "linux")
    dest = args.app_dir / "install" / "app" / "python" / tag
    home = base_home(args.venv)
    if dest.exists():
        shutil.rmtree(dest)

    # Interpreter: exe, dlls, stdlib (no tests/idle/site-packages/import libs).
    dest.mkdir(parents=True)
    for f in home.iterdir():
        if f.is_file() and f.suffix.lower() in {".exe", ".dll", ".txt"} and f.name.lower() != "pythonw.exe":
            shutil.copy2(f, dest / f.name)
    for d in ("DLLs", "Lib"):
        shutil.copytree(home / d, dest / d, ignore=ignore)

    # Packages: exactly what uv.lock pins (runtime deps only).
    with tempfile.TemporaryDirectory() as tmp:
        req = Path(tmp) / "requirements.txt"
        out = subprocess.run(
            ["uv", "export", "--frozen", "--no-dev", "--no-hashes", "--no-emit-project", "--no-header"],
            cwd=ROOT, check=True, capture_output=True, text=True,
        ).stdout
        req.write_text(out, encoding="utf-8")
        subprocess.run(
            ["uv", "pip", "install", "--no-deps", "--python", str(home / "python.exe" if os.name == "nt" else home / "bin" / "python3"),
             "--target", str(dest / "Lib" / "site-packages"), "-r", str(req)],
            cwd=ROOT, check=True,
        )
    for p in (dest / "Lib" / "site-packages").rglob("__pycache__"):
        shutil.rmtree(p, ignore_errors=True)

    size = sum(f.stat().st_size for f in dest.rglob("*") if f.is_file())
    print(f"[ok] {dest}  {size / 1e6:.0f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
