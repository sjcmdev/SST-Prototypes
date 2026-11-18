import pytest
from prototype_example import conductivity


def test_cpp_roundtrip():
    assert conductivity(2.0) == pytest.approx(4.4860945752)


def test_cpp_validation():
    with pytest.raises(RuntimeError):
        conductivity(2.0, 0.0)
