// src/solver/objective.hpp
#pragma once

#include <functional>
#include "gpu/gpu_types.hpp"

// Simplex objective:
// chi2(p) = sum_i (1 - log(I_model(V_i, p)) / log(I_meas_i))^2
[[nodiscard]] double computeChiSquared(
	const double *params,
	const double *V_data,
	const double *I_meas,
	const double *sigma,
	int N_points,
	std::function<double(double, const double *)> model_fn,
    const GpuParamLayout &policy = {}) noexcept;

[[nodiscard]] inline double computeDeltaChiSquared(
	double chi2,
	double chi2_min) noexcept
{
	return chi2 - chi2_min;
}
