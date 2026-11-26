#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include <gtest/gtest.h>
#include <sst/physics/constants.hpp>

#include "Analysis/JFMArrheniusAnalysis.hpp"
#include "Analysis/JFMFitEngine.hpp"
#include <sst/math/lambert_w.hpp>
#include "solver/diode_model.hpp"
#include "solver/objective.hpp"
#include "solver/fit_solver.hpp"
#include "IO/JFMFitJobs.hpp"
#include "IO/JFMPythonBindings.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "IO/JFMIORegistration.hpp"
#include "IO/JFMProjectYamlSerializer.hpp"
#include "Events/JFMEvents.hpp"

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <limits>
#include <cmath>
#include <string>
#include <unordered_map>
#include <chrono>
#include <thread>

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

    TEST(JFMFitEngineTests, MonteCarloJobsReportFailuresAndRetainCurvesOnCancel)
    {
        for (int outcome=0; outcome<5; ++outcome) {
            SCOPED_TRACE(outcome);
            auto bus=CreateRef<EventBus>(); auto store=CreateRef<JFMWorkspaceStore>();
            IOLayer io(bus); RegisterJFMIOHandlers(io,{bus,store}); ProgressTracker tracker(bus);
            auto record=MakeRecord("SD-0");
            JFMFitState fit; fit.modelId="SD-0"; fit.status="completed"; fit.parameters=record.info.prefit->parameters;
            record.info.fit=fit;
            const JFMCharacteristicRef ref{store->GetActiveProjectHandle(),store->AddCharacteristicRecords(store->GetActiveProjectHandle(),{record}).front()};
            auto retained=CreateRef<JFMMonteCarloResult>(); retained->parameterNames={"I0","A"};
            retained->voltage=record.data->voltage; retained->idealCurrent=record.data->current;
            retained->noiseSeed=42; retained->noiseAmplitude.resize(retained->voltage.size(),0.0);
            JFMMonteCarloSample sample; sample.parameters={1e-10,1.5}; sample.accepted=sample.converged=true;
            retained->samples.resize(7,sample);
            const Ref<const JFMMonteCarloResult> previous=std::move(retained);
            JFMMonteCarloState state; state.modelId="SD-0"; state.sampleCount=7; state.acceptedCount=7;
            ASSERT_TRUE(store->ApplyMonteCarloResult(ref,store->BeginMonteCarlo(ref),state,previous));
            const auto generation=store->BeginMonteCarlo(ref);
            EXPECT_EQ(store->GetMonteCarloResult(ref),previous);
            EXPECT_EQ(store->GetMonteCarloState(ref)->acceptedCount,7);
            JFMFitMonteCarloSettings settings; settings.preferGpu=false; settings.saveSamples=false;
            settings.monteCarloSamples=outcome==1?JFMMonteCarloSampleLimit("SD-0")+1:outcome==3?100000:2;
            settings.monteCarloMaxIterations=10;
            if(outcome==2)record.data.reset();
            auto queue=CreateRef<JFMMonteCarloWorkQueue>();
            auto output=std::filesystem::temp_directory_path()/"jfmfix1-mc";
            if(outcome==4) {
                std::filesystem::create_directories(output); output/="output-file";
                std::ofstream file(output); file<<"Not a directory";
            }
            ASSERT_TRUE(queue->Enqueue({{ref,generation,record,*record.info.prefit,fit,settings,Path::FromResolved(output)}}));
            JFMCharacteristicRef validRef;
            if(outcome==1) {
                validRef={ref.project,store->AddCharacteristicRecords(ref.project,{record}).front()};
                auto validSettings=settings; validSettings.monteCarloSamples=2;
                EXPECT_FALSE(queue->Enqueue({{validRef,store->BeginMonteCarlo(validRef),record,*record.info.prefit,fit,validSettings,Path::FromResolved(std::filesystem::temp_directory_path()/"jfmfix1-mc")}}));
            }
            auto job=CreateRef<JFMMonteCarloJob>(bus,store,queue); JobSystem jobs(bus,1);
            const auto id=jobs.Submit(job);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(std::chrono::steady_clock::now()<deadline) {
                const auto status=jobs.GetJob(id); ASSERT_TRUE(status);
                if(outcome==3 && !status->currentStage.empty()) (void)jobs.RequestCancel(id);
                if(status->status!=JobStatus::Queued && status->status!=JobStatus::Running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            jobs.Shutdown(); bus->ProcessQueue();
            const auto status=jobs.GetJob(id); ASSERT_TRUE(status);
            const JobStatus expected=outcome==0?JobStatus::Completed:outcome==3?JobStatus::Cancelled:JobStatus::Failed;
            EXPECT_EQ(status->status,expected);
            const auto progress=tracker.GetSnapshot(id); ASSERT_TRUE(progress); EXPECT_EQ(progress->status,expected);
            if(outcome==1) {
                EXPECT_NE(store->GetMonteCarloResult(validRef),nullptr);
                EXPECT_EQ(store->GetMonteCarloState(validRef)->status,"completed");
            }
            if(outcome==0) {EXPECT_NE(store->GetMonteCarloResult(ref),previous); EXPECT_EQ(store->GetMonteCarloState(ref)->status,"completed");}
            else {
                EXPECT_EQ(store->GetMonteCarloResult(ref),previous);
                EXPECT_EQ(store->GetMonteCarloResult(ref)->idealCurrent,previous->idealCurrent);
                EXPECT_EQ(JFMMonteCarloNoisyCurve(*store->GetMonteCarloResult(ref),0),previous->idealCurrent);
                EXPECT_EQ(store->GetMonteCarloState(ref)->status,outcome==3?"cancelled":"failed");
                EXPECT_FALSE(store->GetMonteCarloState(ref)->warnings.empty());
                EXPECT_NE(store->GetStatusMessage().find(outcome==3?"cancelled":"failed"),std::string::npos);
                if(outcome!=3) {
                    EXPECT_EQ(progress->errorSummary,status->errorMessage);
                    EXPECT_EQ(progress->currentMessage,status->errorMessage);
                }
                if(outcome==4) {
                    EXPECT_NE(status->errorMessage.find("could not be saved"),std::string::npos);
                    std::filesystem::remove(output);
                }
                const auto saved=JFMProjectYamlSerializer::Serialize(store->GetProjectSnapshot()); ASSERT_TRUE(saved);
                const auto loaded=JFMProjectYamlSerializer::Deserialize(*saved); ASSERT_TRUE(loaded);
                EXPECT_EQ(loaded->characteristics.front().monteCarlo->status,outcome==3?"cancelled":"failed");
                EXPECT_EQ(loaded->characteristics.front().monteCarlo->acceptedCount,7);
                EXPECT_FALSE(store->ApplyMonteCarloResult(ref,generation,state,previous));
            }
            EXPECT_TRUE(queue->Enqueue({{ref,store->BeginMonteCarlo(ref),record,*record.info.prefit,fit,settings,{}}}));
            queue->Reset();
        }
    }

    TEST(JFMFitEngineTests, MonteCarloMemoryLimitMatchesRetainedSampleBudget)
    {
        for(const char* model:{"SD-0","SD-RsRsh","SD-RsRsh-alpha-Rsh2-I_ph"}) {
            const auto bytes=sizeof(JFMMonteCarloSample)+JFMModelParameterNames(model).size()*sizeof(double)+128;
            const auto limit=JFMMonteCarloSampleLimit(model);
            EXPECT_LE(limit*bytes,64u*1024*1024); EXPECT_GT((limit+1)*bytes,64u*1024*1024);
        }
        EXPECT_EQ(JFMMonteCarloSampleLimit("unknown"),0);
    }

    TEST(JFMFitEngineTests, MonteCarloRejectedBeforeSubmissionPreservesPreviousResult)
    {
        auto bus=CreateRef<EventBus>(); auto store=CreateRef<JFMWorkspaceStore>();
        auto record=MakeRecord("SD-0"); record.info.fit=JFMFitState{};
        record.info.fit->modelId="SD-0"; record.info.fit->status="completed";
        const JFMCharacteristicRef ref{store->GetActiveProjectHandle(),store->AddCharacteristicRecords(store->GetActiveProjectHandle(),{record}).front()};
        JFMMonteCarloState state; state.modelId="SD-0";
        const auto previous=CreateRef<const JFMMonteCarloResult>();
        ASSERT_TRUE(store->ApplyMonteCarloResult(ref,store->BeginMonteCarlo(ref),state,previous));
        auto settings=CreateRef<JFMUserSettingsStore>(Path::FromResolved(std::filesystem::temp_directory_path()/"jfmfix1-settings"));
        auto options=settings->GetSnapshot().fitMonteCarlo; options.monteCarloSamples=1000000; settings->SetFitMonteCarloSettings(options);
        int submissions=0; auto submitted=bus->Subscribe<JobSubmitRequested>([&](const auto&){++submissions;});
        IOLayer io(bus); RegisterJFMIOHandlers(io,{bus,store,{},settings});
        bus->PublishNew<MonteCarloRequested>(ref.project,std::vector<CharacteristicId>{ref.characteristic}); bus->ProcessQueue();
        EXPECT_EQ(submissions,0); EXPECT_EQ(store->GetMonteCarloResult(ref),previous);
        EXPECT_EQ(store->GetMonteCarloState(ref)->status,"completed");
        EXPECT_NE(store->GetStatusMessage().find(std::to_string(JFMMonteCarloSampleLimit("SD-0"))),std::string::npos);
    }

    TEST(JFMFitEngineTests, MonteCarloCancelledBeforeExecuteReleasesQueueAndKeepsResult)
    {
        auto store=CreateRef<JFMWorkspaceStore>(); auto record=MakeRecord("SD-0");
        record.info.fit=JFMFitState{}; record.info.fit->modelId="SD-0"; record.info.fit->status="completed";
        const JFMCharacteristicRef ref{store->GetActiveProjectHandle(),store->AddCharacteristicRecords(store->GetActiveProjectHandle(),{record}).front()};
        JFMMonteCarloState state; state.modelId="SD-0";
        const auto previous=CreateRef<const JFMMonteCarloResult>();
        ASSERT_TRUE(store->ApplyMonteCarloResult(ref,store->BeginMonteCarlo(ref),state,previous));
        auto queue=CreateRef<JFMMonteCarloWorkQueue>();
        ASSERT_TRUE(queue->Enqueue({{ref,store->BeginMonteCarlo(ref)}}));
        JobSystem jobs({},1);
        const auto id=jobs.SubmitAfter(CreateRef<JFMMonteCarloJob>(nullptr,store,queue),Time::Milliseconds(60000));
        ASSERT_TRUE(jobs.RequestCancel(id)); jobs.Shutdown(true);
        ASSERT_TRUE(jobs.GetJob(id)); EXPECT_EQ(jobs.GetJob(id)->status,JobStatus::Cancelled);
        EXPECT_EQ(store->GetMonteCarloResult(ref),previous);
        EXPECT_EQ(store->GetMonteCarloState(ref)->status,"cancelled");
        EXPECT_TRUE(queue->Enqueue({{ref,store->BeginMonteCarlo(ref)}}));
        queue->Reset();
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

        // 80-digit reference for the same log-ratio objective, independent of native libm rounding.
        constexpr double expected = 0.0130955426609834715920360489573636408496240556704542490168792887824964479875424;
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
		EXPECT_EQ(result->state.backend, "cpu");
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
		EXPECT_EQ(result->result.idealCurrent.size(), record.data->current.size());
		EXPECT_EQ(result->result.noisyCurrent.size(), 4 * record.data->current.size());
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
		ASSERT_EQ(result->result.noisyCurrent.size(), result->result.idealCurrent.size() * 2);
		for (std::size_t sample = 0; sample < 2; ++sample)
			for (std::size_t point = 0; point < result->result.idealCurrent.size(); ++point)
				EXPECT_DOUBLE_EQ(result->result.noisyCurrent[sample * result->result.idealCurrent.size() + point], result->result.idealCurrent[point]);
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
        const auto [lo, hi] = std::minmax_element(result->result.voltage.begin(), result->result.voltage.end());
        EXPECT_NEAR(*lo, 0.0958904, 1e-8); EXPECT_NEAR(*hi, 0.643379, 1e-8);
        EXPECT_EQ(data->voltage, rawVoltage);
        data->current = rawCurrent;
        EXPECT_TRUE(RunJFMMonteCarlo(record, *record.info.prefit, fit, settings));
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
        settings.objectiveMode = "log_legacy";
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
        JFMFitMonteCarloSettings settings; settings.fitMethod = "nm"; settings.preferGpu = false;
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
        for (double sigma : result->result.currentSigma) EXPECT_DOUBLE_EQ(sigma, 0.);
        for (const auto &sample : result->result.samples) EXPECT_DOUBLE_EQ(sample.chiSquared, 0.);
    }

    TEST(JFMFitEngineTests, RealSimplexConvergenceStopsBeforeIterationBudget)
    {
        auto record = MakeRecord("SD-0");
        JFMFitState fit; fit.modelId = "SD-0"; fit.parameters = record.info.prefit->parameters;
        JFMFitMonteCarloSettings settings; settings.fitMethod = "nm"; settings.preferGpu = false;
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
    TEST(JFMFitEngineTests, SharedCurrentPreservesJunctionZero80DigitReference)
    {
        const std::unordered_map<std::string,double> p = {{"I0",1e-12},{"A",1.5},{"Rs",1e9},{"Rsh",1e6}};
        const double v[] = {std::nextafter(.001,0.),.001,std::nextafter(.001,1.)};
        const double oracle[] = {9.99999999999828487367761212899202692681153876613455e-13,
            1.00000000000003988198809452595472837948552149707722e-12,
            1.00000000000025127660842783901025406628988932017698e-12};
        for (int i=0;i<3;++i) EXPECT_NEAR(EvaluateJFMCurrent("SD-RsRsh",v[i],300.,p),oracle[i],std::abs(oracle[i])*1e-12);
    }

    TEST(JFMFitEngineTests, LmIsDefaultAndNmRemainsAnExplicitOption)
    {
        auto record = MakeRecord("SD-RsRsh");
        JFMFitMonteCarloSettings settings; settings.preferGpu = false;
        settings.parameters = {{"I0",{1e-14,1e-5,true}},{"A",{.5,3.,true}},{"Rs",{0.,100.,true}},{"Rsh",{10.,1e9,true}}};
        ASSERT_EQ(settings.fitMethod,"lm");
        auto lm = RunJFMSimplexFit(record,settings); ASSERT_TRUE(lm);
        EXPECT_EQ(lm->state.fitMethod,"LM"); EXPECT_TRUE(lm->state.accepted);
        settings.fitMethod = "nm";
        auto nm = RunJFMSimplexFit(record,settings); ASSERT_TRUE(nm);
        EXPECT_EQ(nm->state.fitMethod,"NM"); EXPECT_LE(lm->state.chiSquared,nm->state.chiSquared+1e-28);
    }

    TEST(JFMFitEngineTests, LmCapRunsNmFallbackAndReportsBothStatuses)
    {
        auto record = MakeRecord("SD-0"); record.info.prefit->parameters["I0"].value *= 2;
        JFMFitMonteCarloSettings settings; settings.preferGpu=false; settings.fitMaxIterations=1;
        auto result=RunJFMSimplexFit(record,settings); ASSERT_TRUE(result);
        EXPECT_EQ(result->state.fitMethod,"LM→NM"); EXPECT_EQ(result->state.lmStatus,0);
        EXPECT_FALSE(result->state.converged); EXPECT_EQ(result->state.stopReason,"max_iter");
    }

    TEST(JFMFitEngineTests, FlatDirectionsAreFlaggedSeparatelyFromConvergence)
    {
        auto record=MakeRecord("SD-RsRsh");
        record.info.prefit->parameters["Rsh"].value=1e15;
        JFMFitMonteCarloSettings settings; settings.preferGpu=false; settings.monteCarloSamples=1;
        settings.monteCarloNoisePercent=0; settings.monteCarloStartPerturbationPercent=0;
        JFMFitState fit; fit.modelId=record.info.prefit->modelId; fit.parameters=record.info.prefit->parameters;
        auto result=RunJFMMonteCarlo(record,*record.info.prefit,fit,settings); ASSERT_TRUE(result);
        const auto &sample=result->result.samples.front();
        EXPECT_TRUE(sample.flatDirections); EXPECT_FALSE(sample.solverConverged);
        EXPECT_EQ(sample.stopReason,"flat_direction"); EXPECT_TRUE(sample.accepted);
    }

    TEST(JFMFitEngineTests, HeadlessFitUsesCommandAndJobAndValidatesMethod)
    {
        auto record=MakeRecord("SD-0");
        nlohmann::json q={{"voltage_V",record.data->voltage},{"current_A",record.data->current},
            {"temperature_K",300.},{"model_id","SD-0"},{"parameters",{{"I0",1e-10},{"A",1.5}}}};
        auto result=RunJFMFitRequest(q); ASSERT_TRUE(result.at("ok")) << result.dump();
        EXPECT_EQ(result.at("fit_method"),"LM"); EXPECT_TRUE(result.contains("job_id"));
        const auto firstId=result.at("job_id").get<std::uint64_t>();
        auto second=RunJFMFitRequest(q); ASSERT_TRUE(second.at("ok")); EXPECT_GT(second.at("job_id").get<std::uint64_t>(),firstId);
        q["method"]="bogus"; result=RunJFMFitRequest(q); EXPECT_FALSE(result.at("ok"));
        EXPECT_EQ(result.at("error").at("code"),"jfm.fit.validation");
        q["method"]="lm"; q["noise_percent"]=-1.; result=RunJFMFitRequest(q);
        EXPECT_FALSE(result.at("ok")); EXPECT_EQ(result.at("error").at("code"),"jfm.fit.validation");
        q["noise_percent"]=0.5; q["samples"]=JFMMonteCarloSampleLimit("SD-0")+1;
        result=RunJFMFitRequest(q);
        EXPECT_FALSE(result.at("ok")); EXPECT_EQ(result.at("error").at("code"),"jfm.fit.failed");
        EXPECT_NE(result.at("error").at("message").get<std::string>().find("64 MiB"),std::string::npos);
        ASSERT_TRUE(result.contains("job_id")); EXPECT_GT(result.at("job_id").get<std::uint64_t>(),firstId);
        EXPECT_TRUE(result.contains("project_id")); EXPECT_TRUE(result.contains("characteristic_id"));
    }

} // namespace SST::Prototypes::JFM

TEST(JFMParameterLabels, PreserveImportedUnitsAndCurrentDensityPrecedence)
{
    using namespace SST::Prototypes::JFM;
    JFMCharacteristicInfo info;
    info.columns = {{"Voltage", "mV", "voltage"}, {"Current", "uA", "current"}, {"Density", "mA/cm^2", "current_density"}};
    EXPECT_EQ(JFMParameterLabel("I0", &info), "I0 [mA/cm^2]");
    EXPECT_EQ(JFMParameterLabel("Rs", &info), "Rs [(mV)/(mA/cm^2)]");
    EXPECT_EQ(JFMParameterLabel("Rsh2", &info), "Rsh2 [(mV)^alpha/(mA/cm^2)]");
    EXPECT_EQ(JFMParameterLabel("A", &info), "A [1]");
    info.columns.pop_back();
    EXPECT_EQ(JFMParameterLabel("I_ph", &info), "I_ph [uA]");
    info.columns[0].role.clear(); info.columns[1].role.clear();
    EXPECT_EQ(JFMParameterLabel("Rs", &info), "Rs [(mV)/(uA)]");
    EXPECT_EQ(JFMParameterLabel("I0"), "I0 [A]");
}

namespace SST::Prototypes::JFM {
TEST(JFMObjectiveTests, ResidualAndAnalyticDerivativeAreContinuousAndUnitInvariant) {
    GpuParamLayout policy; policy.shift=.03; policy.switch_k=3;
    for (auto mode : {JfmObjective::IscShift,JfmObjective::SignedSigma}) {
        policy.objective=mode;
        for (double y : {-.04,-.03,-.027,-.02,0.,1.,2.})
            for(double x : {-.04,-.03,-.02700000001,-.027,-.02699999999,-.02,0.,1.,2.}) {
                double d; const double r=lm::statisticalResidual(x,y,.001,policy,&d);
                ASSERT_TRUE(std::isfinite(r));
                const double eps=1e-8;
                const double numeric=(lm::statisticalResidual(x+eps,y,.001,policy)-lm::statisticalResidual(x-eps,y,.001,policy))/(2*eps);
                EXPECT_NEAR(d,numeric,std::abs(d)*2e-6);
                auto scaled=policy; scaled.shift*=1000;
                EXPECT_NEAR(r,lm::statisticalResidual(x*1000,y*1000,1.,scaled),std::max(1.,std::abs(r))*1e-11);
                EXPECT_DOUBLE_EQ(lm::statisticalResidual(y,y,.001,policy),0);
            }
    }
    policy.objective=JfmObjective::IscShift;
    EXPECT_NEAR(lm::statisticalResidual(-.04,-.03,.001,policy),-10.,1e-13);
    const double v[]={-.1,.1}, y[]={-.032,-.028};
    EXPECT_DOUBLE_EQ(lm::iscShift(v,y,2),.03);
}
TEST(JFMObjectiveTests, LightFitUsesMeasuredShiftAndReportsBranches) {
    auto record=MakeRecord("SD-RsRsh-I_ph");
    auto data=CreateRef<JFMCharacteristicData>(*record.data);
    data->voltage.insert(data->voltage.begin(),{-.1,0.});
    data->current.insert(data->current.begin(),{EvaluateJFMCurrent("SD-RsRsh-I_ph",-.1,300.,Parameters),EvaluateJFMCurrent("SD-RsRsh-I_ph",0.,300.,Parameters)});
    record.data=data; record.info.prefit->fitRange.lowerVoltage=-.1;
    JFMFitMonteCarloSettings options; options.preferGpu=false;
    for(auto mode : {"auto","isc_shift","signed_sigma"}) {
        options.objectiveMode=mode;
        auto result=RunJFMSimplexFit(record,options);
        ASSERT_TRUE(result) << result.Error().technicalDetails;
        EXPECT_TRUE(result->state.accepted);
        EXPECT_EQ(result->state.objective.mode,std::string(mode)=="auto"?"isc_shift":mode);
        EXPECT_GT(result->state.objective.sigmaAbsolute,0);
        auto magnitudes=data->current;for(double &y:magnitudes)y=std::abs(y);std::sort(magnitudes.begin(),magnitudes.end());
        EXPECT_DOUBLE_EQ(result->state.objective.sigmaAbsolute,magnitudes[magnitudes.size()/2]*1e-4);
        EXPECT_EQ(result->state.objective.linearPoints+result->state.objective.logPoints,data->voltage.size());
        if (std::string(mode)!="signed_sigma") EXPECT_DOUBLE_EQ(result->state.objective.shift,-data->current[1]);
    }
    options.objectiveMode="log_legacy";
    EXPECT_FALSE(RunJFMSimplexFit(record,options));
}
TEST(JFMObjectiveTests, StatisticalMonteCarloHandlesZeroCrossingAndAbsoluteNoise) {
    auto record=MakeRecord("SD-RsRsh-I_ph");
    JFMFitState fit; fit.modelId=record.info.prefit->modelId; fit.parameters=record.info.prefit->parameters;
    JFMFitMonteCarloSettings options; options.preferGpu=false; options.objectiveMode="signed_sigma";
    options.monteCarloSamples=8; options.sigmaAbsolute=1e-5; options.monteCarloNoisePercent=0;
    options.monteCarloMaxIterations=200;
    auto result=RunJFMMonteCarlo(record,*record.info.prefit,fit,options);
    ASSERT_TRUE(result) << result.Error().technicalDetails;
    EXPECT_EQ(result->state.acceptedCount,8); EXPECT_TRUE(result->result.confidenceAvailable);
    for(double sigma:result->result.currentSigma) EXPECT_DOUBLE_EQ(sigma,1e-5);
    EXPECT_NE(result->result.noisyCurrent[0],result->result.idealCurrent[0]);
    auto automatic=options;automatic.sigmaAbsolute=0;
    auto withFloor=RunJFMMonteCarlo(record,*record.info.prefit,fit,automatic);
    ASSERT_TRUE(withFloor);EXPECT_GT(withFloor->state.objective.sigmaAbsolute,0);
    EXPECT_TRUE(withFloor->result.confidenceAvailable);
    EXPECT_NE(withFloor->result.noisyCurrent[0],withFloor->result.idealCurrent[0]);
    for(auto &s:result->result.samples) {
        EXPECT_TRUE(std::isfinite(s.objectiveChiSquared));
        EXPECT_DOUBLE_EQ(s.reducedChiSquared,s.chiSquared/5);
    }
}
TEST(JFMObjectiveTests, HeadlessObjectiveValidationIsTyped) {
    nlohmann::json q={{"voltage_V",{-.1,0.,.1,.2,.3,.4,.5,.6}}, {"current_A",{-.03,-.03,-.03,-.03,-.03,-.02,-.01,.01}},
        {"temperature_K",300.},{"model_id","SD-0-I_ph"},{"parameters",{{"I0",1e-10},{"A",1.5},{"I_ph",.03}}}};
    for (auto mode : {"isc_shift","signed_sigma"}) {
        q["objective_mode"]=mode; auto r=RunJFMFitRequest(q); ASSERT_TRUE(r.at("ok")) << r.dump();
        EXPECT_EQ(r["objective"]["mode"],mode); EXPECT_TRUE(r["objective"].contains("sigma_abs_A"));
    }
    for(auto bad : {nlohmann::json{{"objective_mode","wrong"}},nlohmann::json{{"sigma_abs_A",-1.}},nlohmann::json{{"objective_switch_k",0.}}}) {
        auto input=q;input.update(bad);auto r=RunJFMFitRequest(input);
        EXPECT_FALSE(r.at("ok"));EXPECT_EQ(r["error"]["code"],"jfm.fit.validation");
    }
}
}

TEST(JFMStatisticalObjective, UsesAmpereSigmaWithoutCurrentDomainRestrictions) {
    const double p[]={0},v[]={0,1},y[]={-1,1},sigma[]={.1,.2};
    GpuParamLayout policy;policy.objective=JfmObjective::SignedSigma;
    EXPECT_NEAR(computeChiSquared(p,v,y,sigma,2,[](double v,const double*){return v==0?-.9:1.2;},policy),2.,1e-14);
}

TEST(JFMStatisticalObjective, AnalyticNormalEquationsMatchFiniteDifferencesInBothModes) {
    GpuParamLayout l;l.n_total=l.n_free=7;l.T=300.;l.shift=.03;l.sigma_abs=1e-5;l.sigma_rel=.01;
    double p[]={1e-9,1.5,2.,1000.,2.3,200.,.03},v[]={.02,.2,.4,.6,.8,1.};
    double y[6];for(int j=0;j<7;++j){l.free_to_total[j]=j;l.fixed_values[j]=p[j];}
    for(int i=0;i<6;++i)y[i]=lm::current(GpuModelType::SDRsRshAlphaRsh2Iph,v[i],p,300.,nullptr,true)+1e-5*(i-2);
    for(auto mode:{JfmObjective::IscShift,JfmObjective::SignedSigma}) {
        l.objective=mode;double h[64],g[8];
        const double f=lm::objective(GpuModelType::SDRsRshAlphaRsh2Iph,l,p,v,y,6,h,g,true,true);ASSERT_TRUE(std::isfinite(f));
        for(int j=0;j<7;++j) {
            const double old=p[j],eps=std::abs(old)*1e-5;p[j]=old+eps;
            const double plus=lm::objective(GpuModelType::SDRsRshAlphaRsh2Iph,l,p,v,y,6,nullptr,nullptr,true,true);
            p[j]=old-eps;const double minus=lm::objective(GpuModelType::SDRsRshAlphaRsh2Iph,l,p,v,y,6,nullptr,nullptr,true,true);p[j]=old;
            EXPECT_NEAR(g[j],(plus-minus)/(4*eps),std::max(1e-8,std::abs(g[j])*1e-3));
        }
    }
}

namespace SST::Prototypes::JFM {
TEST(JFMObjectiveTests, DarkAutoIgnoresNegativePointsOutsideFitRangeBitwise) {
    auto record=MakeRecord("SD-RsRsh");JFMFitMonteCarloSettings options;options.preferGpu=false;
    auto original=RunJFMSimplexFit(record,options);ASSERT_TRUE(original);
    auto data=CreateRef<JFMCharacteristicData>(*record.data);data->voltage.insert(data->voltage.begin(),-.1);data->current.insert(data->current.begin(),-1e-8);record.data=data;
    auto extended=RunJFMSimplexFit(record,options);ASSERT_TRUE(extended);
    EXPECT_EQ(extended->state.objective.mode,"log_legacy");
    EXPECT_DOUBLE_EQ(original->state.chiSquared,extended->state.chiSquared);
    for(auto &[name,value]:original->state.parameters) EXPECT_DOUBLE_EQ(value.value,extended->state.parameters.at(name).value);
}
}

TEST(JFMParameterLabels, SiDefaultsAndDensityResistanceUsePhysicalUnits)
{
    using namespace SST::Prototypes::JFM;
    EXPECT_EQ(JFMParameterLabel("Rs"), "Rs [Ω]");
    EXPECT_EQ(JFMParameterLabel("I0"), "I0 [A]");
    JFMCharacteristicInfo info; info.columns = {{"V","V","voltage"},{"J","A/cm2","current_density"}};
    EXPECT_EQ(JFMParameterLabel("Rsh", &info), "Rsh [Ω·cm²]");
    info.columns[1].unit = "A/m2";
    EXPECT_EQ(JFMParameterLabel("Rsh", &info), "Rsh [Ω·m²]");
}

#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdlib>
#include <bit>
#include "Analysis/JFMMonteCarloStatistics.hpp"
namespace SST::Prototypes::JFM {
TEST(JFMRetention, SavedPrefixAndRegeneratedNoiseAreBitIdentical) {
    auto record=MakeRecord("SD-0");JFMFitState fit;fit.modelId="SD-0";fit.parameters=record.info.prefit->parameters;
    JFMFitMonteCarloSettings options;options.preferGpu=false;options.monteCarloSamples=7;options.monteCarloMaxIterations=2;
    for(const auto *mode:{"log_legacy","signed_sigma","isc_shift"}) {
        if(std::string_view(mode)=="isc_shift") {
            auto data=CreateRef<JFMCharacteristicData>(*record.data);
            data->voltage.insert(data->voltage.begin(),0.);data->current.insert(data->current.begin(),0.);record.data=data;
        }
        options.objectiveMode=mode;options.sigmaAbsolute=1e-12;options.saveSamples=true;options.savedSampleLimit=0;
        auto full=RunJFMMonteCarlo(record,*record.info.prefit,fit,options);ASSERT_TRUE(full)<<full.Error().technicalDetails;
        for(bool save:{false,true}) {
            options.saveSamples=save;options.savedSampleLimit=2;
            auto bounded=RunJFMMonteCarlo(record,*record.info.prefit,fit,options);ASSERT_TRUE(bounded);
            EXPECT_EQ(bounded->result.noisyCurrent.size(),save?2*bounded->result.voltage.size():0);
            EXPECT_EQ(bounded->state.acceptedCount,full->state.acceptedCount);
            EXPECT_EQ(bounded->state.convergedCount,full->state.convergedCount);
            ASSERT_EQ(bounded->result.samples.size(),full->result.samples.size());
            for(std::size_t i=0;i<full->result.samples.size();++i) {
                EXPECT_EQ(JFMMonteCarloNoisyCurve(bounded->result,i),JFMMonteCarloNoisyCurve(full->result,i));
                EXPECT_EQ(bounded->result.samples[i].parameters,full->result.samples[i].parameters);
                EXPECT_EQ(bounded->result.samples[i].chiSquared,full->result.samples[i].chiSquared);
            }
            const auto a=CalculateJFMMonteCarloScatterStatistics(full->result,0,1),b=CalculateJFMMonteCarloScatterStatistics(bounded->result,0,1);
            ASSERT_EQ(bool(a),bool(b));if(a){EXPECT_EQ(a->meanX,b->meanX);EXPECT_EQ(a->meanY,b->meanY);EXPECT_EQ(a->covarianceXX,b->covarianceXX);EXPECT_EQ(a->covarianceXY,b->covarianceXY);EXPECT_EQ(a->covarianceYY,b->covarianceYY);}
        }
    }
    options.monteCarloSamples=std::numeric_limits<int>::max();
    EXPECT_FALSE(RunJFMMonteCarlo(record,*record.info.prefit,fit,options));
}
TEST(PerfRetain, MonteCarlo4096) {
    if (!std::getenv("SST_MEMORY_PROBE")) GTEST_SKIP();
    auto record=MakeRecord("SD-0");
    auto data=CreateRef<JFMCharacteristicData>();
    for(int i=0;i<4096;++i) {
        const double v=.02+.70*i/4095.; data->voltage.push_back(v);
        data->current.push_back(EvaluateJFMCurrent("SD-0",v,300.,Parameters));
    }
    record.data=data; record.info.prefit->fitRange={0,4096,.02,.72,"memory"};
    JFMFitState fit; fit.modelId="SD-0"; fit.parameters=record.info.prefit->parameters;
    JFMFitMonteCarloSettings settings; settings.preferGpu=false; settings.saveSamples=false;
    settings.monteCarloSamples=4096; settings.monteCarloMaxIterations=1;
    auto result=RunJFMMonteCarlo(record,*record.info.prefit,fit,settings); ASSERT_TRUE(result);
    std::uint64_t hash=14695981039346656037ULL;
    auto add=[&](double value){hash^=std::bit_cast<std::uint64_t>(value);hash*=1099511628211ULL;};
    for(const auto &sample:result->result.samples) {
        for(double value:sample.parameters)add(value);
        add(sample.chiSquared); add(sample.reducedChiSquared); add(sample.objectiveChiSquared);
        add(sample.iterations); add(sample.accepted); add(sample.solverConverged); add(sample.lmStatus);
    }
    for(std::size_t sample=0;sample<result->result.samples.size();++sample)for(double value:JFMMonteCarloNoisyCurve(result->result,sample))add(value);
    const auto stats=CalculateJFMMonteCarloScatterStatistics(result->result,0,1);
    ASSERT_TRUE(stats); add(stats->meanX); add(stats->meanY); add(stats->covarianceXX); add(stats->covarianceXY); add(stats->covarianceYY);
    std::printf("RETAIN jfm numeric_hash=%llu\n",static_cast<unsigned long long>(hash));
    EXPECT_EQ(hash,324877544310535566ULL); // Frozen MSVC baseline: all sample values, noise and scatter statistics.
    EXPECT_TRUE(result->result.noisyCurrent.empty());
    PROCESS_MEMORY_COUNTERS_EX m{}; m.cb=sizeof(m);
    ASSERT_TRUE(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)));
    std::printf("RETAIN jfm samples=%zu private_bytes=%zu peak_commit_bytes=%zu\n",result->result.samples.size(),m.PrivateUsage,m.PeakPagefileUsage);
}
}
