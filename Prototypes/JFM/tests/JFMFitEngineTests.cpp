#include <gtest/gtest.h>

#include "Analysis/JFMFitEngine.hpp"
#include "lambertw/LambertW.hpp"
#include "solver/diode_model.hpp"
#include "solver/objective.hpp"

#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

namespace SST::Prototypes::JFM
{
	namespace
	{
		const std::unordered_map<std::string, double> Parameters = {
			{"I0", 1.0e-10}, {"A", 1.5}, {"Rs", 2.0}, {"Rsh", 1.0e6},
			{"alpha", 2.0}, {"Rsh2", 5.0e7}, {"I_ph", 0.03}};

		JFMCharacteristicRecord MakeRecord(std::string model)
		{
			JFMCharacteristicRecord record;
			record.info.name = "synthetic";
			record.info.temperatureK = 300.0;
			record.info.prefit = JFMPrefitState{};
			record.info.prefit->modelId = std::move(model);
			record.info.prefit->status = "completed";
			record.info.prefit->fitRange = {0, 48, 0.02, 0.72, "test"};
			for (std::string_view name : JFMModelParameterNames(record.info.prefit->modelId))
				record.info.prefit->parameters[std::string(name)] = JFMParameterEstimate{Parameters.at(std::string(name)), std::nullopt, "test", {}};
			auto data = CreateRef<JFMCharacteristicData>();
			for (int index = 0; index < 48; ++index)
			{
				const double voltage = 0.02 + 0.70 * static_cast<double>(index) / 47.0;
				data->voltage.push_back(voltage);
				data->current.push_back(EvaluateJFMCurrent(record.info.prefit->modelId, voltage, 300.0, Parameters));
			}
			record.data = std::move(data);
			return record;
		}
	} // namespace

	TEST(JFMFitEngineTests, EvaluatesEveryRegisteredModel)
	{
		const std::array<const char *, 10> models = {
			"SD-0", "SD-Rs", "SD-Rsh", "SD-RsRsh",
			"SD-0-I_ph", "SD-Rs-I_ph", "SD-Rsh-I_ph", "SD-RsRsh-I_ph",
			"SD-RsRsh-alpha-Rsh2", "SD-RsRsh-alpha-Rsh2-I_ph"};
		for (const char *model : models)
		{
			SCOPED_TRACE(model);
			EXPECT_FALSE(JFMModelParameterNames(model).empty());
			EXPECT_TRUE(std::isfinite(EvaluateJFMCurrent(model, 0.55, 300.0, Parameters)));
		}
	}

	TEST(JFMFitEngineTests, CurrentModelsMatchCanonicalLambertWEquations)
	{
		constexpr double voltage = 0.55;
		constexpr double temperature = 300.0;
		constexpr double k = 8.6e-5;
		const double thermal = Parameters.at("A") * k * temperature;
		const double argument = Parameters.at("I0") * Parameters.at("Rs") / thermal * std::exp(voltage / thermal);
		const double diode = utl::LambertW<0>(argument) * thermal / Parameters.at("Rs");
		const double internalVoltage = voltage - diode * Parameters.at("Rs");
		const double expected4 = diode + internalVoltage / Parameters.at("Rsh");
		const double expected6 = expected4 + std::pow(internalVoltage, Parameters.at("alpha")) / Parameters.at("Rsh2");
		const double parameters4[] = {Parameters.at("I0"), Parameters.at("A"), Parameters.at("Rs"), Parameters.at("Rsh")};
		const double parameters6[] = {Parameters.at("I0"), Parameters.at("A"), Parameters.at("Rs"), Parameters.at("Rsh"), Parameters.at("alpha"), Parameters.at("Rsh2")};

		EXPECT_NEAR(EvaluateJFMCurrent("SD-RsRsh", voltage, temperature, Parameters), expected4, std::abs(expected4) * 1.0e-12);
		EXPECT_NEAR(EvaluateJFMCurrent("SD-RsRsh-alpha-Rsh2", voltage, temperature, Parameters), expected6, std::abs(expected6) * 1.0e-12);
		EXPECT_NEAR(evaluateDiodeIV4(voltage, parameters4, temperature), expected4, std::abs(expected4) * 1.0e-12);
		EXPECT_NEAR(evaluateDiodeIV6(voltage, parameters6, temperature), expected6, std::abs(expected6) * 1.0e-12);
	}

	TEST(JFMFitEngineTests, ChiSquaredUsesSimplexLogRatioResiduals)
	{
		const double voltage[] = {0.0, 1.0};
		const double measured[] = {10.0, 20.0};
		const double sigma[] = {2.0, 4.0};
		const double params[] = {1.0};

		const double chiSquared = computeChiSquared(params, voltage, measured, sigma, 2, [](double v, const double *) {
			return v == 0.0 ? 8.0 : 24.0;
		});

		const double expected = std::pow(1.0 - std::log(8.0) / std::log(10.0), 2.0)
			+ std::pow(1.0 - std::log(24.0) / std::log(20.0), 2.0);
		EXPECT_DOUBLE_EQ(chiSquared, expected);
	}

	TEST(JFMFitEngineTests, MissingResistanceTermsAreRemoved)
	{
		constexpr double voltage = 0.2;
		constexpr double temperature = 300.0;
		const double thermal = Parameters.at("A") * 8.6e-5 * temperature;
		const double noSeries = Parameters.at("I0") * std::exp(voltage / thermal);
		const double argument = Parameters.at("I0") * Parameters.at("Rs") / thermal * std::exp(voltage / thermal);
		const double withSeries = utl::LambertW<0>(argument) * thermal / Parameters.at("Rs");

		EXPECT_NEAR(EvaluateJFMCurrent("SD-0", voltage, temperature, Parameters), noSeries, std::abs(noSeries) * 1.0e-12);
		EXPECT_NEAR(EvaluateJFMCurrent("SD-Rsh", voltage, temperature, Parameters), noSeries + voltage / Parameters.at("Rsh"), std::abs(noSeries) * 1.0e-12);
		EXPECT_NEAR(EvaluateJFMCurrent("SD-Rs", voltage, temperature, Parameters), withSeries, std::abs(withSeries) * 1.0e-12);
	}

	TEST(JFMFitEngineTests, CpuSimplexUsesCompletedPrefitStart)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-0");
		JFMFitMonteCarloSettings settings;
		settings.fitMaxIterations = 100;
		settings.fitReducedChiSquaredTolerance = 1.0e-12;
		settings.monteCarloNoisePercent = 1.0;
		settings.parameters = {{"I0", {1.0e-15, 1.0e-5, true}}, {"A", {0.5, 3.0, true}}};

		auto result = RunJFMSimplexFit(record, settings);
		ASSERT_TRUE(result) << result.Error().technicalDetails;
		EXPECT_TRUE(result->state.converged);
		EXPECT_EQ(result->state.backend, "cpu-simplex");
		EXPECT_NEAR(result->state.parameters.at("I0").value, Parameters.at("I0"), 1.0e-18);
		EXPECT_NEAR(result->state.parameters.at("A").value, Parameters.at("A"), 1.0e-9);
		EXPECT_EQ(result->curve.current.size(), record.data->current.size());
	}

	TEST(JFMFitEngineTests, CpuSimplexDoesNotClampPrefitStartOutsideConfiguredBounds)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-RsRsh-alpha-Rsh2");
		JFMFitMonteCarloSettings settings;
		settings.fitMaxIterations = 100;
		settings.fitReducedChiSquaredTolerance = 1.0e-12;
		settings.monteCarloNoisePercent = 1.0;
		settings.parameters["Rsh"] = {1.0, 1.0e4, true};

		auto result = RunJFMSimplexFit(record, settings);
		ASSERT_TRUE(result) << result.Error().technicalDetails;
		EXPECT_LT(result->state.chiSquared, 1.0e-18);
		EXPECT_NEAR(result->state.parameters.at("Rsh").value, Parameters.at("Rsh"), Parameters.at("Rsh") * 1.0e-9);
	}

	TEST(JFMFitEngineTests, CpuSimplexDoesNotMakeOutOfRangePrefitValueABoundary)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-0");
		JFMFitMonteCarloSettings settings;
		settings.fitMaxIterations = 100;
		settings.fitReducedChiSquaredTolerance = 1.0e-12;
		settings.monteCarloNoisePercent = 1.0;
		settings.parameters = {{"I0", {1.0e-9, 1.0e-5, true}}, {"A", {0.5, 3.0, true}}};

		auto result = RunJFMSimplexFit(record, settings);
		ASSERT_TRUE(result) << result.Error().technicalDetails;
		EXPECT_LT(result->state.chiSquared, 1.0e-18);
		EXPECT_NEAR(result->state.parameters.at("I0").value, Parameters.at("I0"), Parameters.at("I0") * 1.0e-9);
	}

	TEST(JFMFitEngineTests, CpuMonteCarloKeepsUncheckedParametersAtFitValues)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-0");
		JFMFitState fit;
		fit.modelId = "SD-0";
		fit.status = "completed";
		fit.parameters = record.info.prefit->parameters;
		JFMFitMonteCarloSettings settings;
		settings.preferGpu = false;
		settings.monteCarloSamples = 4;
		settings.monteCarloMaxIterations = 20;
		settings.monteCarloNoisePercent = 1.0;
		settings.monteCarloStartPerturbationPercent = 5.0;
		settings.parameters = {{"I0", {1.0e-15, 1.0e-5, false}}, {"A", {0.5, 3.0, true}}};

		auto result = RunJFMMonteCarlo(record, *record.info.prefit, fit, settings);
		ASSERT_TRUE(result) << result.Error().technicalDetails;
		ASSERT_EQ(result->result.samples.size(), 4);
		EXPECT_EQ(result->idealCurrent.size(), record.data->current.size());
		EXPECT_EQ(result->noisyCurrent.size(), 4 * record.data->current.size());
		bool sawNonZeroDeltaChiSquared = false;
		for (const JFMMonteCarloSample &sample : result->result.samples)
		{
			ASSERT_EQ(sample.parameters.size(), 2);
			EXPECT_DOUBLE_EQ(sample.parameters[0], Parameters.at("I0"));
			EXPECT_DOUBLE_EQ(sample.reducedChiSquared, sample.chiSquared);
			EXPECT_EQ(sample.iterations, settings.monteCarloMaxIterations);
			sawNonZeroDeltaChiSquared = sawNonZeroDeltaChiSquared || sample.chiSquared > 0.0;
		}
		EXPECT_TRUE(sawNonZeroDeltaChiSquared);
	}

	TEST(JFMFitEngineTests, CpuMonteCarloWithZeroNoisePreservesIdealCurve)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-0");
		JFMFitState fit;
		fit.modelId = "SD-0";
		fit.status = "completed";
		fit.parameters = record.info.prefit->parameters;
		JFMFitMonteCarloSettings settings;
		settings.preferGpu = false;
		settings.monteCarloSamples = 2;
		settings.monteCarloNoisePercent = 0.0;
		settings.parameters = {{"I0", {1.0e-15, 1.0e-5, false}}, {"A", {0.5, 3.0, false}}};

		auto result = RunJFMMonteCarlo(record, *record.info.prefit, fit, settings);
		ASSERT_TRUE(result) << result.Error().technicalDetails;
		ASSERT_EQ(result->noisyCurrent.size(), result->idealCurrent.size() * 2);
		for (std::size_t sample = 0; sample < 2; ++sample)
			for (std::size_t point = 0; point < result->idealCurrent.size(); ++point)
				EXPECT_DOUBLE_EQ(result->noisyCurrent[sample * result->idealCurrent.size() + point], result->idealCurrent[point]);
	}

	TEST(JFMFitEngineTests, CpuMonteCarloReportsFinalErrorAgainstCleanCurve)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-0");
		JFMFitState fit;
		fit.modelId = "SD-0";
		fit.status = "completed";
		fit.parameters = record.info.prefit->parameters;
		JFMFitMonteCarloSettings settings;
		settings.preferGpu = false;
		settings.monteCarloSamples = 1;
		settings.monteCarloNoisePercent = 5.0;
		settings.parameters = {{"I0", {1.0e-15, 1.0e-5, false}}, {"A", {0.5, 3.0, false}}};

		auto result = RunJFMMonteCarlo(record, *record.info.prefit, fit, settings);
		ASSERT_TRUE(result) << result.Error().technicalDetails;
		ASSERT_EQ(result->result.samples.size(), 1);
		EXPECT_DOUBLE_EQ(result->result.samples.front().chiSquared, 0.0);
		EXPECT_DOUBLE_EQ(result->result.samples.front().reducedChiSquared, 0.0);
	}
} // namespace SST::Prototypes::JFM
