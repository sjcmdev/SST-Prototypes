#pragma once
#include "gpu/gpu_types.hpp"
#include <cstddef>

namespace lm::cpu {
enum class Dispatch { Auto, Scalar, Avx2 };
struct CurrentBlock {
    double value[4];
    double jacobian[7][4];
};
bool avx2Available() noexcept;
// Auto is fixed once per process; JFM_CPU_SCALAR=1 permits reproducible scalar runs.
Dispatch dispatch() noexcept;
void currents(GpuModelType model, const double* voltage, std::size_t count,
    const double* parameters, double temperature, double* output, Dispatch path = Dispatch::Auto);
double objective(GpuModelType model, const GpuParamLayout& layout, const double* parameters,
    const double* voltage, const double* measured, int count, double* h, double* g,
    bool precise, Dispatch path = Dispatch::Auto);
}
