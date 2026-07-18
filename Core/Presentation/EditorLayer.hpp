#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/EditorUiState.hpp"
#include "Presentation/Panels/PanelRegistry.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"

namespace SST
{
	class CommandRegistry;
	class ContextManager;
	class EventBus;
	class JobSystem;
	class KeymapResolver;
	class LogRegistry;
	class ProgressTracker;
	struct ApplicationConfig;
	struct CommandID;
	class Event;

	namespace AppEvents::Config
	{
		struct Applied;
	}

	namespace CoreEvents
	{
		struct OpenCommandPaletteRequested;
	}

	struct EditorServices
	{
		Ref<EventBus> eventBus;
		Ref<LogRegistry> logRegistry;
		WeakRef<JobSystem> jobSystem;
		WeakRef<ProgressTracker> progressTracker;
		WeakRef<KeymapResolver> keymapResolver;
		WeakRef<ContextManager> contextManager;
		WeakRef<CommandRegistry> commandRegistry;
		WeakRef<EditorUiState> uiState;
	};

	using EditorMenuRenderer = std::function<void(EditorServices &)>;

	class EditorLayer final : public Layer, public EventReceiver
	{
	public:
		EditorLayer();

		void BindRuntimeServices(Ref<EventBus> eventBus,
		                         WeakRef<JobSystem> jobSystem,
		                         WeakRef<ProgressTracker> progressTracker,
		                         Ref<LogRegistry> logRegistry,
		                         WeakRef<KeymapResolver> keymapResolver,
		                         WeakRef<ContextManager> contextManager,
		                         WeakRef<CommandRegistry> commandRegistry);
		void ApplyConfig(const ApplicationConfig &config);
		[[nodiscard]] WeakRef<EditorUiState> GetUiStateHandle();
		[[nodiscard]] EditorServices GetServices();

		template <typename TPanel, typename... Args>
		PanelId RegisterPanel(Args &&...args);
		[[nodiscard]] WeakRef<IPanel> FindPanel(PanelId panelId);
		[[nodiscard]] WeakRef<const IPanel> FindPanel(PanelId panelId) const;
		void RenderCorePanelMenuItems();
		void RegisterMenuRenderer(std::string id, EditorMenuRenderer renderer);
		void RegisterSettingsTab(SettingsTabExtension extension);

		void OnDetach() override;
		void OnEvent(Event &event) override;
		void OnImGuiRender() override;

	private:
		void initializePanelsIfNeeded();
		void initializeUiState();
		void bindEvents();
		void handleFontShortcuts(Event &event);
		void renderMenuBar();
		void renderCommandPalettePopup();
		void executeCommandFromPalette(const CommandID &id);
		void onConfigApplied(const AppEvents::Config::Applied &event);
		void onOpenCommandPaletteRequested(const CoreEvents::OpenCommandPaletteRequested &event);

	private:
		struct MenuRendererEntry
		{
			std::string id;
			EditorMenuRenderer renderer;
		};

		PanelRegistry m_Panels;
		std::vector<MenuRendererEntry> m_MenuRenderers;
		std::vector<SettingsTabExtension> m_SettingsTabs;
		Ref<EditorUiState> m_UiState;
		Ref<ApplicationConfig> m_CurrentConfig;
		Ref<EventBus> m_EventBus;
		Ref<LogRegistry> m_LogRegistry;
		WeakRef<JobSystem> m_JobSystem;
		WeakRef<ProgressTracker> m_ProgressTracker;
		WeakRef<KeymapResolver> m_KeymapResolver;
		WeakRef<ContextManager> m_ContextManager;
		WeakRef<CommandRegistry> m_CommandRegistry;
		PanelId m_ProgressMonitorPanelId = 0;
		PanelId m_TaskMonitorPanelId = 0;
		PanelId m_LoggingPanelId = 0;
		PanelId m_SettingsPanelId = 0;
		bool m_PanelsInitialized = false;
		bool m_CommandPaletteOpenRequested = false;
		int m_CommandPaletteSelection = 0;
		std::array<char, 128> m_CommandPaletteSearchBuffer = {};
	};

	template <typename TPanel, typename... Args>
	PanelId EditorLayer::RegisterPanel(Args &&...args)
	{
		return m_Panels.Add<TPanel>(std::forward<Args>(args)...);
	}
} // namespace SST
