#include <gtest/gtest.h>
#include <sst/physics/constants.hpp>

#include "Analysis/JFMArrheniusAnalysis.hpp"
#include "Analysis/JFMFitEngine.hpp"
#include <sst/math/lambert_w.hpp>
#include "solver/diode_model.hpp"
#include "solver/objective.hpp"

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <limits>
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
		constexpr double k = sst::physics::k_B_over_q;
		const double thermal = Parameters.at("A") * k * temperature;
		const double argument = Parameters.at("I0") * Parameters.at("Rs") / thermal * std::exp(voltage / thermal);
		const double diode = sst::math::lambertW0(argument) * thermal / Parameters.at("Rs");
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

	TEST(JFMFitEngineTests, LibraryAdaptersMatchCodataCurrents)
	{
		const std::array<const char *, 5> models = {
			"SD-0", "SD-Rs", "SD-Rsh", "SD-RsRsh", "SD-RsRsh-alpha-Rsh2"};
		// CODATA reference values, independent of the linked Lambert W backend.
		const std::array<double, 5> expected = {
			0.00014445377415953838, 0.0001433894211402049, 0.0001450037741595384,
			0.00014393913436136263, 0.00014394517805387295};
		for (std::size_t index = 0; index < models.size(); ++index)
		{
			SCOPED_TRACE(models[index]);
			EXPECT_NEAR(EvaluateJFMCurrent(models[index], 0.55, 300.0, Parameters), expected[index], expected[index] * 1.0e-12);
			EXPECT_NEAR(EvaluateJFMCurrent(std::string(models[index]) + "-I_ph", 0.55, 300.0, Parameters), expected[index] - 0.03, 3.0e-14);
		}
	}

	TEST(JFMFitEngineTests, ZeroSeriesUsesContinuousLimit)
	{
		auto parameters = Parameters;
		parameters["Rs"] = 0.0;
		for (const char *model : {"SD-Rs", "SD-RsRsh", "SD-RsRsh-alpha-Rsh2"})
			EXPECT_TRUE(std::isfinite(EvaluateJFMCurrent(model, 0.55, 300.0, parameters)));
		EXPECT_NEAR(EvaluateJFMCurrent("SD-Rs", .55,300.,parameters), .00014445377415953838,1e-16);
        EXPECT_NEAR(EvaluateJFMCurrent("SD-RsRsh", .55,300.,parameters), .0001450037741595384,1e-16);
        EXPECT_NEAR(EvaluateJFMCurrent("SD-RsRsh-alpha-Rsh2", .55,300.,parameters), .0001450098241595384,1e-16);
        const double array[] = {1.0e-10, 1.5, 0.0, 1.0e6, 2.0, 5.0e7};
		EXPECT_NEAR(evaluateDiodeIV4(0.55, array, 300.0), 0.0001450037741595384, 1.0e-16);
		EXPECT_NEAR(evaluateDiodeIV6(0.55, array, 300.0), 0.0001450098241595384, 1.0e-16);
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
		const double thermal = Parameters.at("A") * sst::physics::k_B_over_q * temperature;
		const double noSeries = Parameters.at("I0") * std::exp(voltage / thermal);
		const double argument = Parameters.at("I0") * Parameters.at("Rs") / thermal * std::exp(voltage / thermal);
		const double withSeries = sst::math::lambertW0(argument) * thermal / Parameters.at("Rs");

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

	TEST(JFMFitEngineTests, RegularFitStartsFromPrefitAndTuningFitStartsFromTunedValues)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-0");
		record.info.metadata["tuned.I0"] = "1e-6";
		JFMFitMonteCarloSettings settings;
		settings.fitMaxIterations = 1;
		settings.fitReducedChiSquaredTolerance = 1.0e-20;
		settings.monteCarloNoisePercent = 1.0;
		settings.parameters = {{"I0", {1.0e-15, 1.0e-5, true}}, {"A", {0.5, 3.0, false}}};

		auto regular = RunJFMSimplexFit(record, settings);
		auto tuning = RunJFMSimplexFit(record, settings, true);

		ASSERT_TRUE(regular) << regular.Error().technicalDetails;
		ASSERT_TRUE(tuning) << tuning.Error().technicalDetails;
		EXPECT_LT(regular->state.chiSquared, tuning->state.chiSquared);
		EXPECT_NEAR(regular->state.parameters.at("I0").value, Parameters.at("I0"), 1.0e-18);
	}

	TEST(JFMFitEngineTests, ArrheniusMonteCarloUncertaintyTransformsEachCorrelatedSample)
	{
		JFMMonteCarloResult result;
		result.parameterNames = {"A", "I0"};
		result.samples = {
			{{1.0, std::exp(-10.0)}, 0.0, 0.0, 0, true},
			{{2.0, std::exp(-5.0)}, 0.0, 0.0, 0, true}};

		const auto lnI0 = JFMArrheniusMonteCarloUncertainty(JFMArrheniusQuantity::LnI0, result);
		const auto aLnI0 = JFMArrheniusMonteCarloUncertainty(JFMArrheniusQuantity::ALnI0, result);

		ASSERT_TRUE(lnI0);
		ASSERT_TRUE(aLnI0);
		EXPECT_NEAR(*lnI0, std::sqrt(12.5), 1.0e-12);
		EXPECT_NEAR(*aLnI0, 0.0, 1.0e-12);
	}

	TEST(JFMFitEngineTests, ArrheniusCanReadTunedParameters)
	{
		JFMCharacteristicRecord record = MakeRecord("SD-0");
		record.info.metadata["tuned.A"] = "2.5";
		record.info.metadata["tuned.I0"] = "1e-8";

		const auto a = JFMArrheniusValue(JFMArrheniusQuantity::A, record.info, JFMArrheniusParameterSource::Tuned);
		const auto lnI0 = JFMArrheniusValue(JFMArrheniusQuantity::LnI0, record.info, JFMArrheniusParameterSource::Tuned);

		ASSERT_TRUE(a);
		ASSERT_TRUE(lnI0);
		EXPECT_DOUBLE_EQ(*a, 2.5);
		EXPECT_NEAR(*lnI0, std::log(1.0e-8), 1.0e-12);
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
			EXPECT_LE(sample.iterations, settings.monteCarloMaxIterations);
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

    TEST(JFMFitEngineTests, VoltageRangeMapsDescendingRealSweepWithoutReordering)
    {
        const auto fixture = std::filesystem::current_path() / "Prototypes/JFM/example-data/real-measurement/C002/IVT C002_Z035_02/IV_C002_Z035_02_300,0K_100mW.dat";
        std::ifstream input(fixture);
        ASSERT_TRUE(input.is_open()) << fixture;
        auto record = MakeRecord("SD-0");
        auto data = CreateRef<JFMCharacteristicData>();
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream row(line); double v, i, j;
            if (row >> v >> i >> j) { data->voltage.push_back(v); data->current.push_back(i); }
        }
        ASSERT_GT(data->voltage.size(), 100);
        const auto rawVoltage = data->voltage, rawCurrent = data->current;
        // Isolate range selection from the pending light-current objective decision.
        for (double &i : data->current) i = std::max(std::abs(i), 1e-20);
        record.data = data;
        record.info.prefit->fitRange = {39, 149, 0.0958904, 0.643379, "manual"};
        JFMFitState fit; fit.modelId = "SD-0"; fit.parameters = record.info.prefit->parameters;
        JFMFitMonteCarloSettings settings; settings.preferGpu = false; settings.monteCarloSamples = 1;
        settings.parameters = {{"I0", {1e-15, 1e-5, false}}, {"A", {0.5, 3., false}}};
        auto result = RunJFMMonteCarlo(record, *record.info.prefit, fit, settings);
        ASSERT_TRUE(result) << result.Error().technicalDetails;
        const auto [lo, hi] = std::minmax_element(result->voltage.begin(), result->voltage.end());
        EXPECT_NEAR(*lo, 0.0958904, 1e-8); EXPECT_NEAR(*hi, 0.643379, 1e-8);
        EXPECT_EQ(data->voltage, rawVoltage);
        data->current = rawCurrent;
        EXPECT_FALSE(RunJFMMonteCarlo(record, *record.info.prefit, fit, settings));
    }

    TEST(JFMFitEngineTests, BadRangesAndObjectiveDataReturnVisibleErrors)
    {
        auto record = MakeRecord("SD-0");
        JFMFitMonteCarloSettings settings;
        for (const auto bounds : {std::pair{0.8, 0.9}, std::pair{0.4, 0.2}, std::pair{0.3, 0.3}}) {
            record.info.prefit->fitRange.lowerVoltage = bounds.first;
            record.info.prefit->fitRange.upperVoltage = bounds.second;
            auto result = RunJFMSimplexFit(record, settings);
            ASSERT_FALSE(result); EXPECT_FALSE(result.Error().technicalDetails.empty());
        }
        for (double bad : {-1.0, 0.0, 1.0, std::numeric_limits<double>::quiet_NaN()}) {
            record = MakeRecord("SD-0"); auto data = CreateRef<JFMCharacteristicData>(*record.data);
            data->current[10] = bad; record.data = data;
            auto result = RunJFMSimplexFit(record, settings);
            ASSERT_FALSE(result); EXPECT_NE(result.Error().technicalDetails.find("domain"), std::string::npos);
        }
    }

    TEST(JFMFitEngineTests, FractionalAlphaOutsideRealDomainFailsLoudly)
    {
        auto record = MakeRecord("SD-RsRsh-alpha-Rsh2");
        record.info.prefit->parameters["alpha"].value = 2.3;
        auto data = CreateRef<JFMCharacteristicData>(*record.data);
        data->voltage[0] = -0.1; data->current[0] = 1e-4; record.data = data;
        record.info.prefit->fitRange.lowerVoltage = -0.1;
        JFMFitState fit; fit.modelId = record.info.prefit->modelId; fit.parameters = record.info.prefit->parameters;
        auto result = RunJFMMonteCarlo(record, *record.info.prefit, fit, {});
        ASSERT_FALSE(result); EXPECT_NE(result.Error().technicalDetails.find("domain"), std::string::npos);
    }

    TEST(JFMFitEngineTests, OneIterationAcceptedDoesNotMeanSolverConvergence)
    {
        auto record = MakeRecord("SD-0");
        JFMFitState fit; fit.modelId = "SD-0"; fit.parameters = record.info.prefit->parameters;
        JFMFitMonteCarloSettings settings; settings.preferGpu = false;
        settings.monteCarloSamples = 4; settings.monteCarloMaxIterations = 1;
        auto result = RunJFMMonteCarlo(record, *record.info.prefit, fit, settings);
        ASSERT_TRUE(result); EXPECT_GT(result->state.acceptedCount, 0); EXPECT_EQ(result->state.convergedCount, 0);
        for (const auto &sample : result->result.samples) {
            EXPECT_EQ(sample.accepted, sample.converged); EXPECT_FALSE(sample.solverConverged);
            EXPECT_EQ(sample.stopReason, sample.accepted ? "max_iter" : "non_finite");
        }
    }

    TEST(JFMFitEngineTests, ZeroNoiseHasNoConfidenceClassesOrArtificialVariance)
    {
        auto record = MakeRecord("SD-0");
        JFMFitState fit; fit.modelId = "SD-0"; fit.parameters = record.info.prefit->parameters;
        JFMFitMonteCarloSettings settings; settings.preferGpu = false;
        settings.monteCarloSamples = 3; settings.monteCarloNoisePercent = 0.; settings.monteCarloMaxIterations = 1;
        auto result = RunJFMMonteCarlo(record, *record.info.prefit, fit, settings);
        ASSERT_TRUE(result); EXPECT_FALSE(result->result.confidenceAvailable);
        EXPECT_FALSE(result->state.warnings.empty());
        for (double sigma : result->sigma) EXPECT_DOUBLE_EQ(sigma, 0.);
        for (const auto &sample : result->result.samples) EXPECT_DOUBLE_EQ(sample.chiSquared, 0.);
    }

    TEST(JFMFitEngineTests, RealSimplexConvergenceStopsBeforeIterationBudget)
    {
        auto record = MakeRecord("SD-0");
        JFMFitState fit; fit.modelId = "SD-0"; fit.parameters = record.info.prefit->parameters;
        JFMFitMonteCarloSettings settings; settings.preferGpu = false;
        settings.monteCarloSamples = 1; settings.monteCarloMaxIterations = 1000;
        settings.monteCarloNoisePercent = 0.; settings.monteCarloStartPerturbationPercent = 0.;
        settings.parameters = {{"I0", {1e-15,1e-5,false}}, {"A", {.5,3.,true}}};
        auto result = RunJFMMonteCarlo(record,*record.info.prefit,fit,settings);
        ASSERT_TRUE(result); ASSERT_EQ(result->result.samples.size(),1);
        const auto &sample = result->result.samples.front();
        EXPECT_TRUE(sample.accepted); EXPECT_TRUE(sample.solverConverged);
        EXPECT_EQ(sample.stopReason,"converged"); EXPECT_LT(sample.iterations,settings.monteCarloMaxIterations);
    }

    TEST(JFMFitEngineTests, ZeroAcceptedSamplesHaveFailureStatusAndMessage)
    {
        auto record = MakeRecord("SD-RsRsh-alpha-Rsh2-I_ph");
        auto data = CreateRef<JFMCharacteristicData>();
        for (int index=0; index<48; ++index) {
            const double voltage = .9 + .2*index/47.;
            data->voltage.push_back(voltage);
            data->current.push_back(EvaluateJFMCurrent(record.info.prefit->modelId,voltage,300.,Parameters));
        }
        record.data = data; record.info.prefit->fitRange = {0,48,.9,1.1,"test"};
        JFMFitState fit; fit.modelId = record.info.prefit->modelId; fit.parameters = record.info.prefit->parameters;
        record.info.prefit->parameters["I_ph"].value = 10.;
        JFMFitMonteCarloSettings settings; settings.preferGpu = false;
        settings.monteCarloSamples = 2; settings.monteCarloMaxIterations = 2;
        for (auto name : JFMModelParameterNames(fit.modelId)) settings.parameters[std::string(name)] = {1e-15,1e15,false};
        settings.parameters["I_ph"] = {0.,1e6,true};
        auto result = RunJFMMonteCarlo(record,*record.info.prefit,fit,settings);
        ASSERT_TRUE(result); EXPECT_EQ(result->state.acceptedCount,0);
        EXPECT_EQ(result->state.status,"failed"); EXPECT_FALSE(result->state.warnings.empty());
        for (const auto &sample : result->result.samples) EXPECT_EQ(sample.stopReason,"non_finite");
    }
} // namespace SST::Prototypes::JFM
