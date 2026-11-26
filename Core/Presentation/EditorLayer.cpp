#include "Core/dspch.hpp"

#include "Presentation/EditorLayer.hpp"
#include "Presentation/Panels/ViewHost.hpp"

#include <algorithm>

#include "App/Application.hpp"
#include "App/Events/ApplicationConfigEvents.hpp"
#include "App/Managers/ConfigManager.hpp"
#include "Core/Commands/CommandPalette.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Logging/LogRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/BuildInfo.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include "Core/Diagnostics/DiagnosticsCommand.hpp"
#include "Core/Platform/NativeFileDialog.hpp"
#include "Core/Utils/Input.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Core/Utils/Path.hpp"
#include "Events/EditorUiEvents.hpp"
#include "Presentation/Panels/LoggingPanel.hpp"
#include "Presentation/Panels/ProgressMonitorWindow.hpp"
#include "Presentation/Panels/SettingsPanel.hpp"
#include "Presentation/Panels/TaskMonitorWindow.hpp"

#include <imgui.h>

namespace SST
{
	namespace
	{
		template <typename EventType>
		SubscriptionHandle subscribeEditorLayer(
			EventBus &bus,
			EditorLayer &layer,
			void (EditorLayer::*method)(const EventType &),
			EventPriority priority = EventPriority::Normal)
		{
			return bus.Subscribe<EventType>(std::bind_front(method, &layer), priority);
		}
	} // namespace

	EditorLayer::EditorLayer()
		: Layer("EditorLayer"), m_Windows(CreateRef<WindowWorkspace>(m_Panels)), m_Views(CreateRef<ViewHost>(m_Panels, m_Windows))
	{
	}

	void EditorLayer::BindRuntimeServices(Ref<EventBus> eventBus,
	                                      WeakRef<JobSystem> jobSystem,
	                                      WeakRef<ProgressTracker> progressTracker,
	                                      Ref<LogRegistry> logRegistry,
	                                      WeakRef<KeymapResolver> keymapResolver,
	                                      WeakRef<ContextManager> contextManager,
	                                      WeakRef<CommandRegistry> commandRegistry)
	{
		ClearSubscriptions();
		m_EventBus = std::move(eventBus);
		m_JobSystem = std::move(jobSystem);
		m_ProgressTracker = std::move(progressTracker);
		m_LogRegistry = std::move(logRegistry);
		m_KeymapResolver = std::move(keymapResolver);
		m_ContextManager = std::move(contextManager);
		m_CommandRegistry = std::move(commandRegistry);
		if (auto commands = m_CommandRegistry.lock())
		{
			const auto result = m_Windows->Bind(commands, m_KeymapResolver.lock());
			if (!result) DS_LOG_ERROR("Window command registration failed: {}", result.Error().userMessage);
			const auto views = m_Views->Bind(commands, m_JobSystem.lock());
			if (!views) DS_LOG_ERROR("View command registration failed: {}", views.Error().userMessage);
		}
		bindEvents();
	}

	void EditorLayer::ApplyConfig(const ApplicationConfig &config)
	{
		m_CurrentConfig = CreateRef<ApplicationConfig>(config);
		initializeUiState();
		m_UiState->fontScale = config.ui.fontScale;
		m_UiState->fontScaleStep = config.ui.fontScaleStep;
		m_UiState->fontScaleMin = config.ui.fontScaleMin;
		m_UiState->fontScaleMax = config.ui.fontScaleMax;
		m_UiState->fontScaleStepMin = config.ui.fontScaleStepMin;
		m_UiState->fontScaleStepMax = config.ui.fontScaleStepMax;
		m_UiState->fontScaleStepSliderMax = config.ui.fontScaleStepSliderMax;
		m_UiState->defaultWorkerThreadCount = config.jobs.defaultWorkerThreadCount;
		m_UiState->reserveUrgentWorkerByDefault = config.jobs.reserveUrgentWorker;
		m_UiState->selectedFontPath = config.ui.fontPath;
		m_UiState->paths = config.paths;
		m_UiState->appearance = config.appearance;
		m_UiState->layoutPath = config.layout.imGuiIniPath.empty()
			? ConfigManager::GetLayoutPath(config.directory).Utf8()
			: config.layout.imGuiIniPath;
		if (m_UiState->themeSavePath.empty())
			m_UiState->themeSavePath = (config.paths.themesDirectory / Path("dark-orange.yaml")).Utf8();
		if (m_UiState->themeLoadPath.empty())
			m_UiState->themeLoadPath = m_UiState->themeSavePath;
	}

	WeakRef<EditorUiState> EditorLayer::GetUiStateHandle()
	{
		initializeUiState();
		return CreateWeakRef(m_UiState);
	}

	EditorServices EditorLayer::GetServices()
	{
		Application &application = Application::Get();
		return EditorServices{
			m_EventBus != nullptr ? m_EventBus : application.GetEventBusRef(),
			m_LogRegistry != nullptr ? m_LogRegistry : application.GetLogRegistry(),
			m_JobSystem,
			m_ProgressTracker,
			m_KeymapResolver,
			m_ContextManager,
			m_CommandRegistry,
			GetUiStateHandle()};
	}

	WeakRef<IPanel> EditorLayer::FindPanel(PanelId panelId)
	{
		return m_Panels.Get(panelId);
	}

	WeakRef<const IPanel> EditorLayer::FindPanel(PanelId panelId) const
	{
		return m_Panels.Get(panelId);
	}

	void EditorLayer::RenderCorePanelMenuItems()
	{
		initializePanelsIfNeeded();
		m_Views->RenderMenu();
		const PanelId panelIds[] = {m_ProgressMonitorPanelId, m_TaskMonitorPanelId, m_LoggingPanelId, m_SettingsPanelId};
		for (PanelId panelId : panelIds)
		{
			auto panel = FindPanel(panelId).lock();
			if (panel == nullptr)
				continue;
			bool visible = panel->IsVisible();
			if (ImGui::MenuItem(panel->GetTitle().c_str(), nullptr, visible))
				panel->SetVisible(!visible);
		}
	}

	void EditorLayer::RegisterMenuRenderer(std::string id, EditorMenuRenderer renderer)
	{
		if (!renderer)
			return;

		auto existing = std::find_if(
			m_MenuRenderers.begin(),
			m_MenuRenderers.end(),
			[&id](const MenuRendererEntry &entry) { return entry.id == id; });
		if (existing != m_MenuRenderers.end())
		{
			existing->renderer = std::move(renderer);
			return;
		}

		m_MenuRenderers.push_back(MenuRendererEntry{std::move(id), std::move(renderer)});
	}

	void EditorLayer::RegisterSettingsTab(SettingsTabExtension extension)
	{
		if (!extension.renderer || extension.id.empty() || extension.title.empty())
			return;

		auto existing = std::find_if(
			m_SettingsTabs.begin(),
			m_SettingsTabs.end(),
			[&extension](const SettingsTabExtension &entry) { return entry.id == extension.id; });
		if (existing != m_SettingsTabs.end())
		{
			*existing = std::move(extension);
			return;
		}

		m_SettingsTabs.push_back(std::move(extension));
	}

	void EditorLayer::OnDetach()
	{
		// ImGuiLayer saves after EditorLayer detaches; retain its settings payload until then.
		m_Windows->FreezeSettings();
		m_Panels.Clear();
		m_MenuRenderers.clear();
		m_SettingsTabs.clear();
		m_PanelsInitialized = false;
		ClearSubscriptions();
		m_EventBus.reset();
		m_LogRegistry.reset();
		m_JobSystem.reset();
		m_ProgressTracker.reset();
		m_KeymapResolver.reset();
		m_ContextManager.reset();
		m_CommandRegistry.reset();
		m_ProgressMonitorPanelId = 0;
		m_TaskMonitorPanelId = 0;
		m_LoggingPanelId = 0;
		m_SettingsPanelId = 0;
		m_UiState.reset();
		m_CurrentConfig.reset();
	}

	void EditorLayer::OnEvent(Event &event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent &keyEvent) {
			handleFontShortcuts(keyEvent);
			return keyEvent.handled;
		});
		dispatcher.Dispatch<KeyRepeatedEvent>([this](KeyRepeatedEvent &keyEvent) {
			handleFontShortcuts(keyEvent);
			return keyEvent.handled;
		});
	}

	void EditorLayer::OnImGuiRender()
	{
		initializePanelsIfNeeded();
		m_Windows->InstallSettingsHandler();
		renderMenuBar();
		renderSupportDialogs();
		renderCommandPalettePopup();

		const auto panels = m_Panels.Entries(); // A chrome command can duplicate during rendering.
		for (const auto &entry : panels)
			entry.panel->Render();
		m_Windows->RenderOverlays();
	}

	void EditorLayer::initializePanelsIfNeeded()
	{
		if (m_PanelsInitialized)
			return;

		Application &application = Application::Get();
		const ApplicationConfig &config = m_CurrentConfig != nullptr ? *m_CurrentConfig : application.GetConfig();
		Ref<EventBus> eventBus = m_EventBus != nullptr ? m_EventBus : application.GetEventBusRef();

		initializeUiState();
		m_ProgressMonitorPanelId = RegisterPanel<ProgressMonitorWindow>(eventBus, m_ProgressTracker, "Progress Monitor", false);
		m_TaskMonitorPanelId = RegisterPanel<TaskMonitorWindow>(eventBus, m_JobSystem, "Task Monitor", false);
		m_LoggingPanelId = RegisterPanel<LoggingPanel>(
			eventBus,
			m_LogRegistry != nullptr ? m_LogRegistry : application.GetLogRegistry(),
			config.paths.exportsDirectory / Path("event-log-export.csv"),
			"Logging Panel",
			false);
		m_SettingsPanelId = RegisterPanel<SettingsPanel>(
			eventBus,
			m_JobSystem,
			CreateWeakRef(m_UiState),
			m_KeymapResolver,
			m_CommandRegistry,
			config,
			m_SettingsTabs,
			"Settings",
			false);

		m_PanelsInitialized = true;
	}

	void EditorLayer::initializeUiState()
	{
		if (m_UiState)
			return;

		const ApplicationConfig &config = m_CurrentConfig != nullptr ? *m_CurrentConfig : Application::Get().GetConfig();
		m_UiState = CreateRef<EditorUiState>();
		m_UiState->fontScale = config.ui.fontScale;
		m_UiState->fontScaleStep = config.ui.fontScaleStep;
		m_UiState->fontScaleMin = config.ui.fontScaleMin;
		m_UiState->fontScaleMax = config.ui.fontScaleMax;
		m_UiState->fontScaleStepMin = config.ui.fontScaleStepMin;
		m_UiState->fontScaleStepMax = config.ui.fontScaleStepMax;
		m_UiState->fontScaleStepSliderMax = config.ui.fontScaleStepSliderMax;
		m_UiState->defaultWorkerThreadCount = config.jobs.defaultWorkerThreadCount;
		m_UiState->reserveUrgentWorkerByDefault = config.jobs.reserveUrgentWorker;
		m_UiState->selectedFontPath = config.ui.fontPath;
		m_UiState->paths = config.paths;
		m_UiState->appearance = config.appearance;
		m_UiState->layoutPath = config.layout.imGuiIniPath.empty()
			? ConfigManager::GetLayoutPath(config.directory).Utf8()
			: config.layout.imGuiIniPath;
		m_UiState->themeSavePath = (config.paths.themesDirectory / Path("dark-orange.yaml")).Utf8();
		m_UiState->themeLoadPath = m_UiState->themeSavePath;
	}

	void EditorLayer::bindEvents()
	{
		if (m_EventBus == nullptr)
			return;

		AddSubscription(subscribeEditorLayer<AppEvents::Config::Applied>(
			*m_EventBus,
			*this,
			&EditorLayer::onConfigApplied,
			EventPriority::High));
		AddSubscription(subscribeEditorLayer<CoreEvents::OpenCommandPaletteRequested>(
			*m_EventBus,
			*this,
			&EditorLayer::onOpenCommandPaletteRequested,
			EventPriority::High));
	}

	void EditorLayer::handleFontShortcuts(Event &event)
	{
		if (!HasModifier(Input::GetCurrentKeyModifiers(), KeyModifiers::Ctrl))
			return;

		initializeUiState();
		const auto handleKey = [this, &event](int keyCode) {
			const float fontScaleMin = m_UiState->fontScaleMin;
			const float fontScaleMax = std::max(m_UiState->fontScaleMax, fontScaleMin);
			const float fontScaleStep = std::clamp(m_UiState->fontScaleStep, m_UiState->fontScaleStepMin, m_UiState->fontScaleStepMax);

			if (keyCode == ToNativeKeyCode(KeyCode::Minus))
				m_UiState->fontScale = std::clamp(m_UiState->fontScale - fontScaleStep, fontScaleMin, fontScaleMax);
			else if (keyCode == ToNativeKeyCode(KeyCode::Equal))
				m_UiState->fontScale = std::clamp(m_UiState->fontScale + fontScaleStep, fontScaleMin, fontScaleMax);
			else
				return;

			if (m_EventBus != nullptr)
				m_EventBus->Queue(EditorUiEvents::FontScaleChanged{m_UiState->fontScale});
			event.handled = true;
		};

		if (const auto *keyPressed = dynamic_cast<const KeyPressedEvent *>(&event))
			handleKey(keyPressed->GetKeyCode());
		else if (const auto *keyRepeated = dynamic_cast<const KeyRepeatedEvent *>(&event))
			handleKey(keyRepeated->GetKeyCode());
	}

	void EditorLayer::renderMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		EditorServices services = GetServices();
		for (MenuRendererEntry &entry : m_MenuRenderers)
		{
			if (entry.renderer && entry.id != "help")
				entry.renderer(services);
		}

		m_Windows->RenderMenu();
        if(ImGui::BeginMenu("Support")) {
            if(ImGui::MenuItem("About Semiconductor Studio")) m_ShowAbout=true;
            if(ImGui::MenuItem("Export diagnostics...")) {
                m_DiagnosticPreview=DiagnosticPreview(m_LogRegistry ? m_LogRegistry->Snapshot() : std::vector<LogEntry>{});
                m_SupportError.clear(); m_ShowDiagnostics=true;
            }
            ImGui::EndMenu();
        }
        for (auto& entry : m_MenuRenderers) if (entry.id == "help" && entry.renderer) entry.renderer(services);
        const float menuBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
        // ImGui applies menu work-area reservations on the following frame.
        auto *viewport = ImGui::GetMainViewport();
        const float reserved = std::max(0.0f, menuBottom - viewport->WorkPos.y);
        viewport->WorkPos.y += reserved;
        viewport->WorkSize.y = std::max(1.0f, viewport->WorkSize.y - reserved);
	}

	void EditorLayer::renderSupportDialogs()
	{
        const auto onMainViewport=[] {
            const auto* viewport=ImGui::GetMainViewport();
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowPos(viewport->GetWorkCenter(),ImGuiCond_Appearing,ImVec2(.5f,.5f));
        };
        if(!m_RecoveryChecked) {
            m_RecoveryChecked=true;
            try { m_InterruptedSessions=PreviousInterruptedSessions(); }
            catch(const std::exception& error) { m_SupportError=error.what(); }
        }
        if(!m_InterruptedSessions.empty() && !m_ShowDiagnostics && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) ImGui::OpenPopup("Previous session interrupted");
        onMainViewport();
        if(ImGui::BeginPopupModal("Previous session interrupted", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("A previous session ended unexpectedly. Local crash diagnostics are available if capture succeeded.");
            ImGui::TextWrapped("Check your project and saved results before continuing. This notice does not restore documents.");
            ImGui::Text("Interrupted sessions: %zu",m_InterruptedSessions.size());
            if(ImGui::Button("Review diagnostics")) {
                m_DiagnosticPreview=DiagnosticPreview(m_LogRegistry ? m_LogRegistry->Snapshot() : std::vector<LogEntry>{});
                m_ShowDiagnostics=true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Dismiss notice")) {
                bool ok=true;
                for(const auto& path:m_InterruptedSessions) if(!AcknowledgeInterruptedSession(path,m_SupportError)) ok=false;
                if(ok) { m_InterruptedSessions.clear(); ImGui::CloseCurrentPopup(); }
            }
            if(!m_SupportError.empty()) ImGui::TextWrapped("%s",m_SupportError.c_str());
            ImGui::EndPopup();
        }
        if(m_ShowAbout && !ImGui::IsPopupOpen("About Semiconductor Studio")) ImGui::OpenPopup("About Semiconductor Studio");
        onMainViewport();
        if(ImGui::BeginPopupModal("About Semiconductor Studio", &m_ShowAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto& build=GetBuildInfo();
            if(ImGui::BeginTable("##build-info",2)) {
                auto row=[](const char* label,std::string_view value){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(label);ImGui::TableNextColumn();ImGui::TextUnformatted(value.data(),value.data()+value.size());};
                row("Version",build.version); row("Commit",build.revision); row("Build UTC",build.builtUtc); row("Source SHA256",build.sourceSha256);
                row("Toolchain",build.toolchain); row("Configuration",build.configuration);
                ImGui::EndTable();
            }
            ImGui::TextUnformatted("Dependency SHA256");
            ImGui::BeginChild("##deps",ImVec2(720,180),true,ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(build.dependencySha256.data(),build.dependencySha256.data()+build.dependencySha256.size());
            ImGui::EndChild();
            if(build.revision=="unknown") ImGui::TextWrapped("Development build: source commit was not supplied by the release owner.");
            if(ImGui::Button("Copy build identity")) ImGui::SetClipboardText(BuildDescription().c_str());
            ImGui::SameLine(); if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {m_ShowAbout=false;ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
        if(m_ShowDiagnostics) {
            onMainViewport();
            ImGui::SetNextWindowSize(ImVec2(760,460),ImGuiCond_FirstUseEver);
            if(ImGui::Begin("Export diagnostics",&m_ShowDiagnostics)) {
                ImGui::TextWrapped("Preview the exact contents before saving. Nothing is uploaded. Dumps remain local and are excluded.");
                ImGui::BeginChild("##diagnostics-preview",ImVec2(0,-115),true,ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(m_DiagnosticPreview.c_str()); ImGui::EndChild();
                if(ImGui::Button("Save diagnostic report...")) {
                    try {
                        if(const auto path=SaveFileDialog({"Diagnostic report","txt"},"sst-diagnostics.txt")) {
                            if(auto commands=m_CommandRegistry.lock()) {
                                const auto result=ExecuteDiagnosticsExport(*commands,{*path,m_DiagnosticPreview});
                                m_SupportError=result ? "Diagnostic report saved. Review it before sharing." : result.Error().userMessage+" "+result.Error().technicalDetails+" Check destination permissions and free disk space.";
                            } else m_SupportError="Diagnostic command is unavailable. Restart the application and retry.";
                        }
                    } catch(const std::exception& error) { m_SupportError="Cannot export diagnostics: "+std::string(error.what())+". Check destination permissions and retry."; }
                }
                ImGui::TextWrapped("%s",m_SupportError.c_str());
                if(!m_InterruptedSessions.empty() && ImGui::Button("Dismiss recovery notice")) {
                    bool ok=true;
                    for(const auto& path:m_InterruptedSessions) if(!AcknowledgeInterruptedSession(path,m_SupportError)) ok=false;
                    if(ok) m_InterruptedSessions.clear();
                }
            }
            ImGui::End();
        }
	}

	void EditorLayer::renderCommandPalettePopup()
	{
		if (m_CommandPaletteOpenRequested)
		{
			ImGui::OpenPopup("Command Palette");
			m_CommandPaletteOpenRequested = false;
			m_CommandPaletteSelection = 0;
			m_CommandPaletteSearchBuffer.fill('\0');
		}

		bool open = true;
		if (!ImGui::BeginPopupModal("Command Palette", &open, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		auto commandRegistry = m_CommandRegistry.lock();
		if (commandRegistry == nullptr)
		{
			ImGui::TextDisabled("Command runtime unavailable.");
			if (ImGui::Button("Close"))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return;
		}

		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();

		ImGui::SetNextItemWidth(520.0f);
		const bool enterPressed = ImGui::InputTextWithHint(
			"##command_palette_search",
			"Search commands",
			m_CommandPaletteSearchBuffer.data(),
			m_CommandPaletteSearchBuffer.size(),
			ImGuiInputTextFlags_EnterReturnsTrue);

		CommandPaletteIndex palette(*commandRegistry);
		auto keymapResolver = m_KeymapResolver.lock();
		auto contextManager = m_ContextManager.lock();
		palette.SetKeymapResolver(keymapResolver.get(), contextManager.get());
		std::vector<CommandPaletteItem> rows = palette.Search(m_CommandPaletteSearchBuffer.data());

		if (m_CommandPaletteSelection >= static_cast<int>(rows.size()))
			m_CommandPaletteSelection = std::max(0, static_cast<int>(rows.size()) - 1);
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !rows.empty())
			m_CommandPaletteSelection = std::max(0, m_CommandPaletteSelection - 1);
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !rows.empty())
			m_CommandPaletteSelection = std::min(static_cast<int>(rows.size()) - 1, m_CommandPaletteSelection + 1);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();

		ImGui::Separator();
		if (ImGui::BeginChild("##command_palette_results", ImVec2(640.0f, 320.0f), true))
		{
			for (int index = 0; index < static_cast<int>(rows.size()); ++index)
			{
				const CommandPaletteItem &row = rows[static_cast<std::size_t>(index)];
				const std::string label = row.name + (row.shortcut.empty() ? "" : "    " + row.shortcut) + "##" + row.id.value;
				const bool selected = index == m_CommandPaletteSelection;
				ImGui::BeginDisabled(!row.enabled);
				if (ImGui::Selectable(label.c_str(), selected))
				{
					executeCommandFromPalette(row.id);
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				if (!row.description.empty())
					ImGui::TextDisabled("%s", row.description.c_str());
			}
			if (rows.empty())
				ImGui::TextDisabled("No commands found.");
		}
		ImGui::EndChild();

		if (enterPressed && !rows.empty() && rows[static_cast<std::size_t>(m_CommandPaletteSelection)].enabled)
		{
			executeCommandFromPalette(rows[static_cast<std::size_t>(m_CommandPaletteSelection)].id);
			ImGui::CloseCurrentPopup();
		}

		if (!open)
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void EditorLayer::executeCommandFromPalette(const CommandID &id)
	{
		auto commandRegistry = m_CommandRegistry.lock();
		if (commandRegistry == nullptr)
			return;

		CommandContext context(ContextID{"app.command_palette"});
		context.SetSource("Application command palette");
		auto result = commandRegistry->Execute(id, std::move(context));
		if (!result)
			DS_LOG_WARN("Command palette failed: {}", result.Error().technicalDetails);
	}

	void EditorLayer::onConfigApplied(const AppEvents::Config::Applied &event)
	{
		ApplyConfig(event.config);
	}

	void EditorLayer::onOpenCommandPaletteRequested(const CoreEvents::OpenCommandPaletteRequested &)
	{
		m_CommandPaletteOpenRequested = true;
	}
} // namespace SST
