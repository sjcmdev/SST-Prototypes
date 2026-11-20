#include <gtest/gtest.h>
#include <sst/physics/constants.hpp>

#include <algorithm>
#include <cmath>
#include <future>
#include <thread>
#include <chrono>
#include "Core/IO/TextFileIO.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMExportRawTsvJob.hpp"
#include "IO/JFMPlotExport.hpp"
#include "IO/JFMFitJobs.hpp"
#include "Presentation/JFMPlotDocument.hpp"
#include "IO/JFMImportJob.hpp"
#include "IO/JFMImportPreviewJob.hpp"
#include "IO/JFMImportPaths.hpp"
#include "IO/JFMPrefitJob.hpp"
#include "IO/JFMProjectJobs.hpp"
#include "IO/JFMProjectYamlSerializer.hpp"
#include "IO/JFMIORegistration.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"
#include "IO/JFMPythonBindings.hpp"
#include "IO/JFMPythonImportBridge.hpp"
#include "IO/JFMPythonPrefitBridge.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"
#include "ScientificRuntime/Python/PythonInterpreter.hpp"
#include "ScientificRuntime/Python/PythonGilScope.hpp"
#include "Core/App/Managers/ConfigManager.hpp"
#include "Core/App/Managers/ConfigProfileStore.hpp"
#include "Core/App/Serialization/YamlConfigSerializer.hpp"
#include "Core/Platform/PlatformFontDiscovery.hpp"
#include "Core/Logging/Logger.hpp"
#include "IO/JFMProjectBundleSerializer.hpp"
#include "IO/JFMPythonRuntime.hpp"
#include "Vendor/ImGui/imgui.h"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#endif

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
		ASSERT_TRUE(FileSystem::Exists(repoPython.Native())) << repoPython.Utf8();

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
		ASSERT_TRUE(FileSystem::Exists(pythonRoot.Native())) << pythonRoot.Utf8();
		ASSERT_TRUE(FileSystem::Exists(dataPath.Native())) << dataPath.Utf8();

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
			data->current.push_back(2.0e-9 * std::expm1(voltage / (1.6 * sst::physics::k_B_over_q * 300.0)) + voltage / 2.0e4);
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

    TEST(JFMProjectJobsTests, SelectedHzbCharacteristicHasVisibleSemilogPoints)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        const Path root = Path::FromResolved(FileSystem::CurrentPath());
        auto runtime = EnsurePythonRuntimeForBridgeTests();
        ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;
        std::string text, error;
        ASSERT_TRUE(TextFileIO::Load(root / Path("Prototypes/JFM/JFM-projects/HZB-project.yaml.jfm.yaml"), text, error)) << error;
        const auto loaded = JFMProjectYamlSerializer::Deserialize(text);
        ASSERT_TRUE(loaded);
        auto store = CreateRef<JFMWorkspaceStore>();
        auto handle = store->AttachProject(loaded.Value(), Path("HZB.jfm.yaml"));
        auto bus = CreateRef<EventBus>();
        IOLayer io(bus);
        RegisterJFMIOHandlers(io, {bus, store, {}, {}, {}, ResolveJFMPythonRoot(ApplicationConfig{}, root)});
        std::optional<PlotRequested> plotted;
        auto subscription = bus->Subscribe<PlotRequested>([&](const auto &event) { plotted = event; });
        MaterializeProjectDataJob job(bus, loaded.Value(), ResolveJFMPythonRoot(ApplicationConfig{}, root), handle);
        auto context = MakeTestJobContext();
        const auto gil = PythonInterpreter::AcquireRuntimeGil();
        job.Execute(context);
        PythonInterpreter::ReleaseRuntimeGil(gil);
        bus->ProcessQueue();
        bus->ProcessQueue();
        ASSERT_TRUE(plotted);
        EXPECT_EQ(plotted->project, handle);
        ASSERT_EQ(plotted->groups.size(), 1);
        EXPECT_EQ(plotted->groups.front().ids, (std::vector<CharacteristicId>{1}));
        EXPECT_FALSE(store->IsDirty(handle));
        const auto selected = store->GetSelectedCharacteristic();
        ASSERT_TRUE(selected);
        EXPECT_EQ(selected->info.name, "ivd_HZB25_T181_L0");
        JFMPlotDocument doc;
        const auto &data = selected->data;
        doc.BeginFrame();
        auto id = doc.SetSeries({"HZB###selected", {data.voltage.data(), data.current.data(), data.current.size()}}, true);
        sst::ui::plot::PlotSpec spec; spec.y.scale = sst::ui::plot::AxisScale::Log10;
        doc.EndFrame(spec);
        const auto series = doc.project.series(id);
        ASSERT_GT(series.data.count, 100);
        double minY = 1, maxY = 0;
        for (std::size_t i = 0; i < series.data.count; ++i) {
            ASSERT_TRUE(std::isfinite(series.data.y[i]));
            ASSERT_GT(series.data.y[i], 0);
            minY = (std::min)(minY, series.data.y[i]); maxY = (std::max)(maxY, series.data.y[i]);
        }
        EXPECT_GE(minY / 1.32, 1e-12); EXPECT_LE(maxY * 1.32, 1e-1);
        EXPECT_LT(minY, maxY);
        doc.view.viewport = sst::ui::plot::Bounds{-0.1, 3.1, minY / 1.32, maxY * 1.32};
        ASSERT_EQ(doc.Snapshot()->series.size(), 1);
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

    TEST(JFMProjectJobsTests, EnginePlotExportsAllPublicationFormatsThroughNanobind)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        auto runtime = EnsurePythonRuntimeForBridgeTests();
        ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;
        JFMPlotDocument doc;
        const double x[] = {0,1,2}, y[] = {1,2,3};
        doc.BeginFrame();
        doc.SetSeries({"Measurement###raw", {x,y,3}});
        doc.SetSeries({"Fit###fit", {x,y,3}});
        doc.EndFrame({});
        for (const char *format : {"svg", "pdf", "png"}) {
            auto destination = FileSystem::TempDirectoryPath() / (std::string("sst-jfm-j2.") + format);
            nlohmann::json request = {{"operation","export"},{"project",nlohmann::json::parse(doc.project.toJson())},
                {"graph_id",doc.graph},{"ranges",{-1,3,0,4}},{"width_mm",80},{"height_mm",60},
                {"dpi",150},{"format",format},{"path",Path::FromResolved(destination).Utf8()}};
            nlohmann::json result;
            {
            PythonGilAcquireScope scriptGuard; // Reproduce the PythonScriptJob guard around the binding.
            ASSERT_TRUE(scriptGuard.IsActive());
            const auto gil = PythonInterpreter::AcquireRuntimeGil();
            auto *json = PyImport_ImportModule("json");
            auto *module = PyImport_ImportModule("sst_jfm");
            ASSERT_NE(json, nullptr); ASSERT_NE(module, nullptr);
            auto *input = PyObject_CallMethod(json, "loads", "s", request.dump().c_str());
            auto *output = PyObject_CallMethod(module, "plot", "O", input);
            if (!output) PyErr_Print();
            ASSERT_NE(output, nullptr);
            auto *encoded = PyObject_CallMethod(json, "dumps", "O", output);
            result = nlohmann::json::parse(PyUnicode_AsUTF8(encoded));
            Py_DECREF(encoded); Py_DECREF(output); Py_DECREF(input); Py_DECREF(module); Py_DECREF(json);
            PythonInterpreter::ReleaseRuntimeGil(gil);
            }
            ASSERT_EQ(result.at("ok"), true) << result.dump();
            const auto id = result.at("job_id");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            do {
                result = RunJFMPlotRequest({{"operation","status"},{"job_id",id}});
                if (result.at("status") != "queued" && result.at("status") != "running") break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            } while (std::chrono::steady_clock::now() < deadline);
            ASSERT_EQ(result.at("ok"), true) << result.dump();
            ASSERT_EQ(result.at("status"), "completed") << result.dump();
            EXPECT_GT(std::filesystem::file_size(destination), 500);
            std::filesystem::remove(destination);
        }
#else
        FAIL() << "Embedded Python is required for publication exports";
#endif
    }

    TEST(JFMProjectJobsTests, HzbProjectOpensWithoutRewritingTheSource)
    {
        const Path path("Prototypes/JFM/JFM-projects/HZB-project.yaml.jfm.yaml");
        std::string original, after, error;
        ASSERT_TRUE(TextFileIO::Load(path, original, error)) << error;
        auto bus = CreateRef<EventBus>();
        std::optional<JFMProject> loaded;
        auto subscription = bus->Subscribe<ProjectLoadCompleted>([&](const ProjectLoadCompleted &e) { loaded = e.project; });
        auto context = MakeTestJobContext();
        LoadProjectJob job(bus, path);
        job.Execute(context); bus->ProcessQueue();
        ASSERT_TRUE(loaded.has_value());
        EXPECT_GT(loaded->characteristics.size(), 10);
        EXPECT_EQ(loaded->characteristics.front().name, "ivd_HZB25_T181_L0");
        EXPECT_TRUE(loaded->characteristics.front().prefit.has_value());
        EXPECT_TRUE(loaded->characteristics.front().fit.has_value());
        ASSERT_TRUE(TextFileIO::Load(path, after, error)) << error;
        EXPECT_EQ(original, after);
    }
} // namespace SST::Prototypes::JFM




namespace SST::Prototypes::JFM {
class UnicodePaths : public testing::Test {
protected:
    const std::filesystem::path name = std::filesystem::u8path(u8"Za\u017c\u00f3\u0142\u0107_\u65e5\u672c_\U0001f600");
    Path root;
    void SetUp() override {
        root = Path::FromResolved(FileSystem::TempDirectoryPath() / "sst-au2-jfm" / name);
        FileSystem::CreateDirectories(root.Native());
    }
    void TearDown() override {
        std::error_code ec;
        FileSystem::RemoveAll(root.Native(), ec);
    }
};
TEST_F(UnicodePaths, CoreConfigAndLayoutRoundtrip) {
    const Path configPath = root / "install/app/config";
    auto manager = CreateRef<ConfigManager>(configPath);
    std::string error, text;
    ASSERT_TRUE(manager->Initialize(error)) << error;
    auto config = manager->GetConfig();
    config.log.filePath = root / name / "log.txt";
    config.ui.fontPath = (root / name / "font.ttf").Utf8();
    ASSERT_TRUE(TextFileIO::Save(Path::FromResolved(std::filesystem::u8path(config.ui.fontPath)), "font", error));
    YamlConfigSerializer codec;
    ASSERT_TRUE(codec.SerializeDefaultConfig(config, text, error)) << error;
    ASSERT_TRUE(TextFileIO::Save(ConfigManager::GetDefaultConfigPath(configPath), text, error)) << error;
    ConfigManager reopened(configPath);
    ASSERT_TRUE(reopened.Initialize(error)) << error;
    EXPECT_EQ(reopened.GetConfig().log.filePath, config.log.filePath);
    const auto layout = Path::FromResolved(std::filesystem::u8path(reopened.GetConfig().layout.imGuiIniPath));
    ASSERT_TRUE(TextFileIO::Save(layout, "[Window][Unicode]\nPos=4,5\n", error)) << error;
    ASSERT_TRUE(TextFileIO::Load(layout, text, error)) << error;
    EXPECT_EQ(text, "[Window][Unicode]\nPos=4,5\n");
    EXPECT_EQ(reopened.ListIniFiles(layout.parent_path()), (std::vector<Path>{layout}));
    ConfigProfileStore profiles(manager);
    ASSERT_TRUE(profiles.Save(root.filename().Utf8(), config, error)) << error;
    ASSERT_EQ(profiles.Refresh().size(), 1);
    EXPECT_EQ(profiles.Entries()[0].name, root.filename().Utf8());
    ApplicationConfig loaded = config;
    ASSERT_TRUE(profiles.Load(profiles.Entries()[0].path, loaded, error)) << error;
    EXPECT_EQ(loaded.log.filePath, config.log.filePath);
    EXPECT_EQ(loaded.ui.fontPath, config.ui.fontPath);
}
TEST_F(UnicodePaths, CoreFontFileAndLogFile) {
    const auto source = FileSystem::CurrentPath() / "Prototypes/JFM/install/app/assets/fonts/DejaVuSans.ttf";
    ASSERT_TRUE(FileSystem::Exists(source));
    const auto font = root / (name.native() + std::filesystem::path(".ttf").native());
    std::filesystem::copy_file(source, font.Native(), std::filesystem::copy_options::overwrite_existing);
    const auto fonts = Platform::GetFontsFromDirectory(root, "Regression");
    ASSERT_EQ(fonts.size(), 1);
    EXPECT_EQ(fonts[0].path, font.Utf8());
    auto *context = ImGui::CreateContext();
    EXPECT_NE(ImGui::GetIO().Fonts->AddFontFromFileTTF(fonts[0].path.c_str(), 16), nullptr);
    ImGui::DestroyContext(context);
    LoggerOptions options;
    options.logToFile = true;
    options.logFilePath = root / (name.native() + std::filesystem::path(".log").native());
    Logger::Initialize(options);
    DS_LOG_INFO("au2-unicode-log-marker");
    Logger::Flush();
    Logger::Shutdown();
    std::string text, error;
    ASSERT_TRUE(TextFileIO::Load(options.logFilePath, text, error)) << error;
    EXPECT_NE(text.find("au2-unicode-log-marker"), std::string::npos);
}
TEST_F(UnicodePaths, JFMProjectAndBundleRoundtrip) {
    auto bus = CreateRef<EventBus>();
    JFMProject project;
    project.id = "unicode-project";
    project.name = root.filename().Utf8();
    JFMCharacteristicInfo info;
    info.id = 1; info.name = "curve";
    info.sourcePath = root / name / "source.dat";
    info.monteCarlo = JFMMonteCarloState{};
    info.monteCarlo->objective={"signed_sigma","A",0.,1e-5,.01,3.,2,0};
    info.monteCarlo->outputPath = root / name / "output";
    project.characteristics.push_back(info);
    project.scripts.push_back({"script", "custom", root / name / "custom.py", true, 0});
    const auto path = root / (name.native() + std::filesystem::path(".jfm.yaml").native());
    JFMProject loaded;
    bool completed = false;
    auto sub = bus->Subscribe<ProjectLoadCompleted>([&](const auto &event) { loaded = event.project; completed = true; });
    auto ctx = MakeTestJobContext();
    SaveProjectJob save(bus, path, project); save.Execute(ctx); bus->ProcessQueue();
    LoadProjectJob load(bus, path); load.Execute(ctx); bus->ProcessQueue();
    ASSERT_TRUE(completed);
    ASSERT_EQ(loaded.characteristics.size(), 1);
    EXPECT_EQ(loaded.characteristics[0].sourcePath, info.sourcePath);
    EXPECT_EQ(loaded.characteristics[0].monteCarlo->outputPath, info.monteCarlo->outputPath);
    EXPECT_EQ(loaded.scripts[0].path, project.scripts[0].path);
    JFMProjectBundleSnapshot snapshot; snapshot.project = project;
    JFMMonteCarloResult mc; mc.parameterNames = {"I0","A"}; mc.freeParameterCount = 2; mc.confidenceAvailable = false;
    JFMMonteCarloSample sample; sample.parameters = {1e-10,1.5}; sample.accepted = sample.converged = true;
    sample.solverConverged = false; sample.stopReason = "max_iter"; sample.fitMethod = "LM→NM";
    sample.lmStatus = 0; sample.flatDirections = true; sample.objectiveChiSquared = .125; mc.samples.push_back(sample);
    mc.voltage={0.,.1};mc.idealCurrent={-.03,.02};mc.currentSigma={.00123,.0006};mc.noisyCurrent={-.029,.021};
    snapshot.monteCarloResults.push_back({1,mc});
    const auto bundle = root / "bundle.jfm";
    auto saved = JFMProjectBundleSerializer::Save(bundle, snapshot);
    ASSERT_TRUE(saved) << saved.Error().technicalDetails;
    std::string exported, exportError;
    ASSERT_TRUE(TextFileIO::Load(bundle / Path("mc/1/parameters.tsv"), exported, exportError)) << exportError;
    EXPECT_NE(exported.find("\t0\t1\t0\tmax_iter\t"), std::string::npos);
    auto opened = JFMProjectBundleSerializer::Load(bundle);
    ASSERT_TRUE(opened) << opened.Error().technicalDetails;
    EXPECT_EQ(opened->snapshot.project.characteristics[0].monteCarlo->outputPath, bundle / Path("mc/1"));
    ASSERT_EQ(opened->snapshot.monteCarloResults.size(),1);
    const auto &roundtrip = opened->snapshot.monteCarloResults.front().result;
    EXPECT_EQ(roundtrip.currentSigma,mc.currentSigma); EXPECT_EQ(roundtrip.idealCurrent,mc.idealCurrent);
    EXPECT_EQ(opened->snapshot.project.characteristics[0].monteCarlo->objective.mode,"signed_sigma");
    EXPECT_DOUBLE_EQ(opened->snapshot.project.characteristics[0].monteCarlo->objective.sigmaAbsolute,1e-5);
    EXPECT_FALSE(roundtrip.confidenceAvailable); ASSERT_EQ(roundtrip.samples.size(),1);
    EXPECT_TRUE(roundtrip.samples.front().accepted); EXPECT_FALSE(roundtrip.samples.front().solverConverged);
    EXPECT_EQ(roundtrip.samples.front().stopReason,"max_iter");
    EXPECT_EQ(roundtrip.samples.front().fitMethod,"LM→NM"); EXPECT_EQ(roundtrip.samples.front().lmStatus,0);
    EXPECT_TRUE(roundtrip.samples.front().flatDirections); EXPECT_DOUBLE_EQ(roundtrip.samples.front().objectiveChiSquared,.125);
}
TEST_F(UnicodePaths, EmbeddedPythonPathAndImport) {
#if DS_PYTHON_CAPI_AVAILABLE
    auto runtime = EnsurePythonRuntimeForBridgeTests();
    ASSERT_TRUE(runtime) << runtime.Error().technicalDetails;
    const auto moduleRoot = root / name;
    std::string error;
    ASSERT_TRUE(TextFileIO::Save(moduleRoot / "au2_unicode_module.py", "value = 42\n", error)) << error;
    const auto source = FileSystem::CurrentPath() / "Prototypes/JFM/example-data/real-measurement/C002/IVT C002_Z015_6/IV_C002_Z015_6_300,0K.dat";
    const auto target = root / (name.native() + std::filesystem::path("_300K.dat").native());
    std::filesystem::copy_file(source, target.Native(), std::filesystem::copy_options::overwrite_existing);
    PythonGilAcquireScope gil;
    ASSERT_TRUE(gil.IsActive());
    {
        ASSERT_TRUE(ConfigureJFMPythonPaths(moduleRoot));
        EXPECT_EQ(nanobind::cast<int>(nanobind::module_::import_("au2_unicode_module").attr("value")), 42);
        auto info = nanobind::module_::import_("sst_jfm").attr("JFMCharacteristicInfo")();
        info.attr("source_path") = nanobind::str(target.Utf8().c_str());
        EXPECT_EQ(nanobind::cast<JFMCharacteristicInfo &>(info).sourcePath, target);
        EXPECT_EQ(nanobind::cast<std::string>(info.attr("source_path")), target.Utf8());
        JFMPythonImportBridge bridge(ResolveJFMPythonRoot(ApplicationConfig{}, Path::FromResolved(FileSystem::CurrentPath())));
        auto result = bridge.ImportFiles({target}, {});
        EXPECT_TRUE(result) << (result ? "" : result.Error().technicalDetails);
        if (result) {
            ASSERT_FALSE(result->characteristics.empty());
            EXPECT_EQ(result->characteristics[0].info.sourcePath, target);
        }
    }
#else
    GTEST_SKIP() << "Embedded Python unavailable";
#endif
}
    TEST(JFMProjectJobsTests, NanobindFitAcceptsJsonAndMethodKeyword)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        auto runtime=EnsurePythonRuntimeForBridgeTests(); ASSERT_TRUE(runtime);
        const auto gil=PythonInterpreter::AcquireRuntimeGil();
        const int status=PyRun_SimpleString(R"PY(
import json, math, sst_jfm
q = dict(voltage_V=[.1+i*.02 for i in range(30)], temperature_K=300.,
         model_id="SD-0", parameters=dict(I0_A=1e-10,ideality_factor=1.5))
q["current_A"]=[1e-10*math.exp(v/(1.5*1.380649e-23/1.602176634e-19*300.)) for v in q["voltage_V"]]
for method in ("lm", "nm"):
    r=sst_jfm.fit(q,method=method)
    assert r["ok"], r
    assert r["fit_method"] in (("LM", "LM→NM") if method == "lm" else ("NM",)), r
    assert "I0_A" in r["parameters"] and "ideality_factor" in r["parameters"], r
    json.dumps(r,allow_nan=False)

mc=sst_jfm.fit(dict(q,samples=2,noise_percent=0.,start_perturbation_percent=0.,
                   fixed_parameters=["I0_A"],bounds={"ideality_factor":[.5,3.]}),method="lm")
assert mc["ok"], mc
assert mc["parameter_names"] == ["I0_A","ideality_factor"], mc
assert all(sample["accepted"] for sample in mc["samples"]), mc
for mode in ("signed_sigma","isc_shift"):
    light=dict(q,model_id="SD-0-I_ph",parameters=dict(I0_A=1e-10,ideality_factor=1.5,Iph_A=.03),objective_mode=mode,sigma_abs_A=1e-5)
    light["voltage_V"]=[-.1+i*.02 for i in range(45)]
    light["current_A"]=[1e-10*math.exp(v/(1.5*1.380649e-23/1.602176634e-19*300.))-.03 for v in light["voltage_V"]]
    r=sst_jfm.fit(light)
    assert r["ok"], r
    assert r["objective"]["mode"]==mode and r["objective"]["sigma_abs_A"]==1e-5, r
    json.dumps(r,allow_nan=False)
json.dumps(mc,allow_nan=False)
density_q=dict(q,parameters=dict(I0_A_per_cm2=1e-10,ideality_factor=1.5))
density_q["current_density_A_per_cm2"]=density_q.pop("current_A")
wrong_units=sst_jfm.fit(dict(density_q,sigma_abs_A=1e-6))
assert not wrong_units["ok"] and wrong_units["error"]["code"]=="jfm.fit.validation", wrong_units
floor_density=sst_jfm.fit(dict(density_q,objective_mode="signed_sigma",sigma_abs_A_per_cm2=1e-6))
assert floor_density["ok"] and floor_density["objective"]["sigma_abs_A_per_cm2"]==1e-6, floor_density
json.dumps(floor_density,allow_nan=False)
density=sst_jfm.fit(dict(density_q,samples=2,noise_percent=0.,start_perturbation_percent=0.))
assert density["ok"], density
assert density["parameter_names"] == ["I0_A_per_cm2","ideality_factor"], density
assert density["ideal_current_density_A_per_cm2"] == mc["ideal_current_A"], density
assert "current_A" not in density and "noisy_current_A" not in density, density
json.dumps(density,allow_nan=False)

assert sst_jfm.fit(q,method="invalid")["error"]["code"] == "jfm.fit.validation"
bad=dict(q,parameters=dict(I0=1e-10,A=1.5))
assert sst_jfm.fit(bad)["error"]["code"] == "jfm.fit.validation"

import os, sys
if os.environ.get("SST_RUN_PYTEST"):
    sys.path.extend(json.loads(os.environ["SST_ROOT_PYTHON_PATHS"]))
    import pytest
    result = pytest.main([os.environ["SST_RUN_PYTEST"], "-q", "-p", "no:cacheprovider"])
    sys.stdout.flush(); sys.stderr.flush()
    assert result == 0

)PY");
        PythonInterpreter::ReleaseRuntimeGil(gil); EXPECT_EQ(status,0);
#endif
    }

}
