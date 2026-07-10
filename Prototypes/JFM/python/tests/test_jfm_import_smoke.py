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
    result = parse_dat(REPO / "Prototypes/JFM/example-data/IV_C002_Z015_6_290,0K.dat")

    assert result["ok"] is True
    assert result["parser"] == "dat"
    assert result["curves"]
    assert isinstance(result["curves"][0]["data"], np.ndarray)
    assert result["temperature_K"] == 290.0
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

    result = import_file(REPO / "Prototypes/JFM/example-data/IV_C002_Z015_6_290,0K.dat")
    characteristic = result["characteristics"][0]

    assert result["parser"] == "dat"
    assert characteristic.info.source_path.endswith("IV_C002_Z015_6_290,0K.dat")
    assert characteristic.info.columns[0].role == "voltage"
    assert characteristic.info.temperature_K == 290.0
    assert characteristic.data.voltage
    assert len(characteristic.data.current) == len(characteristic.data.voltage)
