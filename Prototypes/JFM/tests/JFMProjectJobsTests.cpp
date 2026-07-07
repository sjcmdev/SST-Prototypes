#include <gtest/gtest.h>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMImportJob.hpp"
#include "Storage/JFMProjectJobs.hpp"

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
} // namespace SST::Prototypes::JFM
