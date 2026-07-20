#include "Core/dspch.hpp"

#include "App/JFMUserSettings.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/Logging/Logger.hpp"

#include <algorithm>
#include <cmath>

#include <yaml-cpp/yaml.h>

namespace SST::Prototypes::JFM
{
	namespace UserSettingsDetail
	{
		template <typename T>
		T ReadOr(const YAML::Node &node, const char *key, T fallback)
		{
			const YAML::Node value = node[key];
			return value && !value.IsNull() ? value.as<T>() : fallback;
		}

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

		template <std::size_t Size>
		std::array<bool, Size> ReadFlags(const YAML::Node &node, std::array<bool, Size> fallback)
		{
			if (!node || !node.IsSequence() || node.size() < Size)
				return fallback;
			for (std::size_t index = 0; index < Size; ++index)
				fallback[index] = node[index].as<bool>();
			return fallback;
		}

		template <std::size_t Size>
		void EmitFlags(YAML::Emitter &out, const std::array<bool, Size> &flags)
		{
			out << YAML::Flow << YAML::BeginSeq;
			for (bool flag : flags)
				out << flag;
			out << YAML::EndSeq;
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
			m_Settings.showFitCurves = root["show_fit_curves"] ? root["show_fit_curves"].as<bool>() : m_Settings.showFitCurves;
			m_Settings.fitCurveColor = ReadColor(root["fit_curve_color"], m_Settings.fitCurveColor);
			m_Settings.fitCurveLineWeight = root["fit_curve_line_weight"] ? std::clamp(root["fit_curve_line_weight"].as<float>(), 0.5f, 8.0f) : m_Settings.fitCurveLineWeight;
			m_Settings.hoveredCurveLineWeight = root["hovered_curve_line_weight"] ? std::clamp(root["hovered_curve_line_weight"].as<float>(), 0.5f, 10.0f) : m_Settings.hoveredCurveLineWeight;
			m_Settings.autoPlotAfterImport = root["auto_plot_after_import"] ? root["auto_plot_after_import"].as<bool>() : m_Settings.autoPlotAfterImport;
			if (const YAML::Node fit = root["fit_monte_carlo"]; fit && fit.IsMap())
			{
				m_Settings.fitMonteCarlo.fitMaxIterations = std::max(1, ReadOr<int>(fit, "fit_max_iterations", m_Settings.fitMonteCarlo.fitMaxIterations));
				m_Settings.fitMonteCarlo.fitReducedChiSquaredTolerance = std::max(0.0, ReadOr<double>(fit, "fit_reduced_chi2_tolerance", m_Settings.fitMonteCarlo.fitReducedChiSquaredTolerance));
				m_Settings.fitMonteCarlo.monteCarloSamples = std::max(1, ReadOr<int>(fit, "mc_samples", m_Settings.fitMonteCarlo.monteCarloSamples));
				m_Settings.fitMonteCarlo.monteCarloMaxIterations = std::max(1, ReadOr<int>(fit, "mc_max_iterations", m_Settings.fitMonteCarlo.monteCarloMaxIterations));
				m_Settings.fitMonteCarlo.monteCarloNoisePercent = std::clamp(ReadOr<double>(fit, "mc_noise_percent", m_Settings.fitMonteCarlo.monteCarloNoisePercent), 0.0, 100.0);
				m_Settings.fitMonteCarlo.monteCarloStartPerturbationPercent = std::clamp(ReadOr<double>(fit, "mc_start_perturbation_percent", m_Settings.fitMonteCarlo.monteCarloStartPerturbationPercent), 0.0, 1000.0);
				m_Settings.fitMonteCarlo.monteCarloSeed = ReadOr<std::uint32_t>(fit, "mc_seed", m_Settings.fitMonteCarlo.monteCarloSeed);
				m_Settings.fitMonteCarlo.preferGpu = ReadOr<bool>(fit, "prefer_gpu", m_Settings.fitMonteCarlo.preferGpu);
				m_Settings.fitMonteCarlo.saveSamples = ReadOr<bool>(fit, "save_samples", m_Settings.fitMonteCarlo.saveSamples);
				m_Settings.fitMonteCarlo.savedSampleLimit = std::max(0, ReadOr<int>(fit, "saved_sample_limit", m_Settings.fitMonteCarlo.savedSampleLimit));
				m_Settings.fitMonteCarlo.scatterZoomOut = std::clamp(ReadOr<float>(fit, "scatter_zoom_out", m_Settings.fitMonteCarlo.scatterZoomOut), 0.0f, 1.0f);
				m_Settings.fitMonteCarlo.showSigmaClasses = ReadFlags(fit["show_sigma_classes"], m_Settings.fitMonteCarlo.showSigmaClasses);
				m_Settings.fitMonteCarlo.showSigmaEllipses = ReadFlags(fit["show_sigma_ellipses"], m_Settings.fitMonteCarlo.showSigmaEllipses);
				if (const YAML::Node colors = fit["sigma_colors"]; colors && colors.IsSequence())
				{
					for (std::size_t index = 0; index < m_Settings.fitMonteCarlo.sigmaColors.size() && index < colors.size(); ++index)
						m_Settings.fitMonteCarlo.sigmaColors[index] = ReadColor(colors[index], m_Settings.fitMonteCarlo.sigmaColors[index]);
				}
				if (const YAML::Node pairs = fit["default_parameter_pairs"]; pairs && pairs.IsSequence())
				{
					std::vector<JFMMonteCarloParameterPair> parsed;
					for (const YAML::Node &pair : pairs)
					{
						if (pair.IsSequence() && pair.size() >= 2)
							parsed.push_back({pair[0].as<std::string>(), pair[1].as<std::string>()});
					}
					if (!parsed.empty())
						m_Settings.fitMonteCarlo.defaultParameterPairs = std::move(parsed);
				}
				if (const YAML::Node parameters = fit["parameters"]; parameters && parameters.IsMap())
				{
					for (const auto &entry : parameters)
					{
						const std::string name = entry.first.as<std::string>();
						auto existing = m_Settings.fitMonteCarlo.parameters.find(name);
						JFMParameterSolverSetting setting = existing == m_Settings.fitMonteCarlo.parameters.end()
							? JFMParameterSolverSetting{}
							: existing->second;
						setting.minimum = ReadOr<double>(entry.second, "minimum", setting.minimum);
						setting.maximum = ReadOr<double>(entry.second, "maximum", setting.maximum);
						setting.monteCarloFree = ReadOr<bool>(entry.second, "free", setting.monteCarloFree);
						if (std::isfinite(setting.minimum) && std::isfinite(setting.maximum) && setting.minimum < setting.maximum)
							m_Settings.fitMonteCarlo.parameters[name] = setting;
					}
				}
			}
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
		out << YAML::Key << "show_fit_curves" << YAML::Value << snapshot.showFitCurves;
		out << YAML::Key << "fit_curve_color" << YAML::Value;
		EmitColor(out, snapshot.fitCurveColor);
		out << YAML::Key << "fit_curve_line_weight" << YAML::Value << snapshot.fitCurveLineWeight;
		out << YAML::Key << "hovered_curve_line_weight" << YAML::Value << snapshot.hoveredCurveLineWeight;
		out << YAML::Key << "auto_plot_after_import" << YAML::Value << snapshot.autoPlotAfterImport;
		out << YAML::Key << "fit_monte_carlo" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "fit_max_iterations" << YAML::Value << snapshot.fitMonteCarlo.fitMaxIterations;
		out << YAML::Key << "fit_reduced_chi2_tolerance" << YAML::Value << snapshot.fitMonteCarlo.fitReducedChiSquaredTolerance;
		out << YAML::Key << "mc_samples" << YAML::Value << snapshot.fitMonteCarlo.monteCarloSamples;
		out << YAML::Key << "mc_max_iterations" << YAML::Value << snapshot.fitMonteCarlo.monteCarloMaxIterations;
		out << YAML::Key << "mc_noise_percent" << YAML::Value << snapshot.fitMonteCarlo.monteCarloNoisePercent;
		out << YAML::Key << "mc_start_perturbation_percent" << YAML::Value << snapshot.fitMonteCarlo.monteCarloStartPerturbationPercent;
		out << YAML::Key << "mc_seed" << YAML::Value << snapshot.fitMonteCarlo.monteCarloSeed;
		out << YAML::Key << "prefer_gpu" << YAML::Value << snapshot.fitMonteCarlo.preferGpu;
		out << YAML::Key << "save_samples" << YAML::Value << snapshot.fitMonteCarlo.saveSamples;
		out << YAML::Key << "saved_sample_limit" << YAML::Value << snapshot.fitMonteCarlo.savedSampleLimit;
		out << YAML::Key << "scatter_zoom_out" << YAML::Value << snapshot.fitMonteCarlo.scatterZoomOut;
		out << YAML::Key << "show_sigma_classes" << YAML::Value;
		EmitFlags(out, snapshot.fitMonteCarlo.showSigmaClasses);
		out << YAML::Key << "show_sigma_ellipses" << YAML::Value;
		EmitFlags(out, snapshot.fitMonteCarlo.showSigmaEllipses);
		out << YAML::Key << "sigma_colors" << YAML::Value << YAML::BeginSeq;
		for (const auto &color : snapshot.fitMonteCarlo.sigmaColors)
			EmitColor(out, color);
		out << YAML::EndSeq;
		out << YAML::Key << "default_parameter_pairs" << YAML::Value << YAML::BeginSeq;
		for (const JFMMonteCarloParameterPair &pair : snapshot.fitMonteCarlo.defaultParameterPairs)
			out << YAML::Flow << YAML::BeginSeq << pair.x << pair.y << YAML::EndSeq;
		out << YAML::EndSeq;
		out << YAML::Key << "parameters" << YAML::Value << YAML::BeginMap;
		for (const auto &[name, setting] : snapshot.fitMonteCarlo.parameters)
		{
			out << YAML::Key << name << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "minimum" << YAML::Value << setting.minimum;
			out << YAML::Key << "maximum" << YAML::Value << setting.maximum;
			out << YAML::Key << "free" << YAML::Value << setting.monteCarloFree;
			out << YAML::EndMap;
		}
		out << YAML::EndMap << YAML::EndMap;
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

	void JFMUserSettingsStore::SetFitCurveDefaults(bool show, std::array<float, 4> color, float lineWeight)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.showFitCurves = show;
			m_Settings.fitCurveColor = color;
			m_Settings.fitCurveLineWeight = std::clamp(lineWeight, 0.5f, 8.0f);
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

	void JFMUserSettingsStore::SetFitMonteCarloSettings(JFMFitMonteCarloSettings settings)
	{
		settings.fitMaxIterations = std::max(1, settings.fitMaxIterations);
		settings.fitReducedChiSquaredTolerance = std::max(0.0, settings.fitReducedChiSquaredTolerance);
		settings.monteCarloSamples = std::max(1, settings.monteCarloSamples);
		settings.monteCarloMaxIterations = std::max(1, settings.monteCarloMaxIterations);
		settings.monteCarloNoisePercent = std::clamp(settings.monteCarloNoisePercent, 0.0, 100.0);
		settings.monteCarloStartPerturbationPercent = std::clamp(settings.monteCarloStartPerturbationPercent, 0.0, 1000.0);
		settings.savedSampleLimit = std::max(0, settings.savedSampleLimit);
		settings.scatterZoomOut = std::clamp(settings.scatterZoomOut, 0.0f, 1.0f);
		for (auto &[name, parameter] : settings.parameters)
		{
			(void)name;
			if (!std::isfinite(parameter.minimum) || !std::isfinite(parameter.maximum))
			{
				parameter.minimum = 0.0;
				parameter.maximum = 1.0;
			}
			else if (parameter.minimum > parameter.maximum)
				std::swap(parameter.minimum, parameter.maximum);
			if (parameter.minimum == parameter.maximum)
				parameter.maximum = std::nextafter(parameter.minimum, std::numeric_limits<double>::infinity());
		}
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.fitMonteCarlo = std::move(settings);
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
		settings.fitMonteCarlo.parameters = {
			{"I0", {1.0e-300, 1.0, true}},
			{"A", {0.1, 10.0, true}},
			{"Rs", {0.0, 1.0e9, true}},
			{"Rsh", {1.0e-6, 1.0e15, true}},
			{"alpha", {0.1, 10.0, true}},
			{"Rsh2", {1.0e-6, 1.0e15, true}},
			{"I_ph", {0.0, 1.0e6, true}}};
		return settings;
	}
} // namespace SST::Prototypes::JFM
