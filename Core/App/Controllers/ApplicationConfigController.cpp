#include "Core/dspch.hpp"

#include "App/Controllers/ApplicationConfigController.hpp"

#include <algorithm>
#include <functional>
#include <vector>

#include "App/Managers/ConfigManager.hpp"
#include "App/Managers/ConfigProfileStore.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "App/Serialization/YamlCodecFacade.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/EventQueue.hpp"
#include "Core/JobSystem/JobSystemConfigEvents.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Events/RendererEvents.hpp"

namespace SST
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeConfigController(
			EventBus &bus,
			ApplicationConfigController &controller,
			void (ApplicationConfigController::*method)(const EventType &))
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &controller));
		}

		void queueAppliedConfigEvents(EventBus &bus, const ApplicationConfig &config, bool persisted)
		{
			bus.Queue(AppEvents::Config::Applied{config, persisted});
			bus.Queue(JobSystemConfigAppliedEvent{config.jobs});
			bus.Queue(EditorUiEvents::RuntimeConfigApplied{
				config.ui,
				config.appearance,
				config.layout.imGuiIniPath.empty() ? ConfigManager::GetLayoutPath(config.directory) : Path::FromResolved(std::filesystem::u8path(config.layout.imGuiIniPath)),
				persisted});
		}

		void queueConfigApplyFailedNotification(EventBus &bus, std::string message)
		{
			bus.Queue(NotificationRequestedEvent{Notification{
				NotificationSeverity::Error,
				NotificationCategory::Config,
				"Config apply failed",
				std::move(message),
				"ApplicationConfigController",
				Time::Now(),
				12000,
				false}});
		}
	}

	ApplicationConfigController::ApplicationConfigController(
		Ref<EventBus> eventBus,
		Ref<ConfigManager> configManager,
		ApplicationConfig &config,
		ApplicationSpecification &specification,
		EventQueue &eventQueue,
		Ref<LogRegistry> logRegistry)
		: m_EventBus(std::move(eventBus)),
		  m_ConfigManager(std::move(configManager)),
		  m_Config(config),
		  m_Specification(specification),
		  m_EventQueue(eventQueue),
		  m_LogRegistry(std::move(logRegistry))
	{
		bindEvents();
	}

	ApplicationConfigController::~ApplicationConfigController()
	{
		ClearSubscriptions();
		m_EventBus.reset();
		m_ConfigManager.reset();
	}

	void ApplicationConfigController::bindEvents()
	{
		if (m_EventBus == nullptr)
		{
			DS_LOG_ERROR("ApplicationConfigController requires a valid EventBus");
			return;
		}

		using namespace AppEvents::Config;

		AddSubscription(subscribeConfigController<ApplyRequested>(*m_EventBus, *this, &ApplicationConfigController::onConfigApplyRequested));
		AddSubscription(subscribeConfigController<SaveUserRequested>(*m_EventBus, *this, &ApplicationConfigController::onUserConfigSaveRequested));
		AddSubscription(subscribeConfigController<SaveDefaultsRequested>(*m_EventBus, *this, &ApplicationConfigController::onDefaultsSaveRequested));
		AddSubscription(subscribeConfigController<ProfileListRequested>(*m_EventBus, *this, &ApplicationConfigController::onProfileListRequested));
		AddSubscription(subscribeConfigController<ProfileSaveRequested>(*m_EventBus, *this, &ApplicationConfigController::onProfileSaveRequested));
		AddSubscription(subscribeConfigController<ProfileLoadRequested>(*m_EventBus, *this, &ApplicationConfigController::onProfileLoadRequested));
		AddSubscription(subscribeConfigController<ProfileExportRequested>(*m_EventBus, *this, &ApplicationConfigController::onProfileExportRequested));
		AddSubscription(subscribeConfigController<EditorUiEvents::AppearanceApplyRequested>(*m_EventBus, *this, &ApplicationConfigController::onAppearanceApplyRequested));
		AddSubscription(subscribeConfigController<EditorUiEvents::ThemeLoaded>(*m_EventBus, *this, &ApplicationConfigController::onThemeLoaded));
		DS_LOG_INFO("ApplicationConfigController event handlers bound");
	}

	void ApplicationConfigController::initializeLogger() const
	{
		LoggerOptions loggerOptions;
		loggerOptions.level = m_Specification.logLevel;
		loggerOptions.logToFile = m_Specification.logToFile;
		loggerOptions.logFilePath = m_Specification.logFilePath.Native();
		loggerOptions.logRegistry = m_LogRegistry;
		Logger::Initialize(loggerOptions);
	}

	ApplicationConfig ApplicationConfigController::normalizeConfigSnapshot(const ApplicationConfig &config) const
	{
		ApplicationConfig normalized = config;
		if (m_ConfigManager != nullptr)
		{
			normalized.directory = m_ConfigManager->GetConfigDirectory();
			normalized.paths = m_ConfigManager->GetPaths();
		}

		if (normalized.layout.imGuiIniPath.empty() && !normalized.directory.Empty())
			normalized.layout.imGuiIniPath = ConfigManager::GetLayoutPath(normalized.directory).Utf8();

		return normalized;
	}

	bool ApplicationConfigController::applyConfigRequest(
		const ApplicationConfig &config,
		ApplicationConfig &appliedConfig,
		std::string &error)
	{
		if (m_ConfigManager == nullptr)
		{
			error = "ConfigManager is not initialized";
			DS_LOG_ERROR("Config apply rejected: {}", error);
			return false;
		}

		if (!ConfigManager::Validate(config, error)) return false;
		appliedConfig = normalizeConfigSnapshot(config);
		DS_LOG_INFO(
			"Config apply requested: directory={} window={}x{} maximized={} workers={} reserve_urgent={} font_scale={}",
			appliedConfig.directory.Utf8(),
			appliedConfig.window.width,
			appliedConfig.window.height,
			appliedConfig.window.maximized,
			appliedConfig.jobs.defaultWorkerThreadCount,
			appliedConfig.jobs.reserveUrgentWorker,
			appliedConfig.ui.fontScale);

		try
		{
			m_Config = appliedConfig;
			m_ConfigManager->SetConfig(m_Config);
			m_ConfigManager->ApplySpecification(m_Specification);
			initializeLogger();
			m_EventQueue.Configure(m_Config.eventQueue.initialCapacity, m_Config.eventQueue.growthStep);
		}
		catch (const std::exception &exception)
		{
			error = exception.what();
			DS_LOG_ERROR("Config apply failed with exception: {}", error);
			return false;
		}
		catch (...)
		{
			error = "Unknown exception while applying config";
			DS_LOG_ERROR("Config apply failed with unknown exception");
			return false;
		}

		DS_LOG_INFO(
			"Config apply complete: event_queue_capacity={} event_queue_growth={}",
			m_Config.eventQueue.initialCapacity,
			m_Config.eventQueue.growthStep);
		return true;
	}

	void ApplicationConfigController::onConfigApplyRequested(const AppEvents::Config::ApplyRequested &event)
	{
		using namespace AppEvents::Config;

		DS_LOG_INFO("Config apply event received: persist={}", event.persist);
		std::string error;
		ApplicationConfig appliedConfig;
		if (!applyConfigRequest(event.config, appliedConfig, error))
		{
			m_EventBus->Queue(ApplyFailed{event.config, event.persist, error});
			DS_LOG_ERROR("Config apply event failed: persist={} error={}", event.persist, error);
			queueConfigApplyFailedNotification(*m_EventBus, error);
			return;
		}

		queueAppliedConfigEvents(*m_EventBus, appliedConfig, false);
		if (event.persist)
		{

			m_EventBus->Queue(PersistRequested{
				PersistKind::UserSettings,
				appliedConfig,
				ConfigManager::GetUserSettingsPath(appliedConfig.directory)});
			DS_LOG_INFO("Config persistence queued after successful apply");
		}

		DS_LOG_INFO("Config applied event queued: persist={}", event.persist);
	}

	void ApplicationConfigController::onUserConfigSaveRequested(const AppEvents::Config::SaveUserRequested &event)
	{
		using namespace AppEvents::Config;

		DS_LOG_INFO("User config save event received");
		ApplicationConfig appliedConfig;
		std::string error;
		if (!applyConfigRequest(event.config, appliedConfig, error))
		{
			m_EventBus->Queue(UserSaveFailed{event.config, error});
			DS_LOG_ERROR("User config save rejected because apply failed: {}", error);
			return;
		}

		queueAppliedConfigEvents(*m_EventBus, appliedConfig, false);

		m_EventBus->Queue(PersistRequested{
			PersistKind::UserSettings,
			appliedConfig,
			ConfigManager::GetUserSettingsPath(appliedConfig.directory)});
		DS_LOG_INFO("User config persistence queued after successful apply");
	}

	void ApplicationConfigController::onDefaultsSaveRequested(const AppEvents::Config::SaveDefaultsRequested &event)
	{
		using namespace AppEvents::Config;

		DS_LOG_INFO("Default config save event received");
		ApplicationConfig appliedConfig;
		std::string error;
		if (!applyConfigRequest(event.config, appliedConfig, error))
		{
			m_EventBus->Queue(DefaultsSaveFailed{event.config, error});
			DS_LOG_ERROR("Default config save rejected because apply failed: {}", error);
			return;
		}

		queueAppliedConfigEvents(*m_EventBus, appliedConfig, false);

		m_EventBus->Queue(PersistRequested{
			PersistKind::Defaults,
			appliedConfig,
			ConfigManager::GetDefaultConfigPath(appliedConfig.directory)});
		DS_LOG_INFO("Default config persistence queued after successful apply");
	}

	void ApplicationConfigController::onProfileListRequested(const AppEvents::Config::ProfileListRequested &event)
	{
		(void)event;
		using namespace AppEvents::Config;

		if (m_ConfigManager == nullptr)
		{
			m_EventBus->Queue(ProfileListFailed{"ConfigManager is not initialized"});
			return;
		}

		ConfigProfileStore store(CreateWeakRef(m_ConfigManager));
		std::vector<ConfigProfileEntry> entries = store.Refresh();
		m_EventBus->Queue(ProfileListLoaded{std::move(entries)});
	}

	void ApplicationConfigController::onProfileSaveRequested(const AppEvents::Config::ProfileSaveRequested &event)
	{
		using namespace AppEvents::Config;

		if (m_ConfigManager == nullptr)
		{
			m_EventBus->Queue(ProfileSaveFailed{event.name, "ConfigManager is not initialized"});
			return;
		}

		ConfigProfileStore store(CreateWeakRef(m_ConfigManager));
		std::string error;
		if (!store.Save(event.name, normalizeConfigSnapshot(event.config), error))
		{
			m_EventBus->Queue(ProfileSaveFailed{event.name, error});
			return;
		}

		m_EventBus->Queue(ProfileSaved{event.name, store.ProfilePath(event.name)});
		m_EventBus->Queue(ProfileListLoaded{std::vector<ConfigProfileEntry>(store.Entries().begin(), store.Entries().end())});
	}

	void ApplicationConfigController::onProfileLoadRequested(const AppEvents::Config::ProfileLoadRequested &event)
	{
		using namespace AppEvents::Config;

		if (m_ConfigManager == nullptr)
		{
			m_EventBus->Queue(ProfileLoadFailed{event.name, "ConfigManager is not initialized"});
			return;
		}

		ConfigProfileStore store(CreateWeakRef(m_ConfigManager));
		ApplicationConfig config;
		std::string error;
		if (!store.Load(event.path, config, error))
		{
			m_EventBus->Queue(ProfileLoadFailed{event.name, error});
			return;
		}

		m_EventBus->Queue(ProfileLoaded{event.name, config});
		m_EventBus->Queue(ApplyRequested{std::move(config), true});
	}

	void ApplicationConfigController::onProfileExportRequested(const AppEvents::Config::ProfileExportRequested &event)
	{
		using namespace AppEvents::Config;

		if (m_ConfigManager == nullptr)
		{
			m_EventBus->Queue(ProfileExportFailed{event.name, "ConfigManager is not initialized"});
			return;
		}

		ConfigProfileStore store(CreateWeakRef(m_ConfigManager));
		std::string error;
		if (!store.Export(event.path, error))
		{
			m_EventBus->Queue(ProfileExportFailed{event.name, error});
			return;
		}

		m_EventBus->Queue(ProfileExported{event.name, m_ConfigManager->GetPaths().exportsDirectory / event.path.filename()});
	}

	void ApplicationConfigController::onAppearanceApplyRequested(const EditorUiEvents::AppearanceApplyRequested &event)
	{
		auto candidate=m_Config;candidate.appearance=event.appearance;
		m_EventBus->Queue(AppEvents::Config::SaveUserRequested{std::move(candidate)});
		DS_LOG_INFO("Appearance apply requested; user config save queued");
	}

	void ApplicationConfigController::onThemeLoaded(const EditorUiEvents::ThemeLoaded &event)
	{
		auto candidate=m_Config;candidate.appearance=event.appearance;
		m_EventBus->Queue(AppEvents::Config::SaveUserRequested{std::move(candidate)});
		DS_LOG_INFO("Appearance theme loaded; user config save queued: {}", event.path.Utf8());
	}
} // namespace SST
