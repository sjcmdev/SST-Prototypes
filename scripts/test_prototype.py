#!/usr/bin/env python
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
from pathlib import Path
import xml.etree.ElementTree as ET

from prototype_cli import ROOT, build_parser, build_target, target_executable_path, prototype_name


def run_gtest(command, environment, require_embedding=False):
    with tempfile.TemporaryDirectory(prefix="prototype-tests-", dir=ROOT / "build") as directory:
        report = Path(directory) / "gtest.xml"
        result = subprocess.run(command + [f"--gtest_output=xml:{report}"], cwd=ROOT,
                                env=environment, check=False).returncode
        if result:
            return result
        if not report.is_file():
            raise SystemExit("GTest did not produce a test report.")
        cases = ET.parse(report).findall(".//testcase")
        if not cases:
            raise SystemExit("GTest selected no tests.")
        if require_embedding and any(case.find("skipped") is not None for case in cases):
            raise SystemExit("Embedding-required run contains skipped tests; integration is not verified.")
        return 0


def main() -> int:
    parser = build_parser("Run GTest and pytest using the root uv environment.")
    parser.add_argument("--require-embedding", action="store_true", help="Fail if embedded Python is unavailable.")
    args = parser.parse_args()
    prototype = prototype_name(args.prototype)
    target = f"{prototype}Tests"
    build_target(target, args.configuration, args.dry_run)
    executable = target_executable_path(target, args.configuration)
    python_root = ROOT / "Prototypes" / prototype / "python"
    tests = python_root / "tests"
    uv = shutil.which("uv")
    if not uv:
        raise SystemExit("uv is required; tests use the root project's environment.")
    environment = os.environ.copy()
    if args.require_embedding:
        environment["SST_REQUIRE_EMBEDDING"] = "1"
    command = [uv, "run", "--no-sync", "--project", str(ROOT), "python", "-c",
        "import json, os, subprocess, sys; "
        "os.environ['SST_ROOT_PYTHON_PATHS'] = json.dumps(sys.path); "
        "raise SystemExit(subprocess.run(sys.argv[1:]).returncode)", str(executable)]
    if args.dry_run:
        print(f"GTest: {command}; pytest={tests}; require_embedding={args.require_embedding}")
        return 0
    result = run_gtest(command, environment, args.require_embedding)
    if result:
        return result
    if not tests.is_dir():
        print(f"[pytest] No suite in {tests}")
        return 0
    # Template pytest needs the built-in module, so execute it in the GTest host.
    if (python_root / "prototype_example").is_dir():
        environment["SST_RUN_PYTEST"] = str(tests)
        environment["SST_REQUIRE_EMBEDDING"] = "1"
        return run_gtest(command + ["--gtest_filter=PrototypePython.EmbeddedRoundtrip"],
                         environment, require_embedding=True)
    environment["PYTHONPATH"] = str(python_root) + os.pathsep + environment.get("PYTHONPATH", "")
    return subprocess.run([uv, "run", "--no-sync", "--project", str(ROOT), "python", "-m", "pytest", str(tests)],
                          cwd=ROOT, env=environment, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
