#include "Core/dspch.hpp"

#include "App/JFMUserSettings.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/IO/RecentFiles.hpp"
#include "Core/Logging/Logger.hpp"

#include <algorithm>
#include <cmath>

#include <yaml-cpp/yaml.h>

namespace SST::Prototypes::JFM
{
	namespace UserSettingsDetail
	{
        template<class T> T RequireMinimum(T minimum,T value) {
            if(!std::isfinite(static_cast<double>(value)) || value<minimum) throw std::invalid_argument("JFM numeric setting is below its minimum or is not finite. Correct the value; settings were not applied.");
            return value;
        }
        template<class T> T RequireRange(T value,T minimum,T maximum) {
            if(!std::isfinite(static_cast<double>(value)) || value<minimum || value>maximum) throw std::invalid_argument("JFM numeric setting is outside its allowed range. Correct the value; settings were not applied.");
            return value;
        }
        void ValidateFitSettings(const JFMFitMonteCarloSettings& s) {
            if(s.fitMethod!="lm" && s.fitMethod!="nm") throw std::invalid_argument("fit_method must be lm or nm.");
            if(s.objectiveMode!="auto" && s.objectiveMode!="isc_shift" && s.objectiveMode!="signed_sigma" && s.objectiveMode!="log_legacy") throw std::invalid_argument("Unknown objective_mode. Choose auto, isc_shift, signed_sigma or log_legacy.");
            if(!std::isfinite(s.objectiveSwitchK) || s.objectiveSwitchK<=0 || !std::isfinite(s.sigmaAbsolute) || s.sigmaAbsolute<0) throw std::invalid_argument("objective_switch_k must be positive and sigma_absolute nonnegative; both must be finite.");
            RequireMinimum(1,s.fitMaxIterations); RequireMinimum(0.,s.fitReducedChiSquaredTolerance);
            RequireMinimum(1,s.monteCarloSamples); RequireMinimum(1,s.monteCarloMaxIterations);
            RequireMinimum(1,s.simplexCalibrationRuns); RequireMinimum(0,s.simplexCalibrationExtraIterations);
            RequireRange(s.monteCarloNoisePercent,0.,100.); RequireRange(s.monteCarloStartPerturbationPercent,0.,1000.);
            RequireMinimum(0,s.savedSampleLimit); RequireRange(s.scatterZoomOut,0.f,1.f);
            for(const auto& [name,p]:s.parameters) if(!std::isfinite(p.minimum) || !std::isfinite(p.maximum) || p.minimum>=p.maximum) throw std::invalid_argument("Invalid fit parameter bounds: "+name+" requires finite minimum < maximum.");
        }
		template <typename T>
		T ReadOr(const YAML::Node &node, const char *key, T fallback)
		{
			const YAML::Node value = node[key];
			return value.IsDefined() ? value.as<T>() : fallback;
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
			return ExistingOr(Path::FromResolved(std::filesystem::u8path(value.as<std::string>())), fallback);
		}

		void EmitPath(YAML::Emitter &out, const char *key, const Path &path)
		{
			out << YAML::Key << key << YAML::Value << path.Utf8();
		}

		std::array<float, 4> ReadColor(const YAML::Node &node, std::array<float, 4> fallback)
		{
			if (!node.IsDefined()) return fallback;
            if(!node.IsSequence() || node.size()!=4) throw std::invalid_argument("Invalid color: expected four RGBA components in [0, 1].");
			return {node[0].as<float>(), node[1].as<float>(), node[2].as<float>(), node[3].as<float>()};
		}

		void EmitColor(YAML::Emitter &out, const std::array<float, 4> &color)
		{
			out << YAML::Flow << YAML::BeginSeq << color[0] << color[1] << color[2] << color[3] << YAML::EndSeq;
		}

		template <std::size_t Size>
		std::array<bool, Size> ReadFlags(const YAML::Node &node, std::array<bool, Size> fallback)
		{
			if (!node.IsDefined()) return fallback;
            if(!node.IsSequence() || node.size()>Size) throw std::invalid_argument("Invalid display flags: expected a boolean sequence.");
			for (std::size_t index = 0; index < Size && index < node.size(); ++index)
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

    std::string JFMUserSettingsStore::GetLoadError() const { std::scoped_lock lock(m_Mutex); return m_LoadError; }
	void JFMUserSettingsStore::Load()
	{
		std::string text;
		std::string error;
		if (!TextFileIO::Load(GetSettingsPath(), text, error))
			return;

		try
		{
			const YAML::Node root = YAML::Load(text);
            if(!root.IsMap()) throw std::invalid_argument("JFM settings root must be a mapping.");
            if(const auto version=root["schema_version"]; version.IsDefined() && version.as<int>()!=1) throw std::invalid_argument("Unsupported JFM settings schema_version. Use the matching SST version or a compatible backup.");
			for(const char* section:{"fit_monte_carlo","auto_prefit"}) if(const auto node=root[section];node.IsDefined() && !node.IsMap()) throw std::invalid_argument(std::string("Invalid JFM settings section: ")+section+" requires a mapping.");
            std::scoped_lock lock(m_Mutex);
            auto candidate=m_Settings;
			candidate.defaultImportDirectory = ReadPath(root, "default_import_directory", candidate.defaultImportDirectory);
			candidate.defaultProjectsDirectory = ReadPath(root, "default_projects_directory", candidate.defaultProjectsDirectory);
			candidate.defaultScriptsDirectory = ReadPath(root, "default_scripts_directory", candidate.defaultScriptsDirectory);
			candidate.defaultPlotSemiLog = root["default_plot_semilog"] ? root["default_plot_semilog"].as<bool>() : candidate.defaultPlotSemiLog;
			candidate.defaultPlotAutoFit = root["default_plot_auto_fit"] ? root["default_plot_auto_fit"].as<bool>() : candidate.defaultPlotAutoFit;
			candidate.defaultPlotShowLegend = root["default_plot_show_legend"] ? root["default_plot_show_legend"].as<bool>() : candidate.defaultPlotShowLegend;
			candidate.defaultPlotFitPadding = root["default_plot_fit_padding"] ? root["default_plot_fit_padding"].as<float>() : candidate.defaultPlotFitPadding;
			candidate.showPrefitCurves = root["show_prefit_curves"] ? root["show_prefit_curves"].as<bool>() : candidate.showPrefitCurves;
			candidate.prefitCurveColor = ReadColor(root["prefit_curve_color"], candidate.prefitCurveColor);
			candidate.prefitCurveLineWeight = root["prefit_curve_line_weight"] ? RequireRange(root["prefit_curve_line_weight"].as<float>(), 0.5f, 8.0f) : candidate.prefitCurveLineWeight;
			candidate.prefitCurvesFitRangeOnly = root["prefit_curves_fit_range_only"] ? root["prefit_curves_fit_range_only"].as<bool>() : candidate.prefitCurvesFitRangeOnly;
			candidate.showFitCurves = root["show_fit_curves"] ? root["show_fit_curves"].as<bool>() : candidate.showFitCurves;
			candidate.fitCurveColor = ReadColor(root["fit_curve_color"], candidate.fitCurveColor);
			candidate.fitCurveLineWeight = root["fit_curve_line_weight"] ? RequireRange(root["fit_curve_line_weight"].as<float>(), 0.5f, 8.0f) : candidate.fitCurveLineWeight;
			candidate.fitCurvesFitRangeOnly = root["fit_curves_fit_range_only"] ? root["fit_curves_fit_range_only"].as<bool>() : candidate.fitCurvesFitRangeOnly;
			candidate.showTunedCurves = root["show_tuned_curves"] ? root["show_tuned_curves"].as<bool>() : candidate.showTunedCurves;
			candidate.tunedCurveColor = ReadColor(root["tuned_curve_color"], candidate.tunedCurveColor);
			candidate.tunedCurveLineWeight = root["tuned_curve_line_weight"] ? RequireRange(root["tuned_curve_line_weight"].as<float>(), 0.5f, 8.0f) : candidate.tunedCurveLineWeight;
			candidate.tunedCurvesFitRangeOnly = root["tuned_curves_fit_range_only"] ? root["tuned_curves_fit_range_only"].as<bool>() : candidate.tunedCurvesFitRangeOnly;
			candidate.autoPrefitFitOnRangeChange = root["auto_prefit_fit_on_range_change"] ? root["auto_prefit_fit_on_range_change"].as<bool>() : candidate.autoPrefitFitOnRangeChange;
			candidate.autoPrefitOnRangeChange = root["auto_prefit_on_range_change"] ? root["auto_prefit_on_range_change"].as<bool>() : candidate.autoPrefitFitOnRangeChange;
			candidate.autoFitOnRangeChange = root["auto_fit_on_range_change"] ? root["auto_fit_on_range_change"].as<bool>() : candidate.autoPrefitFitOnRangeChange;
			candidate.hoveredCurveLineWeight = root["hovered_curve_line_weight"] ? RequireRange(root["hovered_curve_line_weight"].as<float>(), 0.5f, 10.0f) : candidate.hoveredCurveLineWeight;
			candidate.autoPlotAfterImport = root["auto_plot_after_import"] ? root["auto_plot_after_import"].as<bool>() : candidate.autoPlotAfterImport;
			if (const YAML::Node fit = root["fit_monte_carlo"]; fit && fit.IsMap())
			{
				candidate.fitMonteCarlo.fitMethod = ReadOr<std::string>(fit, "fit_method", "lm");
                candidate.fitMonteCarlo.objectiveMode = ReadOr<std::string>(fit, "objective_mode", "auto");
                candidate.fitMonteCarlo.objectiveSwitchK = ReadOr<double>(fit, "objective_switch_k", 3.0);
                candidate.fitMonteCarlo.sigmaAbsolute = ReadOr<double>(fit, "sigma_absolute", 0.0);
				candidate.fitMonteCarlo.fitMaxIterations = RequireMinimum(1, ReadOr<int>(fit, "fit_max_iterations", candidate.fitMonteCarlo.fitMaxIterations));
				candidate.fitMonteCarlo.fitReducedChiSquaredTolerance = RequireMinimum(0.0, ReadOr<double>(fit, "fit_reduced_chi2_tolerance", candidate.fitMonteCarlo.fitReducedChiSquaredTolerance));
				candidate.fitMonteCarlo.monteCarloSamples = RequireMinimum(1, ReadOr<int>(fit, "mc_samples", candidate.fitMonteCarlo.monteCarloSamples));
				candidate.fitMonteCarlo.monteCarloMaxIterations = RequireMinimum(1, ReadOr<int>(fit, "mc_max_iterations", candidate.fitMonteCarlo.monteCarloMaxIterations));
				candidate.fitMonteCarlo.autoCalibrateGpuSimplexIterations = ReadOr<bool>(fit, "auto_calibrate_gpu_simplex_iterations", candidate.fitMonteCarlo.autoCalibrateGpuSimplexIterations);
				candidate.fitMonteCarlo.simplexCalibrationRuns = RequireMinimum(1, ReadOr<int>(fit, "simplex_calibration_runs", candidate.fitMonteCarlo.simplexCalibrationRuns));
				candidate.fitMonteCarlo.simplexCalibrationExtraIterations = RequireMinimum(0, ReadOr<int>(fit, "simplex_calibration_extra_iterations", candidate.fitMonteCarlo.simplexCalibrationExtraIterations));
				candidate.fitMonteCarlo.monteCarloNoisePercent = RequireRange(ReadOr<double>(fit, "mc_noise_percent", candidate.fitMonteCarlo.monteCarloNoisePercent), 0.0, 100.0);
				candidate.fitMonteCarlo.monteCarloStartPerturbationPercent = RequireRange(ReadOr<double>(fit, "mc_start_perturbation_percent", candidate.fitMonteCarlo.monteCarloStartPerturbationPercent), 0.0, 1000.0);
				candidate.fitMonteCarlo.monteCarloSeed = ReadOr<std::uint32_t>(fit, "mc_seed", candidate.fitMonteCarlo.monteCarloSeed);
				candidate.fitMonteCarlo.preferGpu = ReadOr<bool>(fit, "prefer_gpu", candidate.fitMonteCarlo.preferGpu);
				candidate.fitMonteCarlo.useCpuWithGpu = ReadOr<bool>(fit, "use_cpu_with_gpu", candidate.fitMonteCarlo.useCpuWithGpu);
                candidate.fitMonteCarlo.saveSamples = ReadOr<bool>(fit, "save_samples", candidate.fitMonteCarlo.saveSamples);
				candidate.fitMonteCarlo.savedSampleLimit = RequireMinimum(0, ReadOr<int>(fit, "saved_sample_limit", candidate.fitMonteCarlo.savedSampleLimit));
				candidate.fitMonteCarlo.scatterZoomOut = RequireRange(ReadOr<float>(fit, "scatter_zoom_out", candidate.fitMonteCarlo.scatterZoomOut), 0.0f, 1.0f);
				candidate.fitMonteCarlo.showSigmaClasses = ReadFlags(fit["show_sigma_classes"], candidate.fitMonteCarlo.showSigmaClasses);
				candidate.fitMonteCarlo.showSigmaEllipses = ReadFlags(fit["show_sigma_ellipses"], candidate.fitMonteCarlo.showSigmaEllipses);
				if (const YAML::Node colors = fit["sigma_colors"]; colors && colors.IsSequence())
				{
					for (std::size_t index = 0; index < candidate.fitMonteCarlo.sigmaColors.size() && index < colors.size(); ++index)
						candidate.fitMonteCarlo.sigmaColors[index] = ReadColor(colors[index], candidate.fitMonteCarlo.sigmaColors[index]);
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
						candidate.fitMonteCarlo.defaultParameterPairs = std::move(parsed);
				}
				if (const YAML::Node parameters = fit["parameters"]; parameters && parameters.IsMap())
				{
					for (const auto &entry : parameters)
					{
						const std::string name = entry.first.as<std::string>();
						auto existing = candidate.fitMonteCarlo.parameters.find(name);
						JFMParameterSolverSetting setting = existing == candidate.fitMonteCarlo.parameters.end()
							? JFMParameterSolverSetting{}
							: existing->second;
						setting.minimum = ReadOr<double>(entry.second, "minimum", setting.minimum);
						setting.maximum = ReadOr<double>(entry.second, "maximum", setting.maximum);
						setting.monteCarloFree = ReadOr<bool>(entry.second, "free", setting.monteCarloFree);
						if (!std::isfinite(setting.minimum) || !std::isfinite(setting.maximum) || setting.minimum >= setting.maximum) throw std::invalid_argument("Invalid fit parameter bounds: "+name+" requires finite minimum < maximum.");
                        candidate.fitMonteCarlo.parameters[name] = setting;
					}
				}
				if (auto ideality = candidate.fitMonteCarlo.parameters.find("A"); ideality != candidate.fitMonteCarlo.parameters.end()
					&& ideality->second.minimum == 0.1 && ideality->second.maximum == 10.0)
				{
					ideality->second.minimum = 0.0;
					ideality->second.maximum = 5.0;
				}
			}
			if (const YAML::Node autoPrefit = root["auto_prefit"]; autoPrefit && autoPrefit.IsMap())
			{
				candidate.autoPrefit.residualWindowFraction = RequireRange(ReadOr<double>(autoPrefit, "residual_window_fraction", candidate.autoPrefit.residualWindowFraction), 0.001, 1.0);
				candidate.autoPrefit.residualWindowMinPoints = RequireMinimum(3, ReadOr<int>(autoPrefit, "residual_window_min_points", candidate.autoPrefit.residualWindowMinPoints));
				candidate.autoPrefit.residualWindowMaxPoints = RequireMinimum(candidate.autoPrefit.residualWindowMinPoints, ReadOr<int>(autoPrefit, "residual_window_max_points", candidate.autoPrefit.residualWindowMaxPoints));
			}
			candidate.defaultColorPresetName = root["default_color_preset"] ? root["default_color_preset"].as<std::string>() : candidate.defaultColorPresetName;
			if (const YAML::Node presets = root["color_presets"])
			{
				candidate.colorPresets.clear();
				for (const YAML::Node &presetNode : presets)
				{
					JFMColorPreset preset;
					preset.name = presetNode["name"] ? presetNode["name"].as<std::string>() : "Preset";
					preset.start = ReadColor(presetNode["start"], preset.start);
					preset.end = ReadColor(presetNode["end"], preset.end);
					candidate.colorPresets.push_back(std::move(preset));
				}
			}
			if (!HasPreset(candidate.colorPresets, candidate.defaultColorPresetName))
				candidate.defaultColorPresetName = FirstPresetName(candidate.colorPresets);
			candidate.recentProjects.clear();
			if (const YAML::Node recent = root["recent_projects"])
			{
				for (const YAML::Node &entry : recent)
					candidate.recentProjects.push_back(Path::FromResolved(std::filesystem::u8path(entry.as<std::string>())));
			}
            ValidateFitSettings(candidate.fitMonteCarlo);
            RequireRange(candidate.defaultPlotFitPadding,0.f,.5f);
            for(const auto& color:{candidate.prefitCurveColor,candidate.fitCurveColor,candidate.tunedCurveColor}) for(float component:color) RequireRange(component,0.f,1.f);
            for(const auto& preset:candidate.colorPresets) for(const auto& color:{preset.start,preset.end}) for(float component:color) RequireRange(component,0.f,1.f);
            for(const auto& color:candidate.fitMonteCarlo.sigmaColors) for(float component:color) RequireRange(component,0.f,1.f);
            m_Settings=std::move(candidate);
            m_LoadError.clear();
		}
		catch (const std::exception &exception)
		{
			{ std::scoped_lock lock(m_Mutex); m_LoadError="JFM settings were not applied: "+std::string(exception.what())+" Correct the settings file or restore a compatible backup."; }
            DS_LOG_WARN("JFM settings were not applied [{}]: {}. Correct the file or restore a compatible backup.",GetSettingsPath().Utf8(),exception.what());
		}
	}

	void JFMUserSettingsStore::Save() const
	{
		JFMUserSettings snapshot = GetSnapshot();
		YAML::Emitter out;
		out << YAML::BeginMap;
        out << YAML::Key << "schema_version" << YAML::Value << 1;
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
		out << YAML::Key << "fit_curves_fit_range_only" << YAML::Value << snapshot.fitCurvesFitRangeOnly;
		out << YAML::Key << "show_tuned_curves" << YAML::Value << snapshot.showTunedCurves;
		out << YAML::Key << "tuned_curve_color" << YAML::Value;
		EmitColor(out, snapshot.tunedCurveColor);
		out << YAML::Key << "tuned_curve_line_weight" << YAML::Value << snapshot.tunedCurveLineWeight;
		out << YAML::Key << "tuned_curves_fit_range_only" << YAML::Value << snapshot.tunedCurvesFitRangeOnly;
		out << YAML::Key << "auto_prefit_fit_on_range_change" << YAML::Value << snapshot.autoPrefitFitOnRangeChange;
		out << YAML::Key << "auto_prefit_on_range_change" << YAML::Value << snapshot.autoPrefitOnRangeChange;
		out << YAML::Key << "auto_fit_on_range_change" << YAML::Value << snapshot.autoFitOnRangeChange;
		out << YAML::Key << "hovered_curve_line_weight" << YAML::Value << snapshot.hoveredCurveLineWeight;
		out << YAML::Key << "auto_plot_after_import" << YAML::Value << snapshot.autoPlotAfterImport;
		out << YAML::Key << "fit_monte_carlo" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "fit_method" << YAML::Value << snapshot.fitMonteCarlo.fitMethod;
        out << YAML::Key << "objective_mode" << YAML::Value << snapshot.fitMonteCarlo.objectiveMode;
        out << YAML::Key << "objective_switch_k" << YAML::Value << snapshot.fitMonteCarlo.objectiveSwitchK;
        out << YAML::Key << "sigma_absolute" << YAML::Value << snapshot.fitMonteCarlo.sigmaAbsolute;
		out << YAML::Key << "fit_max_iterations" << YAML::Value << snapshot.fitMonteCarlo.fitMaxIterations;
		out << YAML::Key << "fit_reduced_chi2_tolerance" << YAML::Value << snapshot.fitMonteCarlo.fitReducedChiSquaredTolerance;
		out << YAML::Key << "mc_samples" << YAML::Value << snapshot.fitMonteCarlo.monteCarloSamples;
		out << YAML::Key << "mc_max_iterations" << YAML::Value << snapshot.fitMonteCarlo.monteCarloMaxIterations;
		out << YAML::Key << "auto_calibrate_gpu_simplex_iterations" << YAML::Value << snapshot.fitMonteCarlo.autoCalibrateGpuSimplexIterations;
		out << YAML::Key << "simplex_calibration_runs" << YAML::Value << snapshot.fitMonteCarlo.simplexCalibrationRuns;
		out << YAML::Key << "simplex_calibration_extra_iterations" << YAML::Value << snapshot.fitMonteCarlo.simplexCalibrationExtraIterations;
		out << YAML::Key << "mc_noise_percent" << YAML::Value << snapshot.fitMonteCarlo.monteCarloNoisePercent;
		out << YAML::Key << "mc_start_perturbation_percent" << YAML::Value << snapshot.fitMonteCarlo.monteCarloStartPerturbationPercent;
		out << YAML::Key << "mc_seed" << YAML::Value << snapshot.fitMonteCarlo.monteCarloSeed;
		out << YAML::Key << "prefer_gpu" << YAML::Value << snapshot.fitMonteCarlo.preferGpu;
		out << YAML::Key << "use_cpu_with_gpu" << YAML::Value << snapshot.fitMonteCarlo.useCpuWithGpu;
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
		out << YAML::Key << "auto_prefit" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "residual_window_fraction" << YAML::Value << snapshot.autoPrefit.residualWindowFraction;
		out << YAML::Key << "residual_window_min_points" << YAML::Value << snapshot.autoPrefit.residualWindowMinPoints;
		out << YAML::Key << "residual_window_max_points" << YAML::Value << snapshot.autoPrefit.residualWindowMaxPoints;
		out << YAML::EndMap;
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
			out << path.Utf8();
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

	void JFMUserSettingsStore::SetFitCurveDefaults(bool show, std::array<float, 4> color, float lineWeight, bool fitRangeOnly)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.showFitCurves = show;
			m_Settings.fitCurveColor = color;
			m_Settings.fitCurveLineWeight = std::clamp(lineWeight, 0.5f, 8.0f);
			m_Settings.fitCurvesFitRangeOnly = fitRangeOnly;
		}
		Save();
	}

	void JFMUserSettingsStore::SetTunedCurveDefaults(bool show, std::array<float, 4> color, float lineWeight, bool fitRangeOnly)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.showTunedCurves = show;
			m_Settings.tunedCurveColor = color;
			m_Settings.tunedCurveLineWeight = std::clamp(lineWeight, 0.5f, 8.0f);
			m_Settings.tunedCurvesFitRangeOnly = fitRangeOnly;
		}
		Save();
	}

	void JFMUserSettingsStore::SetAutoPrefitFitOnRangeChange(bool enabled)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.autoPrefitFitOnRangeChange = enabled;
			m_Settings.autoPrefitOnRangeChange = enabled;
			m_Settings.autoFitOnRangeChange = enabled;
		}
		Save();
	}

	void JFMUserSettingsStore::SetAutoRangeAnalysis(bool prefit, bool fit)
	{
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.autoPrefitOnRangeChange = prefit;
			m_Settings.autoFitOnRangeChange = fit;
			m_Settings.autoPrefitFitOnRangeChange = prefit && fit;
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
        ValidateFitSettings(settings);
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.fitMonteCarlo = std::move(settings);
		}
		Save();
	}

	void JFMUserSettingsStore::SetAutoPrefitSettings(JFMAutoPrefitSettings settings)
	{
		settings.residualWindowFraction = std::clamp(settings.residualWindowFraction, 0.001, 1.0);
		settings.residualWindowMinPoints = std::max(3, settings.residualWindowMinPoints);
		settings.residualWindowMaxPoints = std::max(settings.residualWindowMinPoints, settings.residualWindowMaxPoints);
		{
			std::scoped_lock lock(m_Mutex);
			m_Settings.autoPrefit = settings;
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
			TouchRecent(m_Settings.recentProjects, path);
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
			{"A", {0.0, 5.0, true}},
			{"Rs", {0.0, 1.0e9, true}},
			{"Rsh", {1.0e-6, 1.0e15, true}},
			{"alpha", {0.1, 10.0, true}},
			{"Rsh2", {1.0e-6, 1.0e15, true}},
			{"I_ph", {0.0, 1.0e6, true}}};
		return settings;
	}
} // namespace SST::Prototypes::JFM
