from __future__ import annotations

from pathlib import Path
import sys
from types import SimpleNamespace

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]
sys.path.insert(0, str(ROOT))

from jfm_import.dat_parser import parse_dat
from jfm_import.scaps_iv_parser import parse_scaps_iv


def test_dat_parser_returns_unified_shape() -> None:
    result = parse_dat(REPO / "Prototypes/JFM/example-data/C002/IVT C002_Z015_6/IV_C002_Z015_6_300,0K.dat")

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


def test_cpp_adapter_returns_bound_shape(monkeypatch) -> None:
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

    result = import_file(REPO / "Prototypes/JFM/example-data/C002/IVT C002_Z015_6/IV_C002_Z015_6_300,0K.dat")
    characteristic = result["characteristics"][0]

    assert result["parser"] == "dat"
    assert characteristic.info.source_path.endswith("IV_C002_Z015_6_300,0K.dat")
    assert characteristic.info.source_record_key == "0"
    assert characteristic.info.columns[0].role == "voltage"
    assert characteristic.info.temperature_K == 300.0
    assert characteristic.data.voltage
    assert len(characteristic.data.current) == len(characteristic.data.voltage)


def test_cpp_adapter_applies_parse_options(monkeypatch) -> None:
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
        REPO / "Prototypes/JFM/example-data/C002/IVT C002_Z015_6/IV_C002_Z015_6_300,0K.dat",
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
