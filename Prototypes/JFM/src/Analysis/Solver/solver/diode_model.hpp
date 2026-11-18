#pragma once

#include <sst/physics/iv.hpp>

// JFM CPU parity fixtures use [I0, A, Rs, Rsh, alpha, Rsh2].
[[nodiscard]] inline double evaluateDiodeIV4(double voltage, const double* p, double temperature = 300.0) noexcept
{
    return sst::physics::seriesShuntDiodeCurrent(voltage,
        temperature, p[0], p[1], p[2], p[3]);
}

[[nodiscard]] inline double evaluateDiodeIV6(double voltage, const double* p, double temperature = 300.0) noexcept
{
    return sst::physics::nonlinearShuntDiodeCurrent(voltage,
        temperature, p[0], p[1], p[2], p[3], p[4], p[5]);
}
