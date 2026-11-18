"""Small Python consumer of the embedded C++ module."""
from importlib import import_module


def conductivity(voltage, temperature=300.0):
    return import_module("sst___PROTOTYPE_NAME_LOWER__").conductivity(voltage, temperature)
