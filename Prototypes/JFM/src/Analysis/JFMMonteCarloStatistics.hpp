#pragma once

#include "Domain/JFMTypes.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace SST::Prototypes::JFM
{
	struct JFMMonteCarloScatterPoint
	{
		double x = 0.0;
		double y = 0.0;
		double mahalanobisSquared = 0.0;
		double deltaChiSquared = 0.0;
		std::uint8_t sigmaClass = 0;
	};

	struct JFMMonteCarloScatterStatistics
	{
		std::vector<JFMMonteCarloScatterPoint> points;
		double meanX = 0.0;
		double meanY = 0.0;
		double covarianceXX = 0.0;
		double covarianceXY = 0.0;
		double covarianceYY = 0.0;
		double eigenvalueMajor = 0.0;
		double eigenvalueMinor = 0.0;
		double eigenvectorAngle = 0.0;
	};

	[[nodiscard]] std::uint8_t ClassifyJFMDeltaChiSquared(double deltaChiSquared, int freeParameterCount) noexcept;
	[[nodiscard]] std::optional<JFMMonteCarloScatterStatistics> CalculateJFMMonteCarloScatterStatistics(
		const JFMMonteCarloResult &result,
		int xParameter,
		int yParameter);
} // namespace SST::Prototypes::JFM
