#include <gtest/gtest.h>

#include "App/JFMUserSettings.hpp"

namespace SST::Prototypes::JFM
{
	TEST(JFMUserSettingsTests, RoundTripsUserSettingsYaml)
	{
		const Path root = Path::FromResolved(FileSystem::TempDirectoryPath() / "sst-jfm-settings-test-root" / std::filesystem::u8path(u8"Za\u017c\u00f3\u0142\u0107_\u65e5\u672c_\U0001f600"));
		const Path config = root / Path("config");
		const Path prototype = root / Path("Prototypes") / Path("JFM");
		(void)FileSystem::CreateDirectories((prototype / Path("example-data")).Native());
		(void)FileSystem::CreateDirectories((prototype / Path("example-data") / Path("nested")).Native());
		(void)FileSystem::CreateDirectories((prototype / Path("JFM-projects")).Native());
		(void)FileSystem::CreateDirectories((prototype / Path("python") / Path("custom_scripts")).Native());

		JFMUserSettingsStore store(config, prototype);
		store.SetDefaultImportDirectory(prototype / Path("example-data") / Path("nested"));
		store.SetDefaultProjectsDirectory(prototype / Path("JFM-projects"));
		store.SetDefaultScriptsDirectory(prototype / Path("python") / Path("custom_scripts"));
		store.SetDefaultColorPreset("Green/Yellow");
		store.SetPrefitCurveDefaults(false, {0.1f, 0.2f, 0.3f, 1.0f}, 3.5f, true, 5.0f);
		store.SetFitCurveDefaults(false, {0.4f, 0.5f, 0.6f, 1.0f}, 4.5f, true);
		store.SetTunedCurveDefaults(true, {0.7f, 0.8f, 0.9f, 1.0f}, 5.5f, true);
		store.SetAutoRangeAnalysis(true, false);
		JFMFitMonteCarloSettings solverSettings = store.GetSnapshot().fitMonteCarlo;
		solverSettings.monteCarloSamples = 2048;
		solverSettings.monteCarloNoisePercent = 2.5;
        solverSettings.objectiveMode="signed_sigma";solverSettings.objectiveSwitchK=4.;solverSettings.sigmaAbsolute=1e-8;
		solverSettings.monteCarloStartPerturbationPercent = 12.0;
		solverSettings.saveSamples = false;
		solverSettings.savedSampleLimit = 321;
		solverSettings.sigmaColors[2] = {0.2f, 0.3f, 0.4f, 0.9f};
		solverSettings.showSigmaClasses = {true, false, true};
		solverSettings.showSigmaEllipses = {false, true, false};
		solverSettings.scatterZoomOut = 0.23f;
		solverSettings.defaultParameterPairs = {{"Rs", "Rsh"}, {"A", "I0"}};
		solverSettings.parameters["Rs"] = {0.01, 50.0, false};
		store.SetFitMonteCarloSettings(std::move(solverSettings));
		store.AddRecentProject(prototype / Path("JFM-projects") / Path("one.jfm.yaml"));

		JFMUserSettingsStore loaded(config, prototype);
		loaded.Load();
		const JFMUserSettings settings = loaded.GetSnapshot();

		EXPECT_EQ(settings.defaultImportDirectory, prototype / Path("example-data") / Path("nested"));
		EXPECT_EQ(settings.defaultScriptsDirectory, prototype / Path("python") / Path("custom_scripts"));
		EXPECT_FALSE(settings.colorPresets.empty());
		EXPECT_TRUE(settings.defaultPlotSemiLog);
		EXPECT_FALSE(settings.showPrefitCurves);
		EXPECT_FLOAT_EQ(settings.prefitCurveColor[1], 0.2f);
		EXPECT_FLOAT_EQ(settings.prefitCurveLineWeight, 3.5f);
		EXPECT_TRUE(settings.prefitCurvesFitRangeOnly);
		EXPECT_FALSE(settings.showFitCurves);
		EXPECT_FLOAT_EQ(settings.fitCurveColor[1], 0.5f);
		EXPECT_FLOAT_EQ(settings.fitCurveLineWeight, 4.5f);
		EXPECT_TRUE(settings.fitCurvesFitRangeOnly);
		EXPECT_TRUE(settings.showTunedCurves);
		EXPECT_FLOAT_EQ(settings.tunedCurveColor[1], 0.8f);
		EXPECT_FLOAT_EQ(settings.tunedCurveLineWeight, 5.5f);
		EXPECT_TRUE(settings.tunedCurvesFitRangeOnly);
		EXPECT_TRUE(settings.autoPrefitOnRangeChange);
		EXPECT_FALSE(settings.autoFitOnRangeChange);
		EXPECT_FALSE(settings.autoPrefitFitOnRangeChange);
		EXPECT_FLOAT_EQ(settings.hoveredCurveLineWeight, 5.0f);
		EXPECT_EQ(settings.defaultColorPresetName, "Green/Yellow");
		EXPECT_EQ(settings.fitMonteCarlo.monteCarloSamples, 2048);
		EXPECT_DOUBLE_EQ(settings.fitMonteCarlo.monteCarloNoisePercent, 2.5);
        EXPECT_EQ(settings.fitMonteCarlo.objectiveMode,"signed_sigma");EXPECT_DOUBLE_EQ(settings.fitMonteCarlo.objectiveSwitchK,4.);EXPECT_DOUBLE_EQ(settings.fitMonteCarlo.sigmaAbsolute,1e-8);
		EXPECT_DOUBLE_EQ(settings.fitMonteCarlo.monteCarloStartPerturbationPercent, 12.0);
		EXPECT_FALSE(settings.fitMonteCarlo.saveSamples);
		EXPECT_EQ(settings.fitMonteCarlo.savedSampleLimit, 321);
		EXPECT_FLOAT_EQ(settings.fitMonteCarlo.sigmaColors[2][2], 0.4f);
		EXPECT_FALSE(settings.fitMonteCarlo.showSigmaClasses[1]);
		EXPECT_TRUE(settings.fitMonteCarlo.showSigmaEllipses[1]);
		EXPECT_FLOAT_EQ(settings.fitMonteCarlo.scatterZoomOut, 0.23f);
		ASSERT_EQ(settings.fitMonteCarlo.defaultParameterPairs.size(), 2);
		EXPECT_EQ(settings.fitMonteCarlo.defaultParameterPairs.front().x, "Rs");
		EXPECT_FALSE(settings.fitMonteCarlo.parameters.at("Rs").monteCarloFree);
		ASSERT_EQ(settings.recentProjects.size(), 1);
		EXPECT_EQ(settings.recentProjects.front(), prototype / Path("JFM-projects") / Path("one.jfm.yaml"));

		std::error_code error;
		(void)FileSystem::RemoveAll(root.Native(), error);
	}
} // namespace SST::Prototypes::JFM
