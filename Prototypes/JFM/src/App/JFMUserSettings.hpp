#pragma once

#include <array>
#include <mutex>
#include <string>
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

	struct JFMUserSettings
	{
		Path defaultImportDirectory;
		Path defaultProjectsDirectory;
		Path defaultScriptsDirectory;
		std::vector<JFMColorPreset> colorPresets;
		bool defaultPlotSemiLog = true;
		bool defaultPlotAutoFit = true;
		bool defaultPlotShowLegend = false;
		float defaultPlotFitPadding = 0.08f;
		bool autoPlotAfterImport = false;
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
		void SetAutoPlotAfterImport(bool enabled);
		void SetColorPresets(std::vector<JFMColorPreset> presets);
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
