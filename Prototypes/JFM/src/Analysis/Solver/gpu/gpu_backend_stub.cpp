#include "gpu_backend.hpp"

#if !JFM_CUDA_AVAILABLE

namespace
{
constexpr const char *Unavailable = "CUDA backend is unavailable in this build";
}

GpuDeviceStatus gpuQueryDevice()
{
    return {false, {}, 0, 0, Unavailable};
}

GpuLambertWValidationResult gpuValidateLambertW()
{
    GpuLambertWValidationResult result;
    result.message = Unavailable;
    return result;
}

GpuNumericValidationResult gpuValidateDiodeCurrent()
{
    GpuNumericValidationResult result;
    result.message = Unavailable;
    return result;
}

GpuNumericValidationResult gpuValidateNoise() { return gpuValidateDiodeCurrent(); }
GpuNumericValidationResult gpuValidateSimplexOneStep() { return gpuValidateDiodeCurrent(); }
GpuNumericValidationResult gpuValidateSimplexFull() { return gpuValidateDiodeCurrent(); }

GpuSimplexParityReport gpuValidateSimplexParityTrace()
{
    GpuSimplexParityReport result;
    result.message = Unavailable;
    return result;
}

GpuMonteCarloOutput gpuRunMonteCarlo(const GpuMonteCarloRequest &)
{
    GpuMonteCarloOutput result;
    result.message = Unavailable;
    return result;
}

GpuMonteCarloBatchPlan gpuPlanMonteCarlo(const GpuMonteCarloRequest &)
{
    GpuMonteCarloBatchPlan plan;
    plan.message = Unavailable;
    return plan;
}

#endif
