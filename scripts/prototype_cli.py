from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ACTIVE_PROTOTYPE_FILE = ROOT / ".sst-prototype"
GENERATED_VS_DIR = ROOT / "build" / "generated" / "vs2022"


def active_prototype() -> str:
	if not ACTIVE_PROTOTYPE_FILE.is_file():
		raise SystemExit("Missing .sst-prototype. Pass a prototype name or create the file.")

	name = ACTIVE_PROTOTYPE_FILE.read_text(encoding="utf-8").strip()
	if not name:
		raise SystemExit(".sst-prototype is empty. Pass a prototype name or set the current prototype.")
	return name


def prototype_name(argument: str | None) -> str:
	return argument or active_prototype()


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


def build_target(target: str, configuration: str) -> None:
	target_file = GENERATED_VS_DIR / target / f"{target}.vcxproj"
	if not target_file.is_file():
		raise SystemExit(f"Missing project file: {target_file}. Run premake first.")

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
	return parser


def run_executable(path: Path) -> int:
	if not path.is_file():
		raise SystemExit(f"Missing executable: {path}")

	return subprocess.run([str(path)], cwd=ROOT, check=False).returncode


def output_dir(configuration: str) -> Path:
	return ROOT / "build" / "bin" / f"{configuration}-windows-x86_64"


if __name__ == "__main__":
	print("Use build_prototype.py or test_prototype.py", file=sys.stderr)
	raise SystemExit(2)

