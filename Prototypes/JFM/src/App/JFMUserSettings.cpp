#include "Core/dspch.hpp"

#include "App/JFMUserSettings.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/Logging/Logger.hpp"

#include <algorithm>

#include <yaml-cpp/yaml.h>

namespace SST::Prototypes::JFM
{
	namespace
	{
		Path ReadPath(const YAML::Node &node, const char *key, Path fallback)
		{
			const YAML::Node value = node[key];
			if (!value || value.IsNull())
				return fallback;
			return Path::FromResolved(value.as<std::string>());
		}

		void EmitPath(YAML::Emitter &out, const char *key, const Path &path)
		{
			out << YAML::Key << key << YAML::Value << path.String();
		}
	}

	JFMUserSettingsStore::JFMUserSettingsStore(Path configDirectory, Path prototypeRoot)
		: m_ConfigDirectory(std::move(configDirectory)),
		  m_PrototypeRoot(prototypeRoot.Empty() ? ResolvePrototypeRoot(Path::FromResolved(FileSystem::CurrentPath())) : std::move(prototypeRoot)),
		  m_Settings(MakeDefaults(m_PrototypeRoot))
	{
	}

	JFMUserSettings JFMUserSettingsStore::GetSnapshot() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Settings;
	}

	Path JFMUserSettingsStore::GetSettingsPath() const
	{
		return m_ConfigDirectory / Path("jfm_user_settings.yaml");
	}

	Path JFMUserSettingsStore::GetDefaultImportDirectory() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Settings.defaultImportDirectory;
	}

	Path JFMUserSettingsStore::GetDefaultProjectsDirectory() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Settings.defaultProjectsDirectory;
	}

	std::vector<Path> JFMUserSettingsStore::GetRecentProjects() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Settings.recentProjects;
	}

	void JFMUserSettingsStore::Load()
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(GetSettingsPath(), text, error))
			return;

		try
		{
			const YAML::Node root = YAML::Load(text);
			std::scoped_lock lock(m_Mutex);
			m_Settings.defaultImportDirectory = ReadPath(root, "default_import_directory", m_Settings.defaultImportDirectory);
			m_Settings.defaultProjectsDirectory = ReadPath(root, "default_projects_directory", m_Settings.defaultProjectsDirectory);
			m_Settings.recentProjects.clear();
			if (const YAML::Node recent = root["recent_projects"])
			{
				for (const YAML::Node &entry : recent)
					m_Settings.recentProjects.push_back(Path::FromResolved(entry.as<std::string>()));
			}
		}
		catch (const std::exception &exception)
		{
			DS_LOG_WARN("JFM user settings load failed: {}", exception.what());
		}
	}

	void JFMUserSettingsStore::Save() const
	{
		JFMUserSettings snapshot = GetSnapshot();
		YAML::Emitter out;
		out << YAML::BeginMap;
		EmitPath(out, "default_import_directory", snapshot.defaultImportDirectory);
		EmitPath(out, "default_projects_directory", snapshot.defaultProjectsDirectory);
		out << YAML::Key << "recent_projects" << YAML::Value << YAML::BeginSeq;
		for (const Path &path : snapshot.recentProjects)
			out << path.String();
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::error_code directoryError;
		(void)FileSystem::CreateDirectories(m_ConfigDirectory.Native(), directoryError);
		std::string error;
		if (!TextFileIO::Save(GetSettingsPath(), out.c_str(), error))
			DS_LOG_WARN("JFM user settings save failed: {}", error);
	}

	void JFMUserSettingsStore::SetDefaultImportDirectory(Path path)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.defaultImportDirectory = std::move(path);
		}
		Save();
	}

	void JFMUserSettingsStore::SetDefaultProjectsDirectory(Path path)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.defaultProjectsDirectory = std::move(path);
		}
		Save();
	}

	void JFMUserSettingsStore::AddRecentProject(Path path)
	{
		if (path.Empty())
			return;

		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.recentProjects.erase(
				std::remove(m_Settings.recentProjects.begin(), m_Settings.recentProjects.end(), path),
				m_Settings.recentProjects.end());
			m_Settings.recentProjects.insert(m_Settings.recentProjects.begin(), std::move(path));
			if (m_Settings.recentProjects.size() > 10)
				m_Settings.recentProjects.resize(10);
		}
		Save();
	}

	Path JFMUserSettingsStore::ResolvePrototypeRoot(Path start)
	{
		for (int depth = 0; depth < 8 && !start.Empty(); ++depth)
		{
			if (FileSystem::Exists((start / Path("example-data")).Native()) && FileSystem::Exists((start / Path("JFM-projects")).Native()))
				return start;
			const Path candidate = start / Path("Prototypes") / Path("JFM");
			if (FileSystem::Exists((candidate / Path("example-data")).Native()))
				return candidate;
			const Path parent = start.parent_path();
			if (parent == start)
				break;
			start = parent;
		}
		return Path::FromResolved(FileSystem::CurrentPath());
	}

	JFMUserSettings JFMUserSettingsStore::MakeDefaults(const Path &prototypeRoot)
	{
		JFMUserSettings settings;
		settings.defaultImportDirectory = prototypeRoot / Path("example-data");
		settings.defaultProjectsDirectory = prototypeRoot / Path("JFM-projects");
		return settings;
	}
} // namespace SST::Prototypes::JFM
