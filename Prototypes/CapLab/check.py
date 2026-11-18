"""Build/test using template tools, with the owner's direct MSBuild contract.
Invoke from the repo root under tools/agents/build-lock.sh.
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
os.environ.setdefault("UV_CACHE_DIR", str(ROOT / "build/uv-cache"))
os.environ.setdefault("PYTHONDONTWRITEBYTECODE", "1")
validation = ROOT / "build/caplab-validation" / (sys.argv[1] if len(sys.argv) > 1 else "CapLab")
validation.mkdir(parents=True, exist_ok=True)
os.environ["PYTEST_ADDOPTS"] = (os.environ.get("PYTEST_ADDOPTS", "") +
    f" --basetemp={validation.as_posix()}/tmp -o cache_dir={validation.as_posix()}/cache")
sys.path.insert(0, str(ROOT / "scripts"))
import prototype_cli


def build_target(target, configuration, dry_run=False):
    generated = ROOT / "build/generated/vs2022"
    command = [prototype_cli.msbuild_path(), str(generated / target / f"{target}.vcxproj"),
               f"-p:Configuration={configuration}", "-p:Platform=x64", "-nr:false", "-m:1",
               f"-p:SolutionDir={generated.as_posix()}/", "-verbosity:minimal"]
    if dry_run:
        print(command)
    else:
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    prototype_cli.build_target = build_target
    if "--app" in sys.argv:
        sys.argv.remove("--app")
        import build_prototype
        raise SystemExit(build_prototype.main())
    import test_prototype
    raise SystemExit(test_prototype.main())
