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
        constexpr std::array<std::array<double, 7>, 3> DeltaChiSquaredThresholds = {{
            {1.00, 2.30, 3.53, 4.72, 5.89, 7.04, 8.1762},
            {4.00, 6.17, 8.02, 9.70, 11.3, 12.8, 14.3371},
            {9.00, 11.8, 14.2, 16.3, 18.2, 20.1, 21.8466},
        }};
	}

	std::uint8_t ClassifyJFMDeltaChiSquared(double deltaChiSquared, int freeParameterCount) noexcept
	{
		if (!(deltaChiSquared >= 0.0) || !std::isfinite(deltaChiSquared))
			return 3;
		const int column = std::clamp(freeParameterCount, 1, 7) - 1;
		if (deltaChiSquared <= DeltaChiSquaredThresholds[0][column])
			return 0;
		if (deltaChiSquared <= DeltaChiSquaredThresholds[1][column])
			return 1;
		if (deltaChiSquared <= DeltaChiSquaredThresholds[2][column])
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
			statistics.points.push_back({x, y, 0.0, sample.chiSquared, result.confidenceAvailable ? ClassifyJFMDeltaChiSquared(sample.chiSquared, result.freeParameterCount) : std::uint8_t{4}});
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
		statistics.eigenvalueMinor = statistics.eigenvalueMajor > 0.0
            ? std::max(0.0, (statistics.covarianceXX * statistics.covarianceYY - statistics.covarianceXY * statistics.covarianceXY) / statistics.eigenvalueMajor) : 0.0;
		statistics.eigenvectorAngle = 0.5 * std::atan2(2.0 * statistics.covarianceXY,
			statistics.covarianceXX - statistics.covarianceYY);

        const double sx = std::sqrt(statistics.covarianceXX);
        const double sy = std::sqrt(statistics.covarianceYY);
        const double correlation = sx > 0.0 && sy > 0.0
            ? std::clamp(statistics.covarianceXY / sx / sy, -1.0, 1.0) : 0.0;
        for (JFMMonteCarloScatterPoint &point : statistics.points)
        {
            const double x = sx > 0.0 ? (point.x - statistics.meanX) / sx : 0.0;
            const double y = sy > 0.0 ? (point.y - statistics.meanY) / sy : 0.0;
            // Pseudoinverse in standardized axes excludes constant and collinear dimensions.
            const double plus = 1.0 + correlation, minus = 1.0 - correlation;
            point.mahalanobisSquared = (plus > 1e-12 ? (x + y) * (x + y) / (2.0 * plus) : 0.0)
                + (minus > 1e-12 ? (x - y) * (x - y) / (2.0 * minus) : 0.0);
        }
		return statistics;
	}
} // namespace SST::Prototypes::JFM
