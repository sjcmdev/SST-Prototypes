#pragma once

#include "lm_core.hpp"

// JFM CPU parity fixtures use [I0, A, Rs, Rsh, alpha, Rsh2].
[[nodiscard]] inline double evaluateDiodeIV4(double voltage, const double* p, double temperature = 300.0) noexcept
{
    return lm::current(GpuModelType::SDRsRsh, voltage, p, temperature, nullptr, true);
}

[[nodiscard]] inline double evaluateDiodeIV6(double voltage, const double* p, double temperature = 300.0) noexcept
{
    return lm::current(GpuModelType::SDRsRshAlphaRsh2, voltage, p, temperature, nullptr, true);
}
