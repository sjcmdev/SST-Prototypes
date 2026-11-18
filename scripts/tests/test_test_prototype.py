import importlib.util
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("prototype_test_runner", SCRIPTS / "test_prototype.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


@pytest.mark.parametrize("xml,required,passes", [
    ('<testsuites><testsuite><testcase/></testsuite></testsuites>', True, True),
    ('<testsuites><testsuite><testcase><skipped/></testcase></testsuite></testsuites>', True, False),
    ('<testsuites><testsuite><testcase><skipped/></testcase></testsuite></testsuites>', False, True),
    ('<testsuites/>', True, False),
])
def test_required_embedding_cannot_pass_via_skip(tmp_path, monkeypatch, xml, required, passes):
    (tmp_path / "build").mkdir()
    monkeypatch.setattr(runner, "ROOT", tmp_path)

    def run(command, **kwargs):
        report = next(item.split("xml:", 1)[1] for item in command if item.startswith("--gtest_output="))
        Path(report).write_text(xml)
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(runner.subprocess, "run", run)
    if passes:
        assert runner.run_gtest(["tests.exe"], {}, required) == 0
    else:
        with pytest.raises(SystemExit):
            runner.run_gtest(["tests.exe"], {}, required)


def test_gtest_failure_stops_runner(tmp_path, monkeypatch):
    (tmp_path / "build").mkdir()
    monkeypatch.setattr(runner, "ROOT", tmp_path)
    monkeypatch.setattr(runner.subprocess, "run", lambda *args, **kwargs: SimpleNamespace(returncode=7))
    assert runner.run_gtest(["tests.exe"], {}, True) == 7
