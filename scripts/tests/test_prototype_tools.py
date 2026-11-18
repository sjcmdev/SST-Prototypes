import sys
from pathlib import Path

import pytest

from scripts import generate_prototype as generator
from scripts.python.common import paths
from scripts.python import build, run


@pytest.fixture
def workspace(tmp_path, monkeypatch):
    template = Path(__file__).resolve().parents[2] / "PrototypeTemplate"
    monkeypatch.setattr(generator, "TEMPLATE_DIR", template)
    monkeypatch.setattr(generator, "PROTOTYPES_DIR", tmp_path / "Prototypes")
    monkeypatch.setattr(generator, "ACTIVE_PROTOTYPE_FILE", tmp_path / ".sst-prototype")
    monkeypatch.setattr(paths, "repo_root", lambda: tmp_path)
    monkeypatch.delenv("SST_PROTOTYPE", raising=False)
    return tmp_path


def generate(monkeypatch, name, *flags):
    monkeypatch.setattr(sys, "argv", ["generate_prototype", name, *flags])
    return generator.main()


@pytest.mark.parametrize("name", ["Demo", "CV_DLCP2"])
def test_generate_and_selection(workspace, monkeypatch, name):
    assert generate(monkeypatch, name) == 0
    target = workspace / "Prototypes" / name
    for path in target.rglob("*"):
        assert "__PROTOTYPE_" not in path.name
        if path.is_file():
            assert b"__PROTOTYPE_" not in path.read_bytes()
    assert paths.project_name() == name
    assert paths.solution_path().name == "SSTPrototypes.sln"
    assert paths.app_executable_path("Debug").parent.name == name
    assert paths.test_executable_path("Release").parent.name == name + "Tests"
    assert "-gmake2" in str(paths.test_executable_path("Dist", generator="gmake2"))


@pytest.mark.parametrize("name", ["", "../Escape", "1Demo", "bad-name", "bad name", "CON", "lpt1"])
def test_invalid_names(workspace, monkeypatch, name):
    assert generate(monkeypatch, name) == 2
    assert not (workspace / "Prototypes").exists()


@pytest.mark.parametrize("flags", [(), ("--force",)])
def test_conflict_preserves_data_and_active_selection(workspace, monkeypatch, flags):
    assert generate(monkeypatch, "Demo") == 0
    sentinel = workspace / "Prototypes/Demo/owner.txt"
    sentinel.write_text("owner changes")
    assert generate(monkeypatch, "Demo", *flags) == 1
    assert sentinel.read_text() == "owner changes"
    assert generate(monkeypatch, "Other") == 0
    assert paths.project_name() == "Demo"


@pytest.mark.parametrize("name", ["JFM", "Generated"])
def test_build_run_dry_run(workspace, monkeypatch, capsys, name):
    assert generate(monkeypatch, name) == 0
    monkeypatch.setattr(build, "detect_tool", lambda _: "MSBuild.exe")
    monkeypatch.setattr(build, "load_preferences", lambda: {"build": {"target": "obsolete"}})
    monkeypatch.setattr(run, "load_preferences", lambda: {})
    assert build.run(build.make_parser().parse_args(["--prototype", name, "--dry-run"])) == 0
    assert run.run(run.make_parser().parse_args(["--prototype", name, "--dry-run"])) == 0
    output = capsys.readouterr().out
    assert f"{name}Tests" == paths.test_target()
    assert "SSTPrototypes.sln" in output
    assert f"{name}.vcxproj" in output
    assert str(paths.app_executable_path("Debug")) in output
    assert "obsolete" not in output


@pytest.mark.parametrize("name", ["JFM", "Generated"])
def test_ci_and_matrix_selection(workspace, monkeypatch, capsys, name):
    from scripts.python import ci_check, test_release_dist_tests as matrix
    assert generate(monkeypatch, name) == 0
    monkeypatch.setattr(build, "detect_tool", lambda _: "MSBuild.exe")
    monkeypatch.setattr(build, "load_preferences", lambda: {})
    monkeypatch.setattr(ci_check.generate_script, "run", lambda _: 0)
    assert ci_check.run(ci_check.make_parser().parse_args(["--prototype", name, "--dry-run"])) == 0
    output = capsys.readouterr().out
    assert "SSTPrototypes.sln" in output
    assert f"{name}.vcxproj" in output
    assert f"{name}Tests.vcxproj" in output
    assert str(paths.test_executable_path("Debug")) in output
    assert matrix.run(matrix.make_parser().parse_args(["--prototype", name, "--compiler", "msvc", "--dry-run"])) == 0
    output = capsys.readouterr().out
    assert f"--target {name}Tests" in output
    assert str(paths.test_executable_path("Release")) in output
    assert str(paths.test_executable_path("Dist")) in output
    assert matrix.resolve_test_executable("Dist", "gcc") == paths.test_executable_path("Dist", generator="gmake2")


def test_ci_rejects_incompatible_generator(workspace, monkeypatch):
    from scripts.python import ci_check
    assert generate(monkeypatch, "Demo") == 0
    assert ci_check.run(ci_check.make_parser().parse_args(["--compiler", "gcc", "--generator", "vs2022", "--dry-run"])) == 2
