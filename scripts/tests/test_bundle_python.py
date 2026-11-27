from pathlib import Path
import pytest
from scripts.python.bundle_python import bundle_python


def test_portable_runtime_includes_stdlib_dlls_and_project_packages(tmp_path: Path):
    base, packages, destination = (tmp_path / name for name in ("base", "packages", "app"))
    for relative in ("python.exe", "python313.dll", "Lib/encodings/__init__.py", "DLLs/_socket.pyd"):
        path = base / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(relative)
    packages.mkdir()
    (packages / "project_package.py").write_text("value = 1")
    bundle_python(destination, base, packages)
    for relative in ("python.exe", "python313.dll", "Lib/encodings/__init__.py", "DLLs/_socket.pyd",
                     "Lib/site-packages/project_package.py"):
        assert (destination / relative).is_file()
    with pytest.raises(ValueError, match="Incomplete Python runtime"):
        bundle_python(tmp_path / "invalid", packages, packages)
