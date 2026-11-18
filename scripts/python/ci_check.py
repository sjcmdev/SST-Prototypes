from __future__ import annotations

import argparse
import sys

from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from scripts.python import build as build_script, generate_projects as generate_script
from scripts.python.common.exec import run_command
from scripts.python.common.paths import project_name, test_target, test_executable_path, repo_root


from scripts.python.common.paths import select_prototype


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Local CI checks.")
    parser.add_argument("--compiler", choices=["msvc", "gcc", "clang"], default="msvc")
    parser.add_argument("--generator", choices=["vs2022", "gmake2"], default=None)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--prototype", help="Prototype name; defaults to .sst-prototype.")
    return parser


def run(args: argparse.Namespace) -> int:
    select_prototype(getattr(args, "prototype", None))
    args.generator = args.generator or ("vs2022" if args.compiler == "msvc" else "gmake2")
    if (args.compiler == "msvc") != (args.generator == "vs2022"):
        print("[error] MSVC requires vs2022; gcc/clang require gmake2.")
        return 2
    print("== CI Check ==")

    code = generate_script.run(
        argparse.Namespace(
            generator=args.generator,
            compiler=args.compiler,
            clean=False,
            dry_run=args.dry_run,
            verbose=args.verbose,
        )
    )
    if code != 0:
        return code

    for cfg in ["Debug", "Release"]:
        code = build_script.run(
            argparse.Namespace(
                config=cfg,
                compiler=args.compiler,
                target=project_name(),
                clean=False,
                rebuild=False,
                dry_run=args.dry_run,
                verbose=args.verbose,
            )
        )
        if code != 0:
            return code

    code = build_script.run(argparse.Namespace(
        config="Debug", compiler=args.compiler, target=test_target(), clean=False,
        rebuild=False, dry_run=args.dry_run, verbose=args.verbose))
    if code:
        return code
    executable = test_executable_path("Debug", generator=args.generator)
    code = run_command([str(executable)], cwd=repo_root(),
                       dry_run=args.dry_run, verbose=args.verbose)
    if code:
        return code
    python_tests = repo_root() / "Prototypes" / project_name() / "python" / "tests"
    if python_tests.is_dir():
        return run_command([sys.executable, "-m", "pytest", str(python_tests)],
                           cwd=repo_root(), dry_run=args.dry_run, verbose=args.verbose)
    return 0


def main() -> int:
    parser = make_parser()
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
