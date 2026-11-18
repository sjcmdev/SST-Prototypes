import ctypes
from unittest.mock import Mock

import pytest

from scripts import smoke_shutdown


def install_windows(monkeypatch, windows):
    api = Mock()
    current = {}

    def enumerate_windows(callback, _):
        for handle, (pid, title, name) in enumerate(windows, 1):
            current["window"] = (pid, title, name)
            callback(handle, 0)

    api.EnumWindows.side_effect = enumerate_windows
    api.GetWindowThreadProcessId.side_effect = lambda handle, owner: setattr(owner._obj, "value", current["window"][0])
    api.GetClassNameW.side_effect = lambda handle, text, count: setattr(text, "value", current["window"][2])
    api.GetWindowTextW.side_effect = lambda handle, text, count: setattr(text, "value", current["window"][1])
    monkeypatch.setattr(ctypes, "WinDLL", lambda *args, **kwargs: api)


def test_selects_main_window_instead_of_progress_monitor(monkeypatch):
    install_windows(monkeypatch, [(42, "Progress Monitor", "ImGui Platform"),
                                 (42, "", "GLFW30"), (99, "CapLab", "GLFW30"),
                                 (42, "CapLab", "GLFW30")])
    assert smoke_shutdown.main_window(42, "CapLab") == 4


def test_missing_or_ambiguous_window_is_not_closed(monkeypatch):
    install_windows(monkeypatch, [])
    assert smoke_shutdown.main_window(42, "CapLab") == 0
    install_windows(monkeypatch, [(42, "CapLab", "GLFW30")] * 2)
    with pytest.raises(RuntimeError, match="Ambiguous"):
        smoke_shutdown.main_window(42, "CapLab")
