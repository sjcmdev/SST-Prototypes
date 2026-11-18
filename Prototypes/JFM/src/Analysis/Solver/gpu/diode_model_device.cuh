#pragma once

#include "lambert_w_device.cuh"
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
    const double i0 = params[0];
    const double ideality = params[1];
    if (!(i0 > 0.0) || !(ideality > 0.0) || !(temperature > 0.0))
        return gpu_nan();
    const double thermal = ideality * sst::physics::k_B_over_q * temperature;
    double diode = 0.0;
    double internal_voltage = voltage;
    if (gpu_model_has_rs(model))
    {
        const double series = params[2];
        if (!(series >= 0.0))
            return gpu_nan();
        const double argument = (i0 * series / thermal) * exp(voltage / thermal);
        diode = LambertW0_d(argument) * thermal / series;
        internal_voltage = voltage - diode * series;
    }
    else
    {
        diode = i0 * exp(gpu_model_has_rsh(model)
            ? voltage / thermal
            : fmin(fmax(voltage / thermal, -745.0), 700.0));
    }

    double current = diode;
    if (gpu_model_has_rsh(model))
    {
        if (!(params[3] > 0.0))
            return gpu_nan();
        current += internal_voltage / params[3];
    }
    if (gpu_model_has_nonlinear(model))
    {
        const double alpha = params[4];
        const double rsh2 = params[5];
        if (!(alpha > 0.0) || !(rsh2 > 0.0))
            return gpu_nan();
        current += pow(internal_voltage, alpha) / rsh2;
    }
    if (gpu_model_has_photocurrent(model))
        current -= params[6];
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
