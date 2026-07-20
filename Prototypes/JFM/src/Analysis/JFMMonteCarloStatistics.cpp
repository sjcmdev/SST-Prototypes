#include "Core/dspch.hpp"

#include "Analysis/JFMMonteCarloStatistics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace SST::Prototypes::JFM
{
	namespace
	{
		constexpr std::array<std::array<double, 6>, 6> DeltaChiSquaredThresholds = {{
			{1.00, 2.30, 3.53, 4.72, 5.89, 7.04},
			{2.71, 4.61, 6.25, 7.78, 9.24, 10.6},
			{4.00, 6.17, 8.02, 9.70, 11.3, 12.8},
			{6.63, 9.21, 11.3, 13.3, 15.1, 16.8},
			{9.00, 11.8, 14.2, 16.3, 18.2, 20.1},
			{15.1, 18.4, 21.1, 23.5, 25.7, 27.8},
		}};
	}

	std::uint8_t ClassifyJFMDeltaChiSquared(double deltaChiSquared, int freeParameterCount) noexcept
	{
		if (!(deltaChiSquared >= 0.0) || !std::isfinite(deltaChiSquared))
			return 3;
		const int column = std::clamp(freeParameterCount, 1, 6) - 1;
		if (deltaChiSquared <= DeltaChiSquaredThresholds[0][column])
			return 0;
		if (deltaChiSquared <= DeltaChiSquaredThresholds[2][column])
			return 1;
		if (deltaChiSquared <= DeltaChiSquaredThresholds[4][column])
			return 2;
		return 3;
	}

	std::optional<JFMMonteCarloScatterStatistics> CalculateJFMMonteCarloScatterStatistics(
		const JFMMonteCarloResult &result,
		int xParameter,
		int yParameter)
	{
		if (xParameter < 0 || yParameter < 0 || xParameter == yParameter
			|| xParameter >= static_cast<int>(result.parameterNames.size())
			|| yParameter >= static_cast<int>(result.parameterNames.size()))
			return std::nullopt;

		JFMMonteCarloScatterStatistics statistics;
		statistics.points.reserve(result.samples.size());
		for (const JFMMonteCarloSample &sample : result.samples)
		{
			if (!sample.converged
				|| xParameter >= static_cast<int>(sample.parameters.size())
				|| yParameter >= static_cast<int>(sample.parameters.size()))
				continue;
			const double x = sample.parameters[xParameter];
			const double y = sample.parameters[yParameter];
			if (!std::isfinite(x) || !std::isfinite(y))
				continue;
			statistics.points.push_back({x, y, 0.0, sample.chiSquared, ClassifyJFMDeltaChiSquared(sample.chiSquared, result.freeParameterCount)});
			statistics.meanX += x;
			statistics.meanY += y;
		}
		if (statistics.points.size() < 3)
			return std::nullopt;

		const double count = static_cast<double>(statistics.points.size());
		statistics.meanX /= count;
		statistics.meanY /= count;
		for (const JFMMonteCarloScatterPoint &point : statistics.points)
		{
			const double dx = point.x - statistics.meanX;
			const double dy = point.y - statistics.meanY;
			statistics.covarianceXX += dx * dx;
			statistics.covarianceXY += dx * dy;
			statistics.covarianceYY += dy * dy;
		}
		const double denominator = count - 1.0;
		statistics.covarianceXX /= denominator;
		statistics.covarianceXY /= denominator;
		statistics.covarianceYY /= denominator;

		const double trace = statistics.covarianceXX + statistics.covarianceYY;
		const double root = std::sqrt(std::max(0.0,
			(statistics.covarianceXX - statistics.covarianceYY) * (statistics.covarianceXX - statistics.covarianceYY)
			+ 4.0 * statistics.covarianceXY * statistics.covarianceXY));
		statistics.eigenvalueMajor = std::max(0.0, 0.5 * (trace + root));
		statistics.eigenvalueMinor = std::max(0.0, 0.5 * (trace - root));
		statistics.eigenvectorAngle = 0.5 * std::atan2(2.0 * statistics.covarianceXY,
			statistics.covarianceXX - statistics.covarianceYY);

		const double regularization = std::max(
			std::abs(trace) * 1.0e-12,
			std::numeric_limits<double>::epsilon() * std::numeric_limits<double>::epsilon());
		const double covarianceXX = statistics.covarianceXX + regularization;
		const double covarianceYY = statistics.covarianceYY + regularization;
		const double determinant = covarianceXX * covarianceYY - statistics.covarianceXY * statistics.covarianceXY;
		if (!(determinant > 0.0) || !std::isfinite(determinant))
			return std::nullopt;
		for (JFMMonteCarloScatterPoint &point : statistics.points)
		{
			const double dx = point.x - statistics.meanX;
			const double dy = point.y - statistics.meanY;
			point.mahalanobisSquared = std::max(0.0,
				(covarianceYY * dx * dx - 2.0 * statistics.covarianceXY * dx * dy + covarianceXX * dy * dy) / determinant);
		}
		return statistics;
	}
} // namespace SST::Prototypes::JFM
