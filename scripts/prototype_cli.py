from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


sys.path.insert(0, str(ROOT))
from scripts.python.common.paths import project_name, solution_path, target_executable_path


def active_prototype() -> str:
    return project_name()


def prototype_name(argument: str | None) -> str:
    return project_name(argument)


def msbuild_path() -> str:
	if path := shutil.which("msbuild"):
		return path

	vswhere = Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
	if vswhere.is_file():
		result = subprocess.run(
			[
				str(vswhere),
				"-latest",
				"-requires",
				"Microsoft.Component.MSBuild",
				"-find",
				"MSBuild/**/Bin/MSBuild.exe",
			],
			cwd=ROOT,
			text=True,
			stdout=subprocess.PIPE,
			stderr=subprocess.PIPE,
			check=False,
		)
		for line in result.stdout.splitlines():
			candidate = line.strip()
			if candidate:
				return candidate

	raise SystemExit("MSBuild was not found in PATH or via vswhere.")


def build_target(target: str, configuration: str, dry_run: bool = False) -> None:
	target_file = solution_path().parent / target / f"{target}.vcxproj"
	if not target_file.is_file() and not dry_run:
		raise SystemExit(f"Missing project file: {target_file}. Run premake first.")

	if dry_run:
		print(f"{solution_path()} target={target} project={target_file} executable={target_executable_path(target, configuration)}")
		return

	subprocess.run(
		[
			msbuild_path(),
			str(target_file),
			f"/p:Configuration={configuration}",
			"/p:Platform=x64",
		],
		cwd=ROOT,
		check=True,
	)


def build_parser(description: str) -> argparse.ArgumentParser:
	parser = argparse.ArgumentParser(description=description)
	parser.add_argument("prototype", nargs="?", help="Prototype name. Defaults to .sst-prototype.")
	parser.add_argument("--configuration", default="Debug", choices=["Debug", "Release", "Dist"])
	parser.add_argument("--dry-run", action="store_true")
	return parser


def run_executable(path: Path, dry_run: bool = False) -> int:
	if dry_run:
		print(f"run={path} cwd={ROOT}")
		return 0
	if not path.is_file():
		raise SystemExit(f"Missing executable: {path}")

	return subprocess.run([str(path)], cwd=ROOT, check=False).returncode



if __name__ == "__main__":
	print("Use build_prototype.py or test_prototype.py", file=sys.stderr)
	raise SystemExit(2)

