K_BOLTZMANN_EV = 8.6e-5


def thermal_voltage(temperature_k: float) -> float:
    if temperature_k <= 0.0:
        raise ValueError("temperature_k must be positive")
    return K_BOLTZMANN_EV * temperature_k
