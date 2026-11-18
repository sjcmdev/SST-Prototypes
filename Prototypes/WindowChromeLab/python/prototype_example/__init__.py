"""Small Python consumer of the embedded C++ module."""
from importlib import import_module


def conductivity(voltage, temperature=300.0):
    return import_module("sst_windowchromelab").conductivity(voltage, temperature)
