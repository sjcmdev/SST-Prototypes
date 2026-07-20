#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMColorPreset
	{
		std::string name;
		std::array<float, 4> start = {0.15f, 0.75f, 1.00f, 1.00f};
		std::array<float, 4> end = {1.00f, 0.12f, 0.55f, 1.00f};
	};

	struct JFMParameterSolverSetting
	{
		double minimum = 0.0;
		double maximum = 1.0;
		bool monteCarloFree = true;
	};

	struct JFMMonteCarloParameterPair
	{
		std::string x;
		std::string y;
	};

	struct JFMFitMonteCarloSettings
	{
		int fitMaxIterations = 2500;
		double fitReducedChiSquaredTolerance = 1.0;
		int monteCarloSamples = 512;
		int monteCarloMaxIterations = 1500;
		double monteCarloNoisePercent = 1.0;
		double monteCarloStartPerturbationPercent = 5.0;
		std::uint32_t monteCarloSeed = 42;
		bool preferGpu = true;
		bool saveSamples = true;
		int savedSampleLimit = 0;
		std::array<std::array<float, 4>, 3> sigmaColors = {{
			{1.00f, 0.12f, 0.08f, 1.00f},
			{1.00f, 0.48f, 0.06f, 1.00f},
			{0.05f, 0.85f, 0.95f, 1.00f}}};
		std::array<bool, 3> showSigmaClasses = {true, true, true};
		std::array<bool, 3> showSigmaEllipses = {true, true, true};
		float scatterZoomOut = 0.08f;
		std::vector<JFMMonteCarloParameterPair> defaultParameterPairs = {{"i0", "a"}, {"RsH", "Rs"}};
		std::unordered_map<std::string, JFMParameterSolverSetting> parameters;
	};

	struct JFMUserSettings
	{
		Path defaultImportDirectory;
		Path defaultProjectsDirectory;
		Path defaultScriptsDirectory;
		std::vector<JFMColorPreset> colorPresets;
		std::string defaultColorPresetName;
		bool defaultPlotSemiLog = true;
		bool defaultPlotAutoFit = true;
		bool defaultPlotShowLegend = false;
		float defaultPlotFitPadding = 0.08f;
		bool showPrefitCurves = true;
		std::array<float, 4> prefitCurveColor = {0.20f, 0.85f, 0.36f, 1.00f};
		float prefitCurveLineWeight = 2.0f;
		bool prefitCurvesFitRangeOnly = false;
		bool showFitCurves = true;
		std::array<float, 4> fitCurveColor = {0.12f, 0.58f, 1.00f, 1.00f};
		float fitCurveLineWeight = 2.0f;
		bool fitCurvesFitRangeOnly = false;
		bool showTunedCurves = false;
		std::array<float, 4> tunedCurveColor = {1.00f, 0.48f, 0.06f, 1.00f};
		float tunedCurveLineWeight = 2.0f;
		bool tunedCurvesFitRangeOnly = false;
		bool autoPrefitFitOnRangeChange = false;
		float hoveredCurveLineWeight = 3.5f;
		bool autoPlotAfterImport = false;
		JFMFitMonteCarloSettings fitMonteCarlo;
		std::vector<Path> recentProjects;
	};

	class JFMUserSettingsStore
	{
	public:
		explicit JFMUserSettingsStore(Path configDirectory, Path prototypeRoot = {});

		[[nodiscard]] JFMUserSettings GetSnapshot() const;
		[[nodiscard]] Path GetSettingsPath() const;
		[[nodiscard]] Path GetDefaultImportDirectory() const;
		[[nodiscard]] Path GetDefaultProjectsDirectory() const;
		[[nodiscard]] Path GetDefaultScriptsDirectory() const;
		[[nodiscard]] std::vector<Path> GetRecentProjects() const;

		void Load();
		void Save() const;
		void SetDefaultImportDirectory(Path path);
		void SetDefaultProjectsDirectory(Path path);
		void SetDefaultScriptsDirectory(Path path);
		void SetPlotDefaults(bool semiLog, bool autoFit, bool showLegend, float fitPadding);
		void SetPrefitCurveDefaults(bool show, std::array<float, 4> color, float lineWeight, bool fitRangeOnly, float hoverLineWeight);
		void SetFitCurveDefaults(bool show, std::array<float, 4> color, float lineWeight, bool fitRangeOnly);
		void SetTunedCurveDefaults(bool show, std::array<float, 4> color, float lineWeight, bool fitRangeOnly);
		void SetAutoPrefitFitOnRangeChange(bool enabled);
		void SetAutoPlotAfterImport(bool enabled);
		void SetFitMonteCarloSettings(JFMFitMonteCarloSettings settings);
		void SetColorPresets(std::vector<JFMColorPreset> presets);
		void SetDefaultColorPreset(std::string presetName);
		void AddRecentProject(Path path);

	private:
		[[nodiscard]] static Path ResolvePrototypeRoot(Path start);
		[[nodiscard]] static JFMUserSettings MakeDefaults(const Path &prototypeRoot);

	private:
		mutable std::mutex m_Mutex;
		Path m_ConfigDirectory;
		Path m_PrototypeRoot;
		JFMUserSettings m_Settings;
	};
} // namespace SST::Prototypes::JFM
