from __future__ import annotations

import os
import re
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def project_name(prototype: str | None = None) -> str:
    name = prototype or os.environ.get("SST_PROTOTYPE") or (repo_root() / ".sst-prototype").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", name) or not (repo_root() / "Prototypes" / name / "premake5.lua").is_file():
        raise ValueError(f"Invalid or missing prototype: {name}")
    return name


def select_prototype(name: str | None) -> None:
    if name is not None:
        os.environ["SST_PROTOTYPE"] = project_name(name)


def test_target() -> str:
    return f"{project_name()}Tests"


def project_executable_name() -> str:
    return f"{project_name()}.exe" if os.name == "nt" else project_name()


def scripts_dir() -> Path:
    return repo_root() / "scripts"


def tools_dir() -> Path:
    return repo_root() / "tools"


def vendor_binaries_dir() -> Path:
    return repo_root() / "Vendor" / "Binaries"


def premake_dir() -> Path:
    return vendor_binaries_dir() / "Premake"


def mdbook_dir() -> Path:
    return vendor_binaries_dir() / "MdBook"


def plantuml_dir() -> Path:
    return vendor_binaries_dir() / "PlantUML"


def tracy_dir() -> Path:
    if os.name == "nt":
        return vendor_binaries_dir() / "Tracy" / "Windows"
    return vendor_binaries_dir() / "Tracy" / "Linux"


def tracy_profiler_path() -> Path:
    return tracy_dir() / ("tracy-profiler.exe" if os.name == "nt" else "tracy-profiler")


def local_config_path() -> Path:
    return repo_root() / ".local" / "toolchain.json"


def logs_dir() -> Path:
    return repo_root() / "logs"


def build_dir() -> Path:
    return repo_root() / "build"


def generated_dir(generator: str = "vs2022") -> Path:
    return build_dir() / "generated" / generator


def solution_path(generator: str = "vs2022") -> Path:
    return generated_dir(generator) / "SSTPrototypes.sln"


def target_executable_path(target: str, config: str, arch: str = "x86_64", generator: str = "vs2022") -> Path:
    system = "windows" if os.name == "nt" else "macosx" if sys.platform == "darwin" else "linux"
    suffix = "-gmake2" if generator == "gmake2" else ""
    output_key = f"{config}-{system}-{arch}{suffix}"
    executable = f"{target}.exe" if os.name == "nt" else target
    return build_dir() / "bin" / output_key / target / executable


def app_executable_path(config: str, arch: str = "x86_64", generator: str = "vs2022") -> Path:
    return target_executable_path(project_name(), config, arch, generator)


def test_executable_path(config: str, arch: str = "x86_64", generator: str = "vs2022") -> Path:
    return target_executable_path(test_target(), config, arch, generator)
