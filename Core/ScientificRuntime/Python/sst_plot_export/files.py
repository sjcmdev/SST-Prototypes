"""Atomic replacement for a staged plot or lab-table batch; no plotting dependencies."""
import os
from pathlib import Path
import shutil
import tempfile


def commit_files(files, cancelled=lambda: False):
    backups, committed = {}, []
    try:
        for _, path in files:
            if path.exists():
                fd, name = tempfile.mkstemp(prefix=".sst-export-backup-", dir=path.parent)
                os.close(fd)
                backups[path] = Path(name)
                shutil.copyfile(path, name)
        if cancelled():
            raise RuntimeError("Export cancelled")
        try:
            for temporary, path in files:
                os.replace(temporary, path)
                committed.append(path)
        except OSError as commit_error:
            failures = []
            for path in reversed(committed):
                if path in backups:
                    backup = backups.pop(path)
                    try:
                        os.replace(backup, path)
                    except OSError:
                        failures.append(str(backup))
                else:
                    try:
                        path.unlink()
                    except OSError:
                        failures.append(str(path))
            if failures:
                raise RuntimeError("Restore failed; retained backups / new files: " + ", ".join(failures)) from commit_error
            raise
    finally:
        for backup in backups.values():
            backup.unlink(missing_ok=True)
