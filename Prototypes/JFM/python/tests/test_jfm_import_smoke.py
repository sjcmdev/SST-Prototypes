from __future__ import annotations

from pathlib import Path
import sys
from types import SimpleNamespace

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
SYNTHETIC_FILE = REPO / "Prototypes/JFM/example-data/syntetic-data/single-diode/canonical/SD-0/T-300K/I0-1e-10__A-1p5/clean.dat"
sys.path.insert(0, str(ROOT))

from jfm_import.dat_parser import parse_dat
from jfm_import.scaps_iv_parser import parse_scaps_iv
from jfm_import.common import build_curve_name


@pytest.fixture
def dat_file(real_data_file):
    return real_data_file("C002/IVT C002_Z015_6/IV_C002_Z015_6_300,0K.dat")


def test_dat_parser_returns_unified_shape(dat_file) -> None:
    result = parse_dat(dat_file)

    assert result["ok"] is True
    assert result["parser"] == "dat"
    assert result["curves"]
    assert isinstance(result["curves"][0]["data"], np.ndarray)
    assert result["temperature_K"] == 300.0
    assert result["curves"][0]["sweep"]["direction"] in {
        "increasing",
        "decreasing",
        "non_monotonic",
        "loop",
    }


def test_scaps_parser_returns_curves() -> None:
    result = parse_scaps_iv(REPO / "Prototypes/JFM/example-data/scaps-batch-file.iv")

    assert result["ok"] is True
    assert result["parser"] == "scaps_iv"
    assert result["curves"]
    assert isinstance(result["curves"][0]["data"], np.ndarray)


def test_cpp_adapter_returns_bound_shape(monkeypatch, dat_file) -> None:
    class JFMColumn:
        pass

    class JFMSweepInfo:
        pass

    class JFMQualityControl:
        pass

    class JFMCharacteristic:
        pass

    monkeypatch.setitem(
        sys.modules,
        "sst_jfm",
        SimpleNamespace(
            JFMColumn=JFMColumn,
            JFMSweepInfo=JFMSweepInfo,
            JFMQualityControl=JFMQualityControl,
            JFMCharacteristic=JFMCharacteristic,
            JFMCharacteristicInfo=SimpleNamespace,
            JFMCharacteristicData=SimpleNamespace,
        ),
    )

    from jfm_import.cpp_adapter import import_file

    result = import_file(dat_file)
    characteristic = result["characteristics"][0]

    assert result["parser"] == "dat"
    assert characteristic.info.source_path.endswith("IV_C002_Z015_6_300,0K.dat")
    assert characteristic.info.source_record_key == "0"
    assert characteristic.info.columns[0].role == "voltage"
    assert characteristic.info.temperature_K == 300.0
    assert characteristic.data.voltage
    assert len(characteristic.data.current) == len(characteristic.data.voltage)


def test_cpp_adapter_applies_parse_options(monkeypatch, dat_file) -> None:
    class JFMColumn:
        pass

    class JFMSweepInfo:
        pass

    class JFMQualityControl:
        pass

    class JFMCharacteristic:
        pass

    monkeypatch.setitem(
        sys.modules,
        "sst_jfm",
        SimpleNamespace(
            JFMColumn=JFMColumn,
            JFMSweepInfo=JFMSweepInfo,
            JFMQualityControl=JFMQualityControl,
            JFMCharacteristic=JFMCharacteristic,
            JFMCharacteristicInfo=SimpleNamespace,
            JFMCharacteristicData=SimpleNamespace,
        ),
    )

    from jfm_import.cpp_adapter import import_file

    result = import_file(
        dat_file,
        {
            "parser": "dat",
            "name_pattern": "curve_{stem}",
            "short_name_pattern": "{sample}_{T}",
            "variable_delimiter": "_",
            "custom_variables": [
                {
                    "name": "sample",
                    "source": "stem",
                    "token_index": 1,
                    "substring_start": 0,
                    "substring_length": -1,
                }
            ],
            "decimal_separator": "auto",
        },
    )

    assert result["characteristics"][0].info.name.startswith("curve_IV_C002")
    assert result["characteristics"][0].info.metadata["short_name"] == "C002_300.0"


def test_import_variables_include_paths_and_allow_overrides(monkeypatch, dat_file) -> None:
    class JFMColumn:
        pass

    class JFMSweepInfo:
        pass

    class JFMQualityControl:
        pass

    class JFMCharacteristic:
        pass

    monkeypatch.setitem(
        sys.modules,
        "sst_jfm",
        SimpleNamespace(
            JFMColumn=JFMColumn,
            JFMSweepInfo=JFMSweepInfo,
            JFMQualityControl=JFMQualityControl,
            JFMCharacteristic=JFMCharacteristic,
            JFMCharacteristicInfo=SimpleNamespace,
            JFMCharacteristicData=SimpleNamespace,
        ),
    )

    from jfm_import.cpp_adapter import import_file

    result = import_file(
        dat_file,
        {
            "parser": "dat",
            "short_name_pattern": "{T}_{parent_folder_1}_{file_name}",
            "variable_delimiter": "_",
            "custom_variables": [
                {
                    "name": "T",
                    "source": "parent_folder_1",
                    "token_index": -1,
                    "substring_start": 0,
                    "substring_length": -1,
                }
            ],
        },
    )

    metadata = result["characteristics"][0].info.metadata
    assert metadata["T"] == "C002"
    assert metadata["file_name"].endswith(".dat")
    assert metadata["parent_folder_1"] == "C002"
    assert Path(metadata["parent_path_1"]) == dat_file.parent.parent
    assert metadata["short_name"].startswith("C002_C002_IV_C002")


def test_synthetic_single_diode_import_gets_temperature_from_path() -> None:
    result = parse_dat(SYNTHETIC_FILE)

    metadata = result["curves"][0]["metadata"]
    assert result["temperature_K"] == 300.0
    assert metadata["temperature_source"] == "path"
    assert metadata["parent_folder_1"] == "T-300K"


def test_custom_variable_override_tolerates_partial_numeric_fields() -> None:
    result = parse_dat(
        SYNTHETIC_FILE,
        {
            "short_name_pattern": "{T}_{stem}",
            "custom_variables": [
                {
                    "name": "T",
                    "source": "parent_folder_1",
                    "token_index": "",
                    "substring_start": "",
                    "substring_length": "",
                }
            ],
        },
    )

    metadata = result["curves"][0]["metadata"]
    assert metadata["T"] == "T-300K"
    assert metadata["short_name"] == "T-300K_clean"


def test_incomplete_name_pattern_keeps_preview_metadata_alive() -> None:
    assert build_curve_name({"stem": "sample"}, "{stem") == "sample"


def test_cpp_adapter_custom_script(monkeypatch, tmp_path: Path) -> None:
    class JFMColumn:
        pass

    class JFMSweepInfo:
        pass

    class JFMQualityControl:
        pass

    class JFMCharacteristic:
        pass

    monkeypatch.setitem(
        sys.modules,
        "sst_jfm",
        SimpleNamespace(
            JFMColumn=JFMColumn,
            JFMSweepInfo=JFMSweepInfo,
            JFMQualityControl=JFMQualityControl,
            JFMCharacteristic=JFMCharacteristic,
            JFMCharacteristicInfo=SimpleNamespace,
            JFMCharacteristicData=SimpleNamespace,
        ),
    )
    script = tmp_path / "custom_parser.py"
    script.write_text(
        "import numpy as np\n"
        "def parse(path, options):\n"
        "    return {'parser': 'custom', 'warnings': [], 'curves': [{\n"
        "        'name': 'custom_curve', 'measurement': 'IV',\n"
        "        'columns': [{'name': 'V', 'unit': 'V', 'role': 'voltage'}, {'name': 'I', 'unit': 'A', 'role': 'current'}],\n"
        "        'data': np.asarray([[0.0, 1.0], [1.0, 2.0]]),\n"
        "        'sweep': {'direction': 'increasing', 'v_start': 0.0, 'v_stop': 1.0, 'n_points': 2},\n"
        "        'metadata': {'temperature_source': 'missing'}, 'warnings': []\n"
        "    }]}\n",
        encoding="utf-8",
    )

    from jfm_import.cpp_adapter import import_file

    result = import_file("dummy.dat", {"parser": "custom", "custom_script_path": str(script)})

    assert result["parser"] == "custom"
    assert result["characteristics"][0].info.name == "custom_curve"
