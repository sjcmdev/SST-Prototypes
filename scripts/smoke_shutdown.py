"""Measure WM_CLOSE -> process exit using the GLFW main window, not ImGui viewports.

Run under build-lock.sh. Configuration writes go to a temporary install copy.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.python.common.paths import target_executable_path


def main_window(process_id: int, title: str) -> int:
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    found = []

    @callback_type
    def visit(handle, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(handle, name, len(name))
        text = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(handle, text, len(text))
        if owner.value == process_id and name.value == "GLFW30" and text.value == title:
            found.append(handle)
        return True

    user32.EnumWindows(visit, 0)
    if len(found) > 1:
        raise RuntimeError(f"Ambiguous GLFW main window: {found}")
    return found[0] if found else 0


def measure(app: str, configuration: str, timeout_s: float, report_directory: Path | None = None) -> dict:
    executable = target_executable_path(app, configuration)
    if not executable.is_file():
        return {"app": app, "error": "missing_executable"}
    with tempfile.TemporaryDirectory(prefix=f"sd1-{app}-") as directory:
        workspace = Path(directory)
        shutil.copytree(executable.parent / "install", workspace / "install")
        with (workspace / "stdout.log").open("w") as output:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            process = subprocess.Popen([str(executable), "--log-file=" + str(workspace / "app.log")],
                                       cwd=workspace, stdout=output, stderr=output, startupinfo=startup)
            try:
                deadline = time.monotonic() + 30
                handle = 0
                while process.poll() is None and time.monotonic() < deadline:
                    handle = main_window(process.pid, app)
                    if handle:
                        break
                    time.sleep(0.05)
                if not handle:
                    return {"app": app, "error": "no_main_window", "exit_code": process.poll()}
                # Allow startup panels and embedded Python to initialize before closing.
                time.sleep(3)
                user32 = ctypes.WinDLL("user32", use_last_error=True)
                user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
                started = time.monotonic()
                if not user32.PostMessageW(handle, 0x0010, 0, 0):
                    raise ctypes.WinError(ctypes.get_last_error())
                try:
                    code = process.wait(timeout_s)
                    return {"app": app, "exit_time_s": round(time.monotonic() - started, 3),
                            "exit_code": code, "passed": code == 0 and time.monotonic() - started < 2}
                except subprocess.TimeoutExpired:
                    return {"app": app, "exit_time_s": timeout_s, "error": "shutdown_timeout", "passed": False}
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                if report_directory:
                    output.flush()
                    logs = "\n".join(path.read_text(encoding="utf-8", errors="replace")
                                     for path in workspace.rglob("*.log"))
                    (report_directory / f"sd1-{app}.log").write_text(logs, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apps", nargs="*", default=["JFM", "PlotLab", "CapLab", "WindowChromeLab", "SD1Template"])
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--timeout-s", type=float, default=15)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    results = [measure(app, args.configuration, args.timeout_s,
                       args.output.parent if args.output else None) for app in args.apps]
    report = json.dumps(results, indent=2)
    print(report)
    if args.output:
        args.output.write_text(report + "\n", encoding="utf-8")
    return 0 if all(result.get("passed") for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
