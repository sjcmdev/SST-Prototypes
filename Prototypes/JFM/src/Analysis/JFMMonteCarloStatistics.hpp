#pragma once

#include "Domain/JFMTypes.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace SST::Prototypes::JFM
{
    inline constexpr std::array<const char *, 5> JFMSigmaLabels = {"<= 1 sigma", "1-2 sigma", "2-3 sigma", ">3 sigma", "Unclassified (zero variance)"};
    inline constexpr std::array<const char *, 5> JFMSigmaTsvLabels = {"le_1_sigma", "1_2_sigma", "2_3_sigma", "gt_3_sigma", "unclassified"};

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
