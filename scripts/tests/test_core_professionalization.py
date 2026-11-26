"""F02/F09/F13 process-boundary acceptance; requires this worktree's MSVC builds."""
from pathlib import Path
import hashlib
import os
import subprocess
import time
import pytest

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / 'build/bin/Release-windows-x86_64'


@pytest.fixture
def tmp_path(tmp_path_factory, request):
    # Recovery generations add nested paths; keep each test's root short and unique.
    root = tmp_path_factory.getbasetemp() / hashlib.sha256(request.node.nodeid.encode()).hexdigest()[:8]
    root.mkdir()
    return root


def executable(app):
    result = BIN / app / (app + '.exe')
    assert result.is_file(), f'Build required: {result}'
    return result


def environment(root):
    return dict(os.environ, SST_DIAGNOSTICS_ROOT=str(root / 'diagnostics'),
                SST_PORTABLE_ROOT=str(root / 'portable'), PYTHONDONTWRITEBYTECODE='1')


def wait_for(predicate, process, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        assert process.poll() is None, f'Probe exited before checkpoint: {process.returncode}'
        time.sleep(.002)
    raise AssertionError('Probe did not reach the required write checkpoint')


def kill_and_restart(root, variable, test, ready, app='CoreProfessionalTests', extra=None):
    env = environment(root)
    env[variable] = 'write'
    env.update(extra or {})
    command = [executable(app), '--gtest_filter=' + test]
    with (root / 'writer.log').open('wb') as log:
        writer = subprocess.Popen(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            wait_for(lambda: ready(root), writer)
            writer.kill()
            writer.wait(timeout=15)
            assert writer.returncode != 0
        finally:
            if writer.poll() is None:
                writer.kill()
                writer.wait(timeout=15)
    env[variable] = 'restore'
    result = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=30)
    (root / 'restore.log').write_bytes(result.stdout + result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr


def test_kill_during_autosave_restores_last_committed_generation(tmp_path):
    kill_and_restart(tmp_path, 'SST_RECOVERY_TEST_MODE', 'CoreRecoveryProcess.KillProbe',
                     lambda root: (root / 'diagnostics/pro-core2-tests/kill-restart/kill-ready').is_file())


def test_kill_during_plot_save_recovers_writing_reservation(tmp_path):
    def ready(root):
        scratch = root / 'diagnostics/pro-core2-tests'
        return ((scratch / 'plot-baseline-ready').is_file() and
                (scratch / 'kill-reservation.plotlab.writing/owner').is_file())
    kill_and_restart(tmp_path, 'SST_PLOT_SAVE_TEST_MODE', 'CoreRecoveryProcess.PlotSaveKillProbe', ready)
    scratch = tmp_path / 'diagnostics/pro-core2-tests'
    assert list(scratch.glob('*.writing.interrupted-*')), 'Interrupted bytes must remain available'


def test_caplab_kill_restart_preserves_original_and_restores_real_project(tmp_path):
    root = tmp_path / 'portable/CapLab/config/recovery'
    kill_and_restart(tmp_path, 'SST_CAP_RECOVERY_TEST_MODE',
                     'CapLabRecovery.RestartRestoresCopyWithoutOverwritingOriginal',
                     lambda _: (root / 'kill-ready').is_file(), app='CapLabTests',
                     extra={'SST_CAP_RECOVERY_ROOT': str(root)})


def test_cli_io_failure_uses_versioned_exit_contract(tmp_path):
    result = subprocess.run([executable('PlotLab'), '--worksheet-script', tmp_path / 'missing.py'],
                            cwd=tmp_path, env=environment(tmp_path), capture_output=True, timeout=45)
    assert result.returncode == 4, (result.returncode, result.stdout, result.stderr)
    assert b'path and permissions' in result.stderr


def test_python_error_adapter_and_exit_contract_are_json_serializable(tmp_path):
    script = tmp_path / 'contract.py'
    script.write_text('import json, sst_application\n'
                      'info = sst_application.exit_contract()\n'
                      'assert info["version"] == 1\n'
                      'assert info["cancelled"] == 130 and info["timed_out"] == 124\n'
                      'assert info["partial"] == 3 and info["io"] == 4\n'
                      'preview = sst_application.reset_settings("interface")\n'
                      'assert preview["ok"] and not preview["persisted"], preview\n'
                      'saved = sst_application.reset_settings("system", persist=True)\n'
                      'assert saved["ok"] and saved["persisted"], saved\n'
                      'bad = sst_application.reset_settings("missing")\n'
                      'assert not bad["ok"] and bad["error"]["category"] == "invalid_input", bad\n'
                      'json.dumps(info)\n', encoding='utf-8')
    result = subprocess.run([executable('PlotLab'), '--worksheet-script', script],
                            cwd=ROOT, env=environment(tmp_path), capture_output=True, timeout=45)
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)


def test_jfm_kill_during_import_keeps_dirty_document_and_restores_copy(tmp_path):
    kill_and_restart(tmp_path, 'SST_JFM_RECOVERY_TEST_MODE',
                     'JFMRecoveryProcess.KillDuringImportKeepsDirtyDocument',
                     lambda root: (root / 'kill-ready').is_file(), app='JFMTests')
    assert (tmp_path / 'restored-copy.jfmb').is_dir()
