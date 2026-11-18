#pragma once

#include <sst/physics/constants.hpp>

namespace SST::Prototypes::JFM
{
    // ponytail: switch to sst::physics CODATA in L2b
    inline constexpr double ModelBoltzmann = 8.6e-5;

    inline double LibraryModelTemperature(double temperature) noexcept
    {
        return ModelBoltzmann * temperature / sst::physics::k_B_over_q;
    }
}
