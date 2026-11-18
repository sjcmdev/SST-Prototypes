#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <future>
#include "Core/IO/TextFileIO.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMExportRawTsvJob.hpp"
#include "IO/JFMImportJob.hpp"
#include "IO/JFMImportPreviewJob.hpp"
#include "IO/JFMImportPaths.hpp"
#include "IO/JFMPrefitJob.hpp"
#include "IO/JFMProjectJobs.hpp"
#include "IO/JFMPythonBindings.hpp"
#include "IO/JFMPythonImportBridge.hpp"
#include "IO/JFMPythonPrefitBridge.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"
#include "ScientificRuntime/Python/PythonInterpreter.hpp"

namespace SST::Prototypes::JFM
{
	namespace
	{
		JobContext MakeTestJobContext()
		{
			return JobContext(
				[](float, float) {},
				[](const std::string &) {},
				[](const std::string &) {},
				[](JobLogLevel, const std::string &) {},
				[] { return false; });
		}

#if DS_PYTHON_CAPI_AVAILABLE
		Result<void> EnsurePythonRuntimeForBridgeTests()
		{
			static PythonInterpreter interpreter;
			static bool started = false;
			if (started)
				return {};

			RegisterJFMPythonBindings();
			auto result = interpreter.Start();
			if (!result)
				return result;
			started = true;
			return {};
		}
#endif
	}

	TEST(JFMProjectJobsTests, SaveAndLoadPublishCompletionEvents)
	{
		auto bus = CreateRef<EventBus>();
		Path path = Path::FromResolved(FileSystem::TempDirectoryPath() / "sst-jfm-project-job-test.jfm.yaml");
		(void)FileSystem::Remove(path.Native());

		JFMProject project;
		project.id = "project";
		project.name = "Job Project";
		project.series.push_back(JFMSeries{"series-1", "Series", {}, {}});

		bool saveCompleted = false;
		bool loadCompleted = false;
		ProjectHandle savedProject = InvalidProjectHandle;
		JFMOperationId saveOperation = InvalidJFMOperationId;
		std::uint64_t saveRevision = 0;
		JFMOperationId loadOperation = InvalidJFMOperationId;
		JFMProject loadedProject;
		auto saveSub = bus->Subscribe<ProjectSaveCompleted>([&](const ProjectSaveCompleted &event) {
			saveCompleted = true;
			savedProject = event.project;
			saveOperation = event.operation;
			saveRevision = event.revision;
		});
		auto loadSub = bus->Subscribe<ProjectLoadCompleted>([&](const ProjectLoadCompleted &event) {
			loadCompleted = true;
			loadedProject = event.project;
			loadOperation = event.operation;
		});

		JobContext context = MakeTestJobContext();
		SaveProjectJob saveJob(bus, path, project, 7, 11, 23);
		saveJob.Execute(context);
		bus->ProcessQueue();

		ASSERT_TRUE(saveCompleted);
		ASSERT_TRUE(FileSystem::Exists(path.Native()));
		EXPECT_EQ(savedProject, 7);
		EXPECT_EQ(saveOperation, 11);
		EXPECT_EQ(saveRevision, 23);

		LoadProjectJob loadJob(bus, path, 12);
		loadJob.Execute(context);
		bus->ProcessQueue();

		ASSERT_TRUE(loadCompleted);
		EXPECT_EQ(loadedProject.name, "Job Project");
		EXPECT_EQ(loadOperation, 12);
		(void)saveSub;
		(void)loadSub;
		(void)FileSystem::Remove(path.Native());
	}

#ifdef _WIN32
    TEST(JFMProjectJobsTests, FailedReplacementPreservesProjectAndDoesNotPublishCompletion)
    {
        const Path path = Path::FromResolved(FileSystem::TempDirectoryPath() / L"sst-zapis-\u017c\u00f3\u0142\u0107.jfm.yaml");
        std::string error, text;
        ASSERT_TRUE(TextFileIO::Save(path, "old project", error)) << error;
        HANDLE locked = CreateFileW(path.Native().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        ASSERT_NE(locked, INVALID_HANDLE_VALUE);
        auto bus = CreateRef<EventBus>();
        bool completed = false, failed = false;
        auto completedSub = bus->Subscribe<ProjectSaveCompleted>([&](const auto &) { completed = true; });
        auto failedSub = bus->Subscribe<ProjectSaveFailed>([&](const auto &event) { failed = !event.error.empty(); });
        JFMProject project;
        project.id = "project";
        project.name = "New project";
        auto context = MakeTestJobContext();
        SaveProjectJob job(bus, path, project, 7, 11, 23);
        job.Execute(context);
        bus->ProcessQueue();
        EXPECT_FALSE(completed);
        EXPECT_TRUE(failed);
        EXPECT_FALSE(TextFileIO::Save(path, "replacement", error));
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(TextFileIO::Load(path, text, error));
        EXPECT_EQ(text, "old project");
        CloseHandle(locked);
        EXPECT_TRUE(TextFileIO::Save(path, "replacement", error));
        EXPECT_TRUE(TextFileIO::Load(path, text, error));
        EXPECT_EQ(text, "replacement");
        (void)FileSystem::Remove(path.Native());
    }
#endif

    TEST(JFMProjectJobsTests, TemporaryFileCreationFailurePreservesExistingFile)
    {
        const Path path = Path::FromResolved(FileSystem::TempDirectoryPath() / "sst-save-parent-file");
        std::string error, text;
        ASSERT_TRUE(TextFileIO::Save(path, "old contents", error));
        EXPECT_FALSE(TextFileIO::Save(path / std::filesystem::path("child"), "new", error));
        EXPECT_FALSE(error.empty());
        EXPECT_TRUE(TextFileIO::Load(path, text, error));
        EXPECT_EQ(text, "old contents");
        (void)FileSystem::Remove(path.Native());
    }

	TEST(JFMProjectJobsTests, PrefitWorkQueueKeepsOnlyLatestGeneration)
	{
		auto queue = CreateRef<JFMPrefitWorkQueue>();
		JFMPrefitWorkItem first;
		first.ref = {4, 7};
		first.generation = 1;
		JFMPrefitWorkItem latest = first;
		latest.generation = 2;

		EXPECT_TRUE(queue->Enqueue({first}));
		EXPECT_FALSE(queue->Enqueue({latest}));
		auto batch = queue->TakeNext();
		ASSERT_TRUE(batch);
		ASSERT_EQ(batch->size(), 1);
		EXPECT_EQ(batch->front().generation, 2);
		EXPECT_FALSE(queue->TakeNext());
		EXPECT_TRUE(queue->Enqueue({first}));
		queue->Reset();
		EXPECT_TRUE(queue->Enqueue({latest}));
	}

	TEST(JFMProjectJobsTests, ImportJobPublishesProgressAndTerminalEvent)
	{
		auto bus = CreateRef<EventBus>();
		bool sawProgress = false;
		bool sawTerminalEvent = false;
		bool routingMatches = true;
		auto progressSub = bus->Subscribe<ImportProgress>([&](const ImportProgress &event) {
			sawProgress = true;
			routingMatches = routingMatches && event.project == 8 && event.operation == 13;
		});
		auto failedSub = bus->Subscribe<ImportFailed>([&](const ImportFailed &event) {
			sawTerminalEvent = true;
			routingMatches = routingMatches && event.project == 8 && event.operation == 13;
		});
		auto completedSub = bus->Subscribe<ImportCompleted>([&](const ImportCompleted &event) {
			sawTerminalEvent = true;
			routingMatches = routingMatches && event.project == 8 && event.operation == 13;
		});

		JobContext context = MakeTestJobContext();
		JFMImportJob job(bus, std::vector<Path>{Path::FromResolved("missing.dat")}, Path::FromResolved("missing-python-root"), {}, 8, 13);
		job.Execute(context);
		bus->ProcessQueue();

		EXPECT_TRUE(sawProgress);
		EXPECT_TRUE(sawTerminalEvent);
		EXPECT_TRUE(routingMatches);
		(void)progressSub;
		(void)failedSub;
		(void)completedSub;
	}

	TEST(JFMProjectJobsTests, MaterializeProjectDataWarnsForMissingSource)
	{
		auto bus = CreateRef<EventBus>();
		bool completed = false;
		ProjectHandle completedProject = InvalidProjectHandle;
		JFMOperationId completedOperation = InvalidJFMOperationId;
		std::vector<JFMCharacteristicRecord> characteristics;
		std::vector<std::string> warnings;
		auto completedSub = bus->Subscribe<ProjectDataMaterializationCompleted>([&](const ProjectDataMaterializationCompleted &event) {
			completed = true;
			completedProject = event.project;
			completedOperation = event.operation;
			characteristics = event.characteristics;
			warnings = event.warnings;
		});

		JFMProject project;
		project.id = "project";
		project.name = "Missing Source Project";
		project.characteristics.push_back(JFMCharacteristicInfo{
			42,
			"Missing Source",
			"IV",
			Path::FromResolved(FileSystem::TempDirectoryPath() / "sst-jfm-missing-source.dat")});
		project.series.push_back(JFMSeries{"series-1", "Series", {}, {42}});

		JobContext context = MakeTestJobContext();
		MaterializeProjectDataJob job(bus, project, Path::FromResolved("missing-python-root"), 9, 14);
		job.Execute(context);
		bus->ProcessQueue();

		ASSERT_TRUE(completed);
		EXPECT_TRUE(characteristics.empty());
		ASSERT_FALSE(warnings.empty());
		EXPECT_NE(warnings.front().find("Source file missing"), std::string::npos);
		EXPECT_EQ(completedProject, 9);
		EXPECT_EQ(completedOperation, 14);
		(void)completedSub;
	}

	TEST(JFMProjectJobsTests, BatchExportSkipsMissingRawArraysAndPublishesWarning)
	{
		auto bus = CreateRef<EventBus>();
		bool completed = false;
		ProjectHandle completedProject = InvalidProjectHandle;
		JFMOperationId completedOperation = InvalidJFMOperationId;
		std::vector<std::string> warnings;
		auto completedSub = bus->Subscribe<ExportRawTsvBatchCompleted>([&](const ExportRawTsvBatchCompleted &event) {
			completed = true;
			completedProject = event.project;
			completedOperation = event.operation;
			warnings = event.warnings;
		});

		JFMCharacteristicInfo validInfo;
		validInfo.name = "valid";
		auto validData = CreateRef<const JFMCharacteristicData>(JFMCharacteristicData{{0.0, 1.0}, {1.0, 2.0}, {}});
		JFMCharacteristicInfo missingInfo;
		missingInfo.name = "missing";

		const Path output = Path::FromResolved(FileSystem::TempDirectoryPath() / "sst-jfm-batch-export");
		(void)FileSystem::RemoveAll(output.Native());

		JobContext context = MakeTestJobContext();
		JFMExportRawTsvBatchJob job(
			bus,
			output,
			{
				JFMExportItem{Path("series") / Path("valid.tsv"), {std::move(validInfo), std::move(validData)}},
				JFMExportItem{Path("series") / Path("missing.tsv"), {std::move(missingInfo), nullptr}},
			},
			17,
			19);
		job.Execute(context);
		bus->ProcessQueue();

		ASSERT_TRUE(completed);
		ASSERT_FALSE(warnings.empty());
		EXPECT_EQ(completedProject, 17);
		EXPECT_EQ(completedOperation, 19);
		EXPECT_TRUE(FileSystem::Exists((output / Path("series") / Path("valid.tsv")).Native()));
		(void)FileSystem::RemoveAll(output.Native());
		(void)completedSub;
	}

	TEST(JFMProjectJobsTests, PythonRootResolverCoversBundleAndRepoLayouts)
	{
		const Path repoRoot = Path::FromResolved(FileSystem::CurrentPath());
		const Path repoPython = repoRoot / Path("Prototypes") / Path("JFM") / Path("python");
		ASSERT_TRUE(FileSystem::Exists(repoPython.Native())) << repoPython.String();

		ApplicationConfig config;
		config.paths.installRoot = repoRoot / Path("build") / Path("bin") / Path("Debug-windows-x86_64") / Path("JFM") / Path("install");
		const std::vector<Path> candidates = BuildJFMPythonRootCandidates(config, repoRoot);

		ASSERT_GE(candidates.size(), 2);
		EXPECT_EQ(candidates.front(), config.paths.installRoot.parent_path() / Path("python"));
		EXPECT_NE(std::find(candidates.begin(), candidates.end(), repoPython), candidates.end());
		EXPECT_FALSE(ResolveJFMPythonRoot(ApplicationConfig{}, repoRoot).Empty());
	}

	TEST(JFMProjectJobsTests, PythonImportBridgeImportsDatCharacteristic)
	{
		if (!JFMPythonImportBridge::IsAvailable())
			GTEST_SKIP() << "Embedded Python is unavailable in this build.";

		const Path repoRoot = Path::FromResolved(FileSystem::CurrentPath());
		const Path pythonRoot = ResolveJFMPythonRoot(ApplicationConfig{}, repoRoot);
		const Path dataPath = repoRoot / Path("Prototypes") / Path("JFM") / Path("example-data") / Path("real-measurement") / Path("C002") / Path("IVT C002_Z015_6") / Path("IV_C002_Z015_6_300,0K.dat");
		ASSERT_TRUE(FileSystem::Exists(pythonRoot.Native())) << pythonRoot.String();
		ASSERT_TRUE(FileSystem::Exists(dataPath.Native())) << dataPath.String();

#if DS_PYTHON_CAPI_AVAILABLE
		auto runtime = EnsurePythonRuntimeForBridgeTests();
		ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;
		const PyGILState_STATE gil = PythonInterpreter::AcquireRuntimeGil();
		ASSERT_TRUE(PyGILState_Check());

		JFMPythonImportBridge bridge(pythonRoot);
		Result<JFMImportBatchResult> result = bridge.ImportFiles({dataPath}, {});
		PythonInterpreter::ReleaseRuntimeGil(gil);

		ASSERT_TRUE(result) << result.Error().technicalDetails;
		ASSERT_FALSE(result->characteristics.empty());
		const JFMCharacteristic &characteristic = result->characteristics.front();
		EXPECT_EQ(characteristic.info.parser, "dat");
		EXPECT_FALSE(characteristic.info.sourceRecordKey.empty());
		EXPECT_FALSE(characteristic.data.voltage.empty());
		EXPECT_FALSE(characteristic.data.current.empty());
		EXPECT_TRUE(characteristic.info.temperatureK.has_value());
#endif
	}

	TEST(JFMProjectJobsTests, PythonImportBridgeCanImportAgainFromWorkerThread)
	{
		if (!JFMPythonImportBridge::IsAvailable())
			GTEST_SKIP() << "Embedded Python is unavailable in this build.";

		const Path repoRoot = Path::FromResolved(FileSystem::CurrentPath());
		const Path pythonRoot = ResolveJFMPythonRoot(ApplicationConfig{}, repoRoot);
		const Path dataPath = repoRoot / Path("Prototypes") / Path("JFM") / Path("example-data") / Path("real-measurement") / Path("C002") / Path("IVT C002_Z015_6") / Path("IV_C002_Z015_6_300,0K.dat");

#if DS_PYTHON_CAPI_AVAILABLE
		auto runtime = EnsurePythonRuntimeForBridgeTests();
		ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;

		JFMPythonImportBridge firstBridge(pythonRoot);
		{
			const PyGILState_STATE gil = PythonInterpreter::AcquireRuntimeGil();
			ASSERT_TRUE(PyGILState_Check());
			ASSERT_TRUE(firstBridge.ImportFiles({dataPath}, {}));
			PythonInterpreter::ReleaseRuntimeGil(gil);
		}

		auto workerResult = std::async(std::launch::async, [pythonRoot, dataPath] {
			const PyGILState_STATE gil = PythonInterpreter::AcquireRuntimeGil();
			JFMPythonImportBridge bridge(pythonRoot);
			Result<JFMImportBatchResult> result = bridge.ImportFiles({dataPath}, {});
			PythonInterpreter::ReleaseRuntimeGil(gil);
			return result;
		});

		Result<JFMImportBatchResult> result = workerResult.get();
		ASSERT_TRUE(result) << result.Error().technicalDetails;
		ASSERT_FALSE(result->characteristics.empty());
#endif
	}

	TEST(JFMProjectJobsTests, PythonPrefitBridgeReturnsManualRangeParametersAndCurve)
	{
#if DS_PYTHON_CAPI_AVAILABLE
		const Path repoRoot = Path::FromResolved(FileSystem::CurrentPath());
		const Path pythonRoot = ResolveJFMPythonRoot(ApplicationConfig{}, repoRoot);
		auto runtime = EnsurePythonRuntimeForBridgeTests();
		ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;

		auto data = CreateRef<JFMCharacteristicData>();
		constexpr std::size_t pointCount = 180;
		data->voltage.reserve(pointCount);
		data->current.reserve(pointCount);
		for (std::size_t index = 0; index < pointCount; ++index)
		{
			const double voltage = -0.1 + static_cast<double>(index) / static_cast<double>(pointCount - 1);
			data->voltage.push_back(voltage);
			data->current.push_back(2.0e-9 * std::expm1(voltage / (1.6 * 8.6e-5 * 300.0)) + voltage / 2.0e4);
		}
		JFMCharacteristicRecord characteristic;
		characteristic.info.id = 1;
		characteristic.info.name = "Synthetic diode";
		characteristic.info.temperatureK = 300.0;
		characteristic.info.prefit = JFMPrefitState{};
		characteristic.info.prefit->modelId = "SD-RsRsh";
		characteristic.info.prefit->manualLowerVoltage = 0.2;
		characteristic.info.prefit->manualUpperVoltage = 0.65;
		characteristic.data = data;

		const PyGILState_STATE gil = PythonInterpreter::AcquireRuntimeGil();
		JFMPythonPrefitBridge bridge(pythonRoot);
		auto result = bridge.Run(characteristic);
		PythonInterpreter::ReleaseRuntimeGil(gil);

		ASSERT_TRUE(result) << result.Error().technicalDetails;
		EXPECT_EQ(result->state.fitRange.reason, "manual");
		EXPECT_GE(result->state.fitRange.lowerVoltage, 0.19);
		EXPECT_LE(result->state.fitRange.upperVoltage, 0.66);
		EXPECT_TRUE(result->state.parameters.contains("I0"));
		EXPECT_TRUE(result->state.parameters.contains("A"));
		EXPECT_EQ(result->curve.voltage.size(), result->curve.current.size());
		ASSERT_FALSE(result->curve.current.empty());
		EXPECT_TRUE(std::all_of(result->curve.current.begin(), result->curve.current.end(), [](double value) {
			return std::isfinite(value);
		}));
#else
		GTEST_SKIP() << "Embedded Python is unavailable in this build.";
#endif
	}

	TEST(JFMProjectJobsTests, ImportPreviewMatchesRealImportMetadata)
	{
		if (!JFMPythonImportBridge::IsAvailable())
			GTEST_SKIP() << "Embedded Python is unavailable in this build.";

#if DS_PYTHON_CAPI_AVAILABLE
		const Path repoRoot = Path::FromResolved(FileSystem::CurrentPath());
		const Path pythonRoot = ResolveJFMPythonRoot(ApplicationConfig{}, repoRoot);
		const Path dataPath = repoRoot / Path("Prototypes") / Path("JFM") / Path("example-data") / Path("real-measurement") / Path("C002") / Path("IVT C002_Z015_6") / Path("IV_C002_Z015_6_300,0K.dat");
		auto runtime = EnsurePythonRuntimeForBridgeTests();
		ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;

		auto bus = CreateRef<EventBus>();
		Ref<const JFMImportPreviewPayload> preview;
		auto subscription = bus->Subscribe<ImportPreviewCompleted>([&](const ImportPreviewCompleted &event) {
			preview = event.payload;
		});
		const PyGILState_STATE gil = PythonInterpreter::AcquireRuntimeGil();
		JFMPythonImportBridge bridge(pythonRoot);
		auto imported = bridge.ImportFiles({dataPath}, {});
		JFMImportPreviewJob previewJob(bus, 3, {dataPath}, false, pythonRoot, {});
		JobContext context = MakeTestJobContext();
		previewJob.Execute(context);
		PythonInterpreter::ReleaseRuntimeGil(gil);
		bus->ProcessQueue();

		ASSERT_TRUE(imported) << imported.Error().technicalDetails;
		ASSERT_TRUE(preview);
		ASSERT_EQ(preview->items.size(), 1);
		ASSERT_FALSE(preview->items.front().characteristics.empty());
		const auto &previewInfo = preview->items.front().characteristics.front().info;
		const auto &importedInfo = imported->characteristics.front().info;
		EXPECT_EQ(previewInfo.name, importedInfo.name);
		EXPECT_EQ(previewInfo.sourceRecordKey, importedInfo.sourceRecordKey);
		EXPECT_EQ(previewInfo.temperatureK, importedInfo.temperatureK);
		(void)subscription;
#endif
	}

	TEST(JFMProjectJobsTests, MaterializationUsesSourceRecordKeyAndFullImportOptions)
	{
		if (!JFMPythonImportBridge::IsAvailable())
			GTEST_SKIP() << "Embedded Python is unavailable in this build.";

#if DS_PYTHON_CAPI_AVAILABLE
		const Path repoRoot = Path::FromResolved(FileSystem::CurrentPath());
		const Path pythonRoot = ResolveJFMPythonRoot(ApplicationConfig{}, repoRoot);
		const Path dataPath = repoRoot / Path("Prototypes") / Path("JFM") / Path("example-data") / Path("scaps-batch-file.iv");
		auto runtime = EnsurePythonRuntimeForBridgeTests();
		ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;

		JFMCharacteristicInfo valid;
		valid.id = 1;
		valid.name = "Renamed after import";
		valid.sourcePath = dataPath;
		valid.sourceRecordKey = "0";
		valid.importOptions.parser = "scaps_iv";
		JFMCharacteristicInfo invalid = valid;
		invalid.id = 2;
		invalid.name = "Different options";
		invalid.importOptions.parser = "custom";
		invalid.importOptions.customScriptPath = "missing-parser.py";

		JFMProject project;
		project.characteristics = {valid, invalid};
		project.series.push_back(JFMSeries{"series", "Series", {}, {1, 2}});
		auto bus = CreateRef<EventBus>();
		std::vector<JFMCharacteristicRecord> materialized;
		std::vector<std::string> warnings;
		auto subscription = bus->Subscribe<ProjectDataMaterializationCompleted>([&](const ProjectDataMaterializationCompleted &event) {
			materialized = event.characteristics;
			warnings = event.warnings;
		});

		const PyGILState_STATE gil = PythonInterpreter::AcquireRuntimeGil();
		MaterializeProjectDataJob job(bus, project, pythonRoot, 4, 5);
		JobContext context = MakeTestJobContext();
		job.Execute(context);
		PythonInterpreter::ReleaseRuntimeGil(gil);
		bus->ProcessQueue();

		ASSERT_EQ(materialized.size(), 1);
		EXPECT_EQ(materialized.front().info.id, 1);
		EXPECT_EQ(materialized.front().info.name, "Renamed after import");
		ASSERT_TRUE(materialized.front().data);
		EXPECT_FALSE(materialized.front().data->voltage.empty());
		EXPECT_FALSE(warnings.empty());
		(void)subscription;
#endif
	}
} // namespace SST::Prototypes::JFM
