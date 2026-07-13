#include <gtest/gtest.h>

#include "App/JFMUserSettings.hpp"

namespace SST::Prototypes::JFM
{
	TEST(JFMUserSettingsTests, RoundTripsUserSettingsYaml)
	{
		const Path root = Path::FromResolved(FileSystem::TempDirectoryPath() / "sst-jfm-settings-test-root");
		const Path config = root / Path("config");
		const Path prototype = root / Path("Prototypes") / Path("JFM");
		(void)FileSystem::CreateDirectories((prototype / Path("example-data")).Native());
		(void)FileSystem::CreateDirectories((prototype / Path("JFM-projects")).Native());

		JFMUserSettingsStore store(config, prototype);
		store.SetDefaultImportDirectory(prototype / Path("example-data") / Path("nested"));
		store.SetDefaultProjectsDirectory(prototype / Path("JFM-projects"));
		store.SetDefaultScriptsDirectory(prototype / Path("python") / Path("custom_scripts"));
		store.AddRecentProject(prototype / Path("JFM-projects") / Path("one.jfm.yaml"));

		JFMUserSettingsStore loaded(config, prototype);
		loaded.Load();
		const JFMUserSettings settings = loaded.GetSnapshot();

		EXPECT_EQ(settings.defaultImportDirectory, prototype / Path("example-data") / Path("nested"));
		EXPECT_EQ(settings.defaultScriptsDirectory, prototype / Path("python") / Path("custom_scripts"));
		ASSERT_EQ(settings.recentProjects.size(), 1);
		EXPECT_EQ(settings.recentProjects.front(), prototype / Path("JFM-projects") / Path("one.jfm.yaml"));

		std::error_code error;
		(void)FileSystem::RemoveAll(root.Native(), error);
	}
} // namespace SST::Prototypes::JFM
