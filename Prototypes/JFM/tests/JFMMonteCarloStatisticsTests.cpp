#include <gtest/gtest.h>

#include "Analysis/JFMMonteCarloStatistics.hpp"

namespace SST::Prototypes::JFM
{
	TEST(JFMMonteCarloStatisticsTests, ClassifiesDeltaChiSquaredSigmaBands)
	{
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(1.00, 1), 0);
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(4.00, 1), 1);
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(9.00, 1), 2);
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(9.01, 1), 3);
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(2.30, 2), 0);
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(6.17, 2), 1);
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(11.8, 2), 2);
		EXPECT_EQ(ClassifyJFMDeltaChiSquared(11.9, 2), 3);
	}

	TEST(JFMMonteCarloStatisticsTests, CalculatesKnownMeanAndCovariance)
	{
		JFMMonteCarloResult result;
		result.parameterNames = {"x", "y"};
		result.freeParameterCount = 2;
		for (const auto &parameters : std::vector<std::vector<double>>{{-1.0, -2.0}, {-1.0, 2.0}, {1.0, -2.0}, {1.0, 2.0}})
			result.samples.push_back({parameters, 0.0, 0.0, 1, true});

		const auto statistics = CalculateJFMMonteCarloScatterStatistics(result, 0, 1);
		ASSERT_TRUE(statistics.has_value());
		EXPECT_DOUBLE_EQ(statistics->meanX, 0.0);
		EXPECT_DOUBLE_EQ(statistics->meanY, 0.0);
		EXPECT_NEAR(statistics->covarianceXX, 4.0 / 3.0, 1.0e-12);
		EXPECT_NEAR(statistics->covarianceYY, 16.0 / 3.0, 1.0e-12);
		EXPECT_NEAR(statistics->covarianceXY, 0.0, 1.0e-12);
		EXPECT_EQ(statistics->points.size(), 4);
	}

	TEST(JFMMonteCarloStatisticsTests, UsesDeltaChiSquaredForScatterSigmaClass)
	{
		JFMMonteCarloResult result;
		result.parameterNames = {"x", "y"};
		result.freeParameterCount = 2;
		result.samples.push_back({{0.0, 0.0}, 2.0, 2.0, 1, true});
		result.samples.push_back({{1.0, 0.0}, 5.0, 5.0, 1, true});
		result.samples.push_back({{0.0, 1.0}, 10.0, 10.0, 1, true});
		result.samples.push_back({{1.0, 1.0}, 12.0, 12.0, 1, true});

		const auto statistics = CalculateJFMMonteCarloScatterStatistics(result, 0, 1);
		ASSERT_TRUE(statistics.has_value());
		ASSERT_EQ(statistics->points.size(), 4);
		EXPECT_EQ(statistics->points[0].sigmaClass, 0);
		EXPECT_EQ(statistics->points[1].sigmaClass, 1);
		EXPECT_EQ(statistics->points[2].sigmaClass, 2);
		EXPECT_EQ(statistics->points[3].sigmaClass, 3);
		EXPECT_DOUBLE_EQ(statistics->points[2].deltaChiSquared, 10.0);
	}
} // namespace SST::Prototypes::JFM
