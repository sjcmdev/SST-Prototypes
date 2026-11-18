from jfm_constants import K_BOLTZMANN_EV


def thermal_voltage(temperature_k: float) -> float:
    if temperature_k <= 0.0:
        raise ValueError("temperature_k must be positive")
    return K_BOLTZMANN_EV * temperature_k
