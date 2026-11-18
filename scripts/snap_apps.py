"""Capture SST main framebuffers. Detached ImGui viewports are excluded.

Run from the repository root: .venv/Scripts/python scripts/snap_apps.py --build --test
Uses the existing embedded CapLab API to prepare fixture data. All build/test
processes use the owner's global build lock. Each app runs in an isolated copy
of its installed assets/config, so gallery runs never save owner settings.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from prototype_cli import ROOT, msbuild_path, target_executable_path

LOCK = ROOT.parent / "tools/agents/build-lock.sh"
SHOTS = ROOT.parent / "tools/agents/runs/gallery/shots"
APPS = {"CapLab": "CapLab", "JFM": "JFM", "PlotLab": "PlotLab",
        "WindowChromeLab": "WindowChromeLab", "PrototypeTemplate": "GalleryTemplate"}


def run_locked(command, **kwargs):
    # Windows' system bash is WSL; use Git Bash, which owns this lock script.
    bash = next((str(Path(entry) / "bash.exe") for entry in os.environ["PATH"].split(os.pathsep)
                 if "git" in entry.lower() and (Path(entry) / "bash.exe").is_file()), "bash")
    arguments = [str(value).replace("\\", "/") for value in command]
    return subprocess.run([bash, LOCK.as_posix(), *arguments], cwd=ROOT, check=True, **kwargs)


def prepare_template():
    destination = ROOT / "build/gallery-template"
    shutil.copytree(ROOT / "PrototypeTemplate", destination, dirs_exist_ok=True)
    for path in destination.rglob("*"):
        if path.is_file():
            try:
                text = path.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            path.write_text(text.replace("__PROTOTYPE_NAME_UPPER__", "GALLERYTEMPLATE")
                            .replace("__PROTOTYPE_NAME__", "GalleryTemplate"), encoding="utf-8")
    for path in sorted(destination.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        if "__PROTOTYPE_NAME__" in path.name:
            path.replace(path.with_name(path.name.replace("__PROTOTYPE_NAME__", "GalleryTemplate")))


def build(configuration):
    prepare_template()
    run_locked([ROOT / "Vendor/Binaries/Premake/Windows/premake5.exe",
                "--file=scripts/snap_apps.premake5.lua", "vs2022"])
    with (ROOT / "build/gallery-build.log").open("w", encoding="utf-8") as log:
        run_locked([msbuild_path(), ROOT / "build/generated/vs2022/SSTPrototypes.sln",
                    f"-p:Configuration={configuration}", "-p:Platform=x64", "-nr:false", "-m:1", "-v:minimal"],
                   stdout=log, stderr=subprocess.STDOUT)


def embedded_environment():
    env = os.environ.copy()
    env["SST_REQUIRE_EMBEDDING"] = "1"
    env["SST_ROOT_PYTHON_PATHS"] = json.dumps(sys.path + [str(path) for path in (ROOT / "Prototypes").glob("*/python")])
    env["PYTEST_ADDOPTS"] = env.get("PYTEST_ADDOPTS", "") + f" --basetemp={(ROOT / 'build/gallery-pytest').as_posix()}"
    return env


def run_tests(configuration):
    failures = []

    def attempt(command, **kwargs):
        try:
            run_locked(command, **kwargs)
        except subprocess.CalledProcessError as error:
            failures.append({"command": list(map(str, command)), "exit_code": error.returncode})

    # Every generated suite, sequentially, always from the repository root.
    for project in sorted((ROOT / "build/generated/vs2022").glob("*Tests/*Tests.vcxproj")):
        target = project.stem
        exe = target_executable_path(target, configuration)
        env = embedded_environment()
        attempt([exe, f"--gtest_output=xml:{ROOT / 'build' / (target + '-gallery.xml')}"], env=env)
        name = target.removesuffix("Tests")
        python_root = ROOT / "build/gallery-template/python" if name == "GalleryTemplate" else ROOT / "Prototypes" / name / "python"
        tests = python_root / "tests"
        if not tests.is_dir():
            continue
        if name in {"CapLab", "WindowChromeLab", "GalleryTemplate", "SD1Template"}:
            env["SST_RUN_PYTEST"] = str(tests)
            attempt([exe, "--gtest_filter=PrototypePython.EmbeddedRoundtrip"], env=env)
        else:
            env["PYTHONPATH"] = str(python_root)
            attempt([sys.executable, "-m", "pytest", str(tests), "-q", "-p", "no:cacheprovider"], env=env)
    attempt([sys.executable, "-m", "pytest", "scripts/tests", "-q", "-p", "no:cacheprovider"], env=embedded_environment())
    (ROOT / "build/gallery-test-failures.json").write_text(json.dumps(failures, indent=2), encoding="utf-8")
    return failures


def prepare_caplab(configuration, output):
    env = embedded_environment()
    env["SST_GALLERY_PROJECT"] = str(output)
    env["SST_RUN_PYTEST"] = str(ROOT / "scripts/tests/test_gallery_fixture.py")
    run_locked([target_executable_path("CapLabTests", configuration),
                "--gtest_filter=PrototypePython.EmbeddedRoundtrip"], env=env)
    if not output.is_file():
        raise RuntimeError("Embedded CapLab API did not create a project")


def inspect_png(path):
    from PIL import Image, ImageStat
    with Image.open(path) as image:
        image.load()
        variance = sum(ImageStat.Stat(image.convert("RGB")).var)
        if variance <= 0:
            raise ValueError(f"Uniform screenshot: {path}")
        return {"width_px": image.width, "height_px": image.height,
                "size_bytes": path.stat().st_size, "pixel_variance": variance}


def prepare_gallery_layout(sandbox, *, fresh=False):
    """Keep copied workspace panels inside the main framebuffer, with tools collapsed."""
    import yaml
    config = sandbox / "install/app/config"
    settings = config / "ui_settings.yaml"
    data = yaml.safe_load(settings.read_text(encoding="utf-8-sig")) if settings.is_file() else {}
    data.setdefault("window", {}).update(x=0, y=0, width=1280, height=720, maximized=False)
    settings.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
    layout = config / "layouts/imgui.ini"
    previous = layout.read_text(encoding="utf-8-sig") if layout.is_file() else ""
    workspace = next((line[5:] for line in previous.splitlines() if line.startswith("Data=")), None)
    if fresh:
        workspace = None
    if workspace:
        envelope = json.loads(workspace)
        state = json.loads(envelope["state"])
        for window in state["windows"]:
            window["state"].update(x=10, y=40, width=1260, height=630, dock=0)
        envelope["state"] = json.dumps(state)
        workspace = json.dumps(envelope)
    text = "[Window][sst-window-1]\nPos=10,40\nSize=1260,630\nCollapsed=0\n\n"
    for index, title in enumerate(["Settings", "Logging Panel", "Task Monitor", "Progress Monitor"]):
        text += f"[Window][{title}]\nPos={10 + index * 290},690\nSize=280,200\nCollapsed=1\n\n"
    if workspace:
        text += f"[SSTWindows][Workspace]\nData={workspace}\n"
    layout.parent.mkdir(parents=True, exist_ok=True)
    layout.write_text(text, encoding="utf-8")


def capture(app, target, configuration, output, open_path, frames):
    executable = target_executable_path(target, configuration)
    sandbox = ROOT / "build/gallery-runs" / app
    sandbox.mkdir(parents=True, exist_ok=True)
    shutil.copytree(executable.parent / "install", sandbox / "install", dirs_exist_ok=True)
    if (executable.parent / "python").is_dir():
        shutil.copytree(executable.parent / "python", sandbox / "python", dirs_exist_ok=True)
    if app != "JFM":
        prepare_gallery_layout(sandbox, fresh=app == "CapLab")
    # Running an executable beside copied config would resolve its original config.
    # CWD takes precedence in Core, so this portable install isolates all writes.
    shot = output / f"{app}.png"
    shot.unlink(missing_ok=True)
    log = sandbox / "capture.log"
    command = [str(executable), "--screenshot", str(shot), "--screenshot-after-frames", str(frames),
               "--exit-after-screenshot", f"--log-file={log}"]
    if open_path:
        command += ["--open", str(open_path)]
    result = subprocess.run(command, cwd=sandbox, timeout=120, check=False)
    if result.returncode:
        raise RuntimeError(f"{app} exited with {result.returncode}; see {log}")
    info = inspect_png(shot)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    return {"app": app, "path_png": str(shot), **info, "requested_data": str(open_path) if open_path else None,
            "open_dispatched": "Startup --open dispatched:" in text, "log": str(log)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--test", action="store_true")
    parser.add_argument("--configuration", choices=["Debug", "Release", "Dist"], default="Debug")
    parser.add_argument("--output", type=Path, default=SHOTS)
    parser.add_argument("--frames", type=int, default=30)
    args = parser.parse_args()
    if args.frames < 1:
        parser.error("--frames must be positive")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.build:
        build(args.configuration)
    failures = run_tests(args.configuration) if args.test else []
    caplab = args.output / "CapLab-fixture.caplab"
    prepare_caplab(args.configuration, caplab)
    inputs = {"CapLab": caplab, "JFM": ROOT / "Prototypes/JFM/JFM-projects/HZB-project.yaml.jfm.yaml",
              "PlotLab": ROOT / "Prototypes/PlotLab/examples/demo.csv"}
    results = []
    for app, target in APPS.items():
        try:
            result = capture(app, target, args.configuration, args.output, inputs.get(app), args.frames)
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
            result = {"app": app, "error": str(error)}
        results.append(result)
        print(json.dumps(result, ensure_ascii=False), flush=True)
    (args.output / "report.json").write_text(json.dumps(results, indent=2, ensure_ascii=False), encoding="utf-8")
    return int(bool(failures) or any("error" in result for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
