#include <gtest/gtest.h>

#include <algorithm>
#include <future>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMImportJob.hpp"
#include "IO/JFMImportPaths.hpp"
#include "IO/JFMProjectJobs.hpp"
#include "IO/JFMPythonBindings.hpp"
#include "IO/JFMPythonImportBridge.hpp"
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
		project.series.push_back(JFMSeries{"series-1", "Series", {}});

		bool saveCompleted = false;
		bool loadCompleted = false;
		JFMProject loadedProject;
		auto saveSub = bus->Subscribe<ProjectSaveCompleted>([&saveCompleted](const ProjectSaveCompleted &) {
			saveCompleted = true;
		});
		auto loadSub = bus->Subscribe<ProjectLoadCompleted>([&](const ProjectLoadCompleted &event) {
			loadCompleted = true;
			loadedProject = event.project;
		});

		JobContext context = MakeTestJobContext();
		SaveProjectJob saveJob(bus, path, project);
		saveJob.Execute(context);
		bus->ProcessQueue();

		ASSERT_TRUE(saveCompleted);
		ASSERT_TRUE(FileSystem::Exists(path.Native()));

		LoadProjectJob loadJob(bus, path);
		loadJob.Execute(context);
		bus->ProcessQueue();

		ASSERT_TRUE(loadCompleted);
		EXPECT_EQ(loadedProject.name, "Job Project");
		(void)saveSub;
		(void)loadSub;
		(void)FileSystem::Remove(path.Native());
	}

	TEST(JFMProjectJobsTests, ImportJobPublishesProgressAndTerminalEvent)
	{
		auto bus = CreateRef<EventBus>();
		bool sawProgress = false;
		bool sawTerminalEvent = false;
		auto progressSub = bus->Subscribe<ImportProgress>([&sawProgress](const ImportProgress &) {
			sawProgress = true;
		});
		auto failedSub = bus->Subscribe<ImportFailed>([&sawTerminalEvent](const ImportFailed &) {
			sawTerminalEvent = true;
		});
		auto completedSub = bus->Subscribe<ImportCompleted>([&sawTerminalEvent](const ImportCompleted &) {
			sawTerminalEvent = true;
		});

		JobContext context = MakeTestJobContext();
		JFMImportJob job(bus, {Path::FromResolved("missing.dat")}, Path::FromResolved("missing-python-root"));
		job.Execute(context);
		bus->ProcessQueue();

		EXPECT_TRUE(sawProgress);
		EXPECT_TRUE(sawTerminalEvent);
		(void)progressSub;
		(void)failedSub;
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
		const Path dataPath = repoRoot / Path("Prototypes") / Path("JFM") / Path("example-data") / Path("IV_C002_Z015_6_290,0K.dat");
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
		const Path dataPath = repoRoot / Path("Prototypes") / Path("JFM") / Path("example-data") / Path("IV_C002_Z015_6_290,0K.dat");

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
} // namespace SST::Prototypes::JFM
