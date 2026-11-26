import os
from pathlib import Path

import pytest


@pytest.fixture
def real_data_file():
    def require(relative_path):
        root = Path(os.environ.get("SST_JFM_REAL_DATA") or
                    Path(__file__).resolve().parents[2] / "example-data" / "real-measurement")
        path = root / relative_path
        if not path.is_file():
            pytest.skip(f"JFM real-measurement file missing: {path}; set SST_JFM_REAL_DATA to the data root")
        return path

    return require
