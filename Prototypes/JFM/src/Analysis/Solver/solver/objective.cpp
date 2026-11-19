// src/solver/objective.cpp
#include "objective.hpp"
#include "lm_core.hpp"

#include <cmath>
#include <limits>

double computeChiSquared(
	const double *params,
	const double *V_data,
	const double *I_meas,
	const double *sigma,
	int N_points,
	std::function<double(double, const double *)> model_fn) noexcept
{
	constexpr double kInf = std::numeric_limits<double>::infinity();
	(void)sigma;

	double chi2 = 0.0;
	for (int i = 0; i < N_points; ++i)
	{
		if (!(I_meas[i] > 0.0) || !std::isfinite(I_meas[i]))
			return kInf;

		const double I_model = model_fn(V_data[i], params);
		if (!(I_model > 0.0) || !std::isfinite(I_model))
			return kInf;

		const double residual = lm::relativeLogResidual(I_model, I_meas[i]);
		if (!std::isfinite(residual))
			return kInf;
		chi2 += residual * residual;
	}
	return chi2;
}
