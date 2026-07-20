// src/solver/phys_const.hpp
#pragma once

/// Stałe fizyczne dla modelu IV diody.
namespace PhysConst
{

    /// Stała Boltzmanna [J/K]
    static constexpr double k_B = 1.380649e-23;

    /// Ładunek elementarny [C]
    static constexpr double q = 1.602176634e-19;

    /// Stała modelu IV [V/K]. Musi być identyczna w Pythonie, CPU i CUDA.
    static constexpr double k = 8.6e-5;

    /// Temperatura referencyjna [K]
    static constexpr double T_ref = 300.0;

    /// Napięcie termiczne przy T_ref [V]
    static constexpr double Vt_ref = k * T_ref;
}
