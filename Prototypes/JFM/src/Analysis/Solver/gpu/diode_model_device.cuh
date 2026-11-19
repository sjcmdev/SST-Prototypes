#pragma once

#include "lambert_w_device.cuh"
#include "solver/lm_core.hpp"
#include <sst/physics/constants.hpp>

__device__ __forceinline__ double gpu_nan()
{
    return __longlong_as_double(0x7FF8000000000000ULL);
}

__device__ __forceinline__ bool gpu_model_has_rs(GpuModelType model)
{
    return model == GpuModelType::SDRs || model == GpuModelType::SDRsRsh ||
        model == GpuModelType::SDRsIph || model == GpuModelType::SDRsRshIph ||
        model == GpuModelType::SDRsRshAlphaRsh2 || model == GpuModelType::SDRsRshAlphaRsh2Iph;
}

__device__ __forceinline__ bool gpu_model_has_rsh(GpuModelType model)
{
    return model == GpuModelType::SDRsh || model == GpuModelType::SDRsRsh ||
        model == GpuModelType::SDRshIph || model == GpuModelType::SDRsRshIph ||
        model == GpuModelType::SDRsRshAlphaRsh2 || model == GpuModelType::SDRsRshAlphaRsh2Iph;
}

__device__ __forceinline__ bool gpu_model_has_nonlinear(GpuModelType model)
{
    return model == GpuModelType::SDRsRshAlphaRsh2 || model == GpuModelType::SDRsRshAlphaRsh2Iph;
}

__device__ __forceinline__ bool gpu_model_has_photocurrent(GpuModelType model)
{
    return model == GpuModelType::SD0Iph || model == GpuModelType::SDRsIph ||
        model == GpuModelType::SDRshIph || model == GpuModelType::SDRsRshIph ||
        model == GpuModelType::SDRsRshAlphaRsh2Iph;
}

__device__ __forceinline__ double evaluateJFMCurrent_d(GpuModelType model, double voltage, const double* params, double temperature)
{
    const double current = lm::current(model, voltage, params, temperature, nullptr, true);
    return isfinite(current) ? current : gpu_nan();
}

__device__ __forceinline__ double evaluateDiodeIV4_d(double voltage, const double* params, double temperature)
{
    return evaluateJFMCurrent_d(GpuModelType::SDRsRsh, voltage, params, temperature);
}

__device__ __forceinline__ double evaluateDiodeIV6_d(double voltage, const double* params, double temperature)
{
    return evaluateJFMCurrent_d(GpuModelType::SDRsRshAlphaRsh2, voltage, params, temperature);
}
