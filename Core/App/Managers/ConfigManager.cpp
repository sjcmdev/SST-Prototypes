#include "Core/dspch.hpp"

#include <algorithm>
#include <cmath>
#include <Windows.h>
#include <array>
#include <cctype>
#include <filesystem>
#include <locale>
#include <sstream>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "App/Managers/ConfigManager.hpp"
#include "App/Serialization/YamlCodecFacade.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/RuntimeTuning.hpp"
#include "IO/TextFileIO.hpp"

namespace SST
{


	namespace ConfigPaths
	{
		Path NormalizeConfigDirectory(const Path &configDirectory)
		{
			if (!configDirectory.Empty())
				return Path::FromResolved(configDirectory.Native());

			return Path::FromResolved(FileSystem::CurrentPath() / "install" / "app" / "config");
		}

		bool IsPortableAppConfigDirectory(const Path &configDirectory)
		{
			const FilePath path = configDirectory.Native().lexically_normal();
			return path.filename() == "config"
				&& path.parent_path().filename() == "app"
				&& path.parent_path().parent_path().filename() == "install";
		}

		ApplicationPaths Build(const Path &configDirectory)
		{
			ApplicationPaths paths;
			paths.appConfigDirectory = NormalizeConfigDirectory(configDirectory);
            wchar_t executable[32768]{};
                GetModuleFileNameW(nullptr, executable, 32768);
                const auto *portable = _wgetenv(L"SST_PORTABLE_ROOT");
                const auto *local = _wgetenv(L"LOCALAPPDATA");
                if (portable && *portable)
                    paths.userConfigDirectory = Path::FromResolved(std::filesystem::absolute(std::filesystem::path(portable)) / std::filesystem::path(executable).stem() / "config");
                else if (local && *local)
                    paths.userConfigDirectory = Path::FromResolved(std::filesystem::path(local) / "SemiconductorStudio" / std::filesystem::path(executable).stem() / "config");
                else
                    throw std::runtime_error("Cannot locate user settings: LOCALAPPDATA is missing. Set SST_PORTABLE_ROOT to a writable directory.");

			if (IsPortableAppConfigDirectory(paths.appConfigDirectory))
			{
				paths.installRoot = Path::FromResolved(paths.appConfigDirectory.Native().parent_path().parent_path());
				const Path appRoot = Path::FromResolved(paths.installRoot.Native() / "app");

				paths.profilesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "profiles");
				paths.themesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "themes");
				paths.layoutsDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "layouts");
				paths.exportsDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "exports");
				paths.assetsDirectory = Path::FromResolved(appRoot.Native() / "assets");
				paths.fontsDirectory = Path::FromResolved(paths.assetsDirectory.Native() / "fonts");
				return paths;
			}

			paths.installRoot = Path::FromResolved(paths.appConfigDirectory.Native().parent_path());

			paths.profilesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "profiles");
			paths.themesDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "themes");
			paths.layoutsDirectory = paths.userConfigDirectory / "layouts";
			paths.exportsDirectory = Path::FromResolved(paths.userConfigDirectory.Native() / "exports");
			paths.assetsDirectory = Path::FromResolved(paths.installRoot.Native() / "assets");
			paths.fontsDirectory = Path::FromResolved(paths.assetsDirectory.Native() / "fonts");
			return paths;
		}

		bool EnsureDirectory(const Path &directory, std::string &error)
		{
			std::error_code directoryError;
			FileSystem::CreateDirectories(directory.Native(), directoryError);
			if (!directoryError)
				return true;

			error = directoryError.message();
			return false;
		}

		bool EnsurePortableDirectories(const ApplicationPaths &paths, std::string &error)
		{
			return EnsureDirectory(paths.userConfigDirectory, error)
				&& EnsureDirectory(paths.profilesDirectory, error)
				&& EnsureDirectory(paths.themesDirectory, error)
				&& EnsureDirectory(paths.layoutsDirectory, error)
				&& EnsureDirectory(paths.exportsDirectory, error);
		}

        bool MigratePreferences(const ApplicationPaths& paths,std::string& error) {
            auto copy=[&](const Path& source,const Path& target) {
                if(source==target || FileSystem::Exists(target.Native()) || !FileSystem::Exists(source.Native()) || std::filesystem::is_symlink(source.Native())) return true;
                std::string text;
                return TextFileIO::Load(source,text,error) && TextFileIO::Save(target,text,error);
            };
            for(const char* file:{"keybindings.yaml","imgui.ini","jfm_user_settings.yaml","caplab-recent.json","caplab-import-profiles.json","prototype_settings.txt"}) {
                const auto target=std::string_view(file)=="imgui.ini" ? paths.layoutsDirectory/file : paths.userConfigDirectory/file;
                if(!copy(paths.appConfigDirectory/file,target)) return false;
            }
            for(const char* section:{"profiles","themes","layouts"}) {
                const auto source=paths.appConfigDirectory/section;
                if(!std::filesystem::is_directory(source.Native()) || std::filesystem::is_symlink(source.Native())) continue;
                for(const auto& file:std::filesystem::directory_iterator(source.Native()))
                    if(file.is_regular_file() && !file.is_symlink() && !copy(Path::FromResolved(file.path()),paths.userConfigDirectory/section/Path::FromResolved(file.path().filename()))) return false;
            }
            const auto* local=_wgetenv(L"LOCALAPPDATA");wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
            if(local && *local && !copy(Path::FromResolved(std::filesystem::path(local)/"Semiconductor Studio"/std::filesystem::path(executable).stem()/"imgui.ini"),paths.layoutsDirectory/"imgui.ini")) return false;
            return true;
        }
		std::vector<Path> ListFilesByExtension(const Path &directory, std::string_view extension)
		{
			std::vector<Path> result;
			std::error_code error;
			if (!std::filesystem::exists(directory.Native(), error) || error)
				return result;

			for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native(), error))
			{
				if (error)
					break;
				if (!entry.is_regular_file(error) || error)
				{
					error.clear();
					continue;
				}

				std::string fileExtension = Path::FromResolved(entry.path().extension()).Utf8();
				std::transform(fileExtension.begin(), fileExtension.end(), fileExtension.begin(), [](unsigned char character) {
					return static_cast<char>(std::tolower(character));
				});
				if (fileExtension == extension)
					result.push_back(Path::FromResolved(entry.path()));
			}

			std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) {
				return left.Native() < right.Native();
			});
			return result;
		}
	}

	ConfigManager::ConfigManager(Path configDirectory):
		m_ConfigDirectory(std::move(configDirectory)),
		m_Config(CreateDefaultConfig(m_ConfigDirectory))
	{
	}

	bool ConfigManager::Initialize(std::string &error)
	{
		auto previous=m_Config;
        bool committed=false;
        struct Rollback { ApplicationConfig& live; ApplicationConfig& previous; bool& committed;
            ~Rollback() { if(!committed) live=std::move(previous); }
        } rollback{m_Config,previous,committed};
        m_ConfigDirectory = ConfigPaths::NormalizeConfigDirectory(m_ConfigDirectory);
		m_Config = CreateDefaultConfig(m_ConfigDirectory);
		if (!ConfigPaths::EnsurePortableDirectories(m_Config.paths, error))
			return false;

		if (!ConfigPaths::MigratePreferences(m_Config.paths,error)) return false;

		if (!loadOrCreateDefaultConfig(error))
			return false;
		if (!loadOrCreateUserSettings(error))
			return false;

		m_Config.directory = m_ConfigDirectory;
		m_Config.paths = ResolvePortablePaths(m_ConfigDirectory);
		m_Config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).Utf8();
		m_Initialized = true;
        committed=true;
		return true;
	}

	const Path &ConfigManager::GetConfigDirectory() const
	{
		return m_ConfigDirectory;
	}

	const ApplicationConfig &ConfigManager::GetConfig() const
	{
		return m_Config;
	}

	const ApplicationPaths &ConfigManager::GetPaths() const
	{
		return m_Config.paths;
	}

	bool ConfigManager::Validate(const ApplicationConfig &config, std::string &error)
    {
        auto reject = [&](const char *field, const char *expected) {
            error = std::string("Invalid settings field '") + field + "': " + expected + ". Correct this field or reset its section; settings were not applied.";
            return false;
        };
        const auto &u = config.ui;
        if (config.window.width < 320 || config.window.height < 240) return reject("window.width/height", "minimum size is 320 x 240 pixels");
        if (!std::isfinite(u.fontScaleMin) || !std::isfinite(u.fontScaleMax) || u.fontScaleMin <= 0 || u.fontScaleMax < u.fontScaleMin) return reject("ui.font_scale", "finite limits with 0 < min <= max are required");
        if (!std::isfinite(u.fontScale) || u.fontScale < u.fontScaleMin || u.fontScale > u.fontScaleMax) return reject("ui.font_scale", "value must be finite and within the configured limits");
        if (!std::isfinite(u.fontScaleStepMin) || !std::isfinite(u.fontScaleStepMax) || u.fontScaleStepMin <= 0 || u.fontScaleStepMax < u.fontScaleStepMin) return reject("ui.font_scale_step", "finite limits with 0 < min <= max are required");
        for (const auto value : {u.fontScaleStep, u.fontScaleStepSliderMax})
            if (!std::isfinite(value) || value < u.fontScaleStepMin || value > u.fontScaleStepMax) return reject("ui.font_scale_step", "step must be finite and within the configured limits");
        if (config.jobs.defaultWorkerThreadCount < 1) return reject("jobs.default_worker_threads", "at least one worker is required");
        if (config.eventQueue.initialCapacity < RuntimeDefaults::EventQueueMinCapacity || config.eventQueue.growthStep < RuntimeDefaults::EventQueueMinGrowthStep) return reject("event_queue", "capacity and growth must be at least 32");
        const auto &a = config.appearance;
        for (const auto &color : {a.backgroundColor,a.surfaceColor,a.raisedSurfaceColor,a.borderColor,a.collapsibleColor,a.textColor,a.mutedTextColor,a.accentColor,a.clearColor})
            for (float value : color) if (!std::isfinite(value) || value < 0 || value > 1) return reject("appearance.colors", "RGBA components must be finite and in [0, 1]");
        for (float value : {a.windowRounding,a.frameRounding,a.grabRounding,a.popupRounding,a.scrollbarRounding,a.tabRounding,a.windowPaddingX,a.windowPaddingY,a.framePaddingX,a.framePaddingY,a.itemSpacingX,a.itemSpacingY})
            if (!std::isfinite(value) || value < 0) return reject("appearance", "rounding and spacing must be finite and nonnegative");
        const auto &r = a.stateRules;
        for (float value : {r.neutralHoverLighten,r.neutralActiveLighten,r.accentHoverLighten,r.accentHoverSaturation,r.accentActiveDarken,r.accentActiveSaturation,r.selectedAlpha,r.selectedHoverAlpha,r.selectedActiveAlpha,r.disabledAlpha,r.disabledBackgroundMix})
            if (!std::isfinite(value) || value < 0 || value > 1) return reject("appearance.state_rules", "values must be finite and in [0, 1]");
        error.clear();
        return true;
    }

	void ConfigManager::SetConfig(const ApplicationConfig &config)
	{
		std::string error;
		if (!Validate(config, error)) throw std::invalid_argument(error);
		m_Config = config;
		m_Config.directory = m_ConfigDirectory;
		m_Config.paths = ResolvePortablePaths(m_ConfigDirectory);
		if (m_Config.layout.imGuiIniPath.empty())
			m_Config.layout.imGuiIniPath = GetLayoutPath(m_ConfigDirectory).Utf8();
	}

	void ConfigManager::SetUiConfig(const UIConfig &config)
	{
		auto candidate=m_Config; candidate.ui=config; SetConfig(candidate);
	}

	void ConfigManager::SetAppearanceConfig(const AppearanceConfig &config)
	{
		auto candidate=m_Config; candidate.appearance=config; SetConfig(candidate);
	}

	std::vector<Path> ConfigManager::ListYamlFiles(const Path &directory) const
	{
		std::vector<Path> result = ConfigPaths::ListFilesByExtension(directory, ".yaml");
		std::vector<Path> ymlFiles = ConfigPaths::ListFilesByExtension(directory, ".yml");
		result.insert(result.end(), ymlFiles.begin(), ymlFiles.end());
		std::sort(result.begin(), result.end(), [](const Path &left, const Path &right) {
			return left.Native() < right.Native();
		});
		return result;
	}

	std::vector<Path> ConfigManager::ListIniFiles(const Path &directory) const
	{
		return ConfigPaths::ListFilesByExtension(directory, ".ini");
	}

	void ConfigManager::ApplySpecification(ApplicationSpecification &specification) const
	{
		specification.logLevel = m_Config.log.level;
		specification.logToFile = m_Config.log.toFile;
		specification.logFilePath = m_Config.log.filePath;
		specification.traceEvents = m_Config.log.traceEvents;
	}

	ApplicationConfig ConfigManager::CreateDefaultConfig(const Path &configDirectory)
	{
		ApplicationConfig config;
		config.directory = ConfigPaths::NormalizeConfigDirectory(configDirectory);
		config.paths = ResolvePortablePaths(config.directory);
		config.layout.imGuiIniPath = GetLayoutPath(configDirectory).Utf8();
		config.log.level = LogLevel::Info;
		config.log.toFile = true;
		config.log.filePath = Path::FromResolved(RuntimeDefaults::LogFilePath);
		config.log.traceEvents = false;
		config.window.x = RuntimeDefaults::WindowPosX;
		config.window.y = RuntimeDefaults::WindowPosY;
		config.window.width = RuntimeDefaults::WindowWidth;
		config.window.height = RuntimeDefaults::WindowHeight;
		config.window.maximized = RuntimeDefaults::WindowMaximized;
		config.window.title = RuntimeDefaults::WindowTitle;
		config.ui.fontScale = RuntimeDefaults::UiFontScale;
		config.ui.fontScaleStep = RuntimeDefaults::UiFontScaleStep;
		config.ui.fontPath = RuntimeDefaults::UiFontPath;
		config.ui.settingsPreviewEnabled = RuntimeDefaults::UiSettingsPreviewEnabled;
		config.ui.settingsAutoSaveOnPreview = RuntimeDefaults::UiSettingsAutoSaveOnPreview;
		config.ui.fontScaleMin = RuntimeDefaults::UiFontScaleMin;
		config.ui.fontScaleMax = RuntimeDefaults::UiFontScaleMax;
		config.ui.fontScaleStepMin = RuntimeDefaults::UiFontScaleStepMin;
		config.ui.fontScaleStepMax = RuntimeDefaults::UiFontScaleStepMax;
		config.ui.fontScaleStepSliderMax = RuntimeDefaults::UiFontScaleStepSliderMax;
		config.jobs.defaultWorkerThreadCount = RuntimeDefaults::JobsDefaultWorkerThreads;
		config.jobs.reserveUrgentWorker = RuntimeDefaults::JobsReserveUrgentWorker;
		config.eventQueue.initialCapacity = RuntimeDefaults::EventQueueInitialCapacity;
		config.eventQueue.growthStep = RuntimeDefaults::EventQueueGrowthStep;
		return config;
	}

	ApplicationPaths ConfigManager::ResolvePortablePaths(const Path &configDirectory)
	{
		return ConfigPaths::Build(configDirectory);
	}

	Path ConfigManager::GetDefaultConfigPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).appConfigDirectory / Path(DefaultConfigFileName);
	}

	Path ConfigManager::GetUserSettingsPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).userConfigDirectory / Path(UserSettingsFileName);
	}

	Path ConfigManager::GetKeybindingsPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).userConfigDirectory / Path(KeybindingsFileName);
	}

	Path ConfigManager::GetLayoutPath(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).layoutsDirectory / Path(LayoutFileName);
	}

	Path ConfigManager::GetProfilesDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).profilesDirectory;
	}

	Path ConfigManager::GetThemesDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).themesDirectory;
	}

	Path ConfigManager::GetLayoutsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).layoutsDirectory;
	}

	Path ConfigManager::GetExportsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).exportsDirectory;
	}

	Path ConfigManager::GetAssetsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).assetsDirectory;
	}

	Path ConfigManager::GetFontsDirectory(const Path &configDirectory)
	{
		return ResolvePortablePaths(configDirectory).fontsDirectory;
	}

	bool ConfigManager::ensureInitialized(std::string &error) const
	{
		if (m_Initialized)
			return true;

		error = "ConfigManager is not initialized";
		return false;
	}

	bool ConfigManager::loadOrCreateDefaultConfig(std::string &error)
	{
		const Path path = GetDefaultConfigPath(m_ConfigDirectory);
		if (!FileSystem::Exists(path.Native()))
			return true; // Packaged defaults are read-only; built-in typed defaults remain valid.


		std::string text;
		if (!TextFileIO::Load(path, text, error))
			return false;

		if (!YamlCodecFacade::Default().DeserializeDefaultConfig(text, m_Config, error)) {
            error=path.Utf8()+": "+error;
            return false;
        }
        return true;
	}

	bool ConfigManager::loadOrCreateUserSettings(std::string &error)
	{
		const Path path = GetUserSettingsPath(m_ConfigDirectory);
		if (!FileSystem::Exists(path.Native()))
		{
			const Path legacyAppSettingsPath = m_Config.paths.appConfigDirectory / Path(UserSettingsFileName);
			if (legacyAppSettingsPath != path && FileSystem::Exists(legacyAppSettingsPath.Native()))
			{
				std::string legacyText;
				if (!TextFileIO::Load(legacyAppSettingsPath, legacyText, error))
					return false;
				if (!YamlCodecFacade::Default().DeserializeUserSettings(legacyText, m_Config, error))
					return false;
			}

			std::string text;
			if (!YamlCodecFacade::Default().SerializeUserSettings(m_Config, text, error))
				return false;
			return TextFileIO::Save(path, text, error);
		}

		std::string text;
		if (!TextFileIO::Load(path, text, error))
			return false;

		if (!YamlCodecFacade::Default().DeserializeUserSettings(text, m_Config, error)) {
            error=path.Utf8()+": "+error;
            return false;
        }
        return true;
	}
} // namespace SST
