#pragma once

#include <mutex>
#include <vector>

#include "Core/Utils/Path.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMUserSettings
	{
		Path defaultImportDirectory;
		Path defaultProjectsDirectory;
		Path defaultScriptsDirectory;
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
