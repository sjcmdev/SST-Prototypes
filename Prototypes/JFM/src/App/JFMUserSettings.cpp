#include "Core/dspch.hpp"

#include "App/JFMUserSettings.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/Logging/Logger.hpp"

#include <algorithm>

#include <yaml-cpp/yaml.h>

namespace SST::Prototypes::JFM
{
	namespace UserSettingsDetail
	{
		Path ExistingOr(Path path, Path fallback)
		{
			if (path.Empty() || !FileSystem::Exists(path.Native()))
				return fallback;
			return path;
		}

		Path ReadPath(const YAML::Node &node, const char *key, Path fallback)
		{
			const YAML::Node value = node[key];
			if (!value || value.IsNull())
				return fallback;
			return ExistingOr(Path::FromResolved(value.as<std::string>()), fallback);
		}

		void EmitPath(YAML::Emitter &out, const char *key, const Path &path)
		{
			out << YAML::Key << key << YAML::Value << path.String();
		}

		std::array<float, 4> ReadColor(const YAML::Node &node, std::array<float, 4> fallback)
		{
			if (!node || !node.IsSequence() || node.size() < 4)
				return fallback;
			return {node[0].as<float>(), node[1].as<float>(), node[2].as<float>(), node[3].as<float>()};
		}

		void EmitColor(YAML::Emitter &out, const std::array<float, 4> &color)
		{
			out << YAML::Flow << YAML::BeginSeq << color[0] << color[1] << color[2] << color[3] << YAML::EndSeq;
		}

		bool HasPreset(const std::vector<JFMColorPreset> &presets, const std::string &name)
		{
			return std::any_of(presets.begin(), presets.end(), [&name](const JFMColorPreset &preset) {
				return preset.name == name;
			});
		}

		std::string FirstPresetName(const std::vector<JFMColorPreset> &presets)
		{
			return presets.empty() ? std::string{} : presets.front().name;
		}
	} // namespace UserSettingsDetail
	using namespace UserSettingsDetail;

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

	Path JFMUserSettingsStore::GetDefaultScriptsDirectory() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Settings.defaultScriptsDirectory;
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
			m_Settings.defaultScriptsDirectory = ReadPath(root, "default_scripts_directory", m_Settings.defaultScriptsDirectory);
			m_Settings.defaultPlotSemiLog = root["default_plot_semilog"] ? root["default_plot_semilog"].as<bool>() : m_Settings.defaultPlotSemiLog;
			m_Settings.defaultPlotAutoFit = root["default_plot_auto_fit"] ? root["default_plot_auto_fit"].as<bool>() : m_Settings.defaultPlotAutoFit;
			m_Settings.defaultPlotShowLegend = root["default_plot_show_legend"] ? root["default_plot_show_legend"].as<bool>() : m_Settings.defaultPlotShowLegend;
			m_Settings.defaultPlotFitPadding = root["default_plot_fit_padding"] ? root["default_plot_fit_padding"].as<float>() : m_Settings.defaultPlotFitPadding;
			m_Settings.showPrefitCurves = root["show_prefit_curves"] ? root["show_prefit_curves"].as<bool>() : m_Settings.showPrefitCurves;
			m_Settings.prefitCurveColor = ReadColor(root["prefit_curve_color"], m_Settings.prefitCurveColor);
			m_Settings.prefitCurveLineWeight = root["prefit_curve_line_weight"] ? std::clamp(root["prefit_curve_line_weight"].as<float>(), 0.5f, 8.0f) : m_Settings.prefitCurveLineWeight;
			m_Settings.prefitCurvesFitRangeOnly = root["prefit_curves_fit_range_only"] ? root["prefit_curves_fit_range_only"].as<bool>() : m_Settings.prefitCurvesFitRangeOnly;
			m_Settings.hoveredCurveLineWeight = root["hovered_curve_line_weight"] ? std::clamp(root["hovered_curve_line_weight"].as<float>(), 0.5f, 10.0f) : m_Settings.hoveredCurveLineWeight;
			m_Settings.autoPlotAfterImport = root["auto_plot_after_import"] ? root["auto_plot_after_import"].as<bool>() : m_Settings.autoPlotAfterImport;
			m_Settings.defaultColorPresetName = root["default_color_preset"] ? root["default_color_preset"].as<std::string>() : m_Settings.defaultColorPresetName;
			if (const YAML::Node presets = root["color_presets"])
			{
				m_Settings.colorPresets.clear();
				for (const YAML::Node &presetNode : presets)
				{
					JFMColorPreset preset;
					preset.name = presetNode["name"] ? presetNode["name"].as<std::string>() : "Preset";
					preset.start = ReadColor(presetNode["start"], preset.start);
					preset.end = ReadColor(presetNode["end"], preset.end);
					m_Settings.colorPresets.push_back(std::move(preset));
				}
			}
			if (!HasPreset(m_Settings.colorPresets, m_Settings.defaultColorPresetName))
				m_Settings.defaultColorPresetName = FirstPresetName(m_Settings.colorPresets);
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
		EmitPath(out, "default_scripts_directory", snapshot.defaultScriptsDirectory);
		out << YAML::Key << "default_plot_semilog" << YAML::Value << snapshot.defaultPlotSemiLog;
		out << YAML::Key << "default_plot_auto_fit" << YAML::Value << snapshot.defaultPlotAutoFit;
		out << YAML::Key << "default_plot_show_legend" << YAML::Value << snapshot.defaultPlotShowLegend;
		out << YAML::Key << "default_plot_fit_padding" << YAML::Value << snapshot.defaultPlotFitPadding;
		out << YAML::Key << "show_prefit_curves" << YAML::Value << snapshot.showPrefitCurves;
		out << YAML::Key << "prefit_curve_color" << YAML::Value;
		EmitColor(out, snapshot.prefitCurveColor);
		out << YAML::Key << "prefit_curve_line_weight" << YAML::Value << snapshot.prefitCurveLineWeight;
		out << YAML::Key << "prefit_curves_fit_range_only" << YAML::Value << snapshot.prefitCurvesFitRangeOnly;
		out << YAML::Key << "hovered_curve_line_weight" << YAML::Value << snapshot.hoveredCurveLineWeight;
		out << YAML::Key << "auto_plot_after_import" << YAML::Value << snapshot.autoPlotAfterImport;
		out << YAML::Key << "default_color_preset" << YAML::Value << snapshot.defaultColorPresetName;
		out << YAML::Key << "color_presets" << YAML::Value << YAML::BeginSeq;
		for (const JFMColorPreset &preset : snapshot.colorPresets)
		{
			out << YAML::BeginMap;
			out << YAML::Key << "name" << YAML::Value << preset.name;
			out << YAML::Key << "start" << YAML::Value;
			EmitColor(out, preset.start);
			out << YAML::Key << "end" << YAML::Value;
			EmitColor(out, preset.end);
			out << YAML::EndMap;
		}
		out << YAML::EndSeq;
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

	void JFMUserSettingsStore::SetDefaultScriptsDirectory(Path path)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.defaultScriptsDirectory = std::move(path);
		}
		Save();
	}

	void JFMUserSettingsStore::SetPlotDefaults(bool semiLog, bool autoFit, bool showLegend, float fitPadding)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.defaultPlotSemiLog = semiLog;
			m_Settings.defaultPlotAutoFit = autoFit;
			m_Settings.defaultPlotShowLegend = showLegend;
			m_Settings.defaultPlotFitPadding = std::clamp(fitPadding, 0.0f, 0.5f);
		}
		Save();
	}

	void JFMUserSettingsStore::SetPrefitCurveDefaults(bool show, std::array<float, 4> color, float lineWeight, bool fitRangeOnly, float hoverLineWeight)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.showPrefitCurves = show;
			m_Settings.prefitCurveColor = color;
			m_Settings.prefitCurveLineWeight = std::clamp(lineWeight, 0.5f, 8.0f);
			m_Settings.prefitCurvesFitRangeOnly = fitRangeOnly;
			m_Settings.hoveredCurveLineWeight = std::clamp(hoverLineWeight, 0.5f, 10.0f);
		}
		Save();
	}

	void JFMUserSettingsStore::SetAutoPlotAfterImport(bool enabled)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.autoPlotAfterImport = enabled;
		}
		Save();
	}

	void JFMUserSettingsStore::SetColorPresets(std::vector<JFMColorPreset> presets)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.colorPresets = std::move(presets);
			if (!HasPreset(m_Settings.colorPresets, m_Settings.defaultColorPresetName))
				m_Settings.defaultColorPresetName = FirstPresetName(m_Settings.colorPresets);
		}
		Save();
	}

	void JFMUserSettingsStore::SetDefaultColorPreset(std::string presetName)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.defaultColorPresetName = HasPreset(m_Settings.colorPresets, presetName)
				? std::move(presetName)
				: FirstPresetName(m_Settings.colorPresets);
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
		settings.defaultScriptsDirectory = prototypeRoot / Path("python") / Path("jfm_import");
		settings.colorPresets = {
			JFMColorPreset{"Cyan/Pink", {0.15f, 0.75f, 1.00f, 1.00f}, {1.00f, 0.12f, 0.55f, 1.00f}},
			JFMColorPreset{"Blue/Orange", {0.22f, 0.45f, 1.00f, 1.00f}, {1.00f, 0.55f, 0.05f, 1.00f}},
			JFMColorPreset{"Green/Yellow", {0.10f, 0.85f, 0.35f, 1.00f}, {1.00f, 0.90f, 0.15f, 1.00f}},
			JFMColorPreset{"Violet/Red", {0.58f, 0.20f, 0.95f, 1.00f}, {1.00f, 0.18f, 0.18f, 1.00f}},
		};
		settings.defaultColorPresetName = settings.colorPresets.front().name;
		return settings;
	}
} // namespace SST::Prototypes::JFM
