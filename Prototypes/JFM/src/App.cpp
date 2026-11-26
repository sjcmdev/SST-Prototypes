#include "Core/dspch.hpp"
#include "Core/Presentation/ProjectRecoveryLayer.hpp"
#include "IO/JFMProjectBundleSerializer.hpp"
#include "IO/JFMProjectJobs.hpp"

#include "App/Application.hpp"
#include "App/JFMCommands.hpp"
#include "IO/JFMPlotExport.hpp"
#include "IO/JFMFitJobs.hpp"
#include "App/JFMUserSettings.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/JFMDomainLayer.hpp"
#include "IO/IOLayer.hpp"
#include "IO/JFMIORegistration.hpp"
#include "IO/JFMImportPaths.hpp"
#include "IO/JFMPythonBindings.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/JFMMenu.hpp"
#include "Presentation/Panels/ArrheniusPlotPanel.hpp"
#include "Presentation/Panels/CharacteristicDetailsPanel.hpp"
#include "Presentation/Panels/ControlPanel.hpp"
#include "Presentation/Panels/ImportStatusPanel.hpp"
#include "Presentation/Panels/JFMImportDialogPanel.hpp"
#include "Presentation/Panels/JFMOverviewPanel.hpp"
#include "Presentation/Panels/JFMSettingsPanel.hpp"
#include "Presentation/Panels/MonteCarloCurvesPanel.hpp"
#include "Presentation/Panels/MonteCarloScatterPanel.hpp"
#include "Presentation/Panels/MonteCarloSettingsPanel.hpp"
#include "Presentation/Panels/ProjectTreePanel.hpp"
#include "Presentation/Panels/RawPlotPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"
#include "Presentation/Panels/WindowWorkspace.hpp"
#include <imgui_internal.h>

namespace SST
{	
	using namespace Prototypes::JFM;

	void RegisterJFMEditor(
		Application &application,
		Ref<JFMWorkspaceStore> workspace,
		Ref<JFMUserSettingsStore> settings,
		Ref<JFMImportDialogState> importDialog)
	{
		
		EditorLayer &editorLayer = application.GetEditorLayer();

		JFMPanelIds panelIds;
		panelIds.projectTree = editorLayer.RegisterPanel<ProjectTreePanel>(
			workspace,
			settings,
			importDialog,
			application.GetEventBusRef(),
			application.GetRuntimeServices().commandRegistry,
			application.GetRuntimeServices().contextManager);
		panelIds.overview = editorLayer.RegisterPanel<JFMOverviewPanel>(workspace);
		panelIds.importStatus = editorLayer.RegisterPanel<ImportStatusPanel>(workspace);
		panelIds.rawPlot = editorLayer.RegisterPanel<RawPlotPanel>(
			workspace,
			settings,
			application.GetEventBusRef(),
			application.GetRuntimeServices().commandRegistry,
			application.GetRuntimeServices().undoStack);
		panelIds.details = editorLayer.RegisterPanel<CharacteristicDetailsPanel>(workspace);
		panelIds.control = editorLayer.RegisterPanel<ControlPanel>(
			workspace,
			settings,
			application.GetEventBusRef(),
			application.GetRuntimeServices().commandRegistry,
			application.GetRuntimeServices().undoStack);
		panelIds.monteCarloSettings = editorLayer.RegisterPanel<MonteCarloSettingsPanel>(
			workspace,
			settings,
			application.GetEventBusRef(),
			application.GetRuntimeServices().commandRegistry);
		panelIds.monteCarloScatter = editorLayer.RegisterPanel<MonteCarloScatterPanel>(
			workspace,
			settings,
			application.GetEventBusRef());
		panelIds.monteCarloCurves = editorLayer.RegisterPanel<MonteCarloCurvesPanel>(
			workspace,
			application.GetEventBusRef());
		panelIds.arrhenius = editorLayer.RegisterPanel<ArrheniusPlotPanel>(workspace);
		// Keep analysis tools available from View without occupying the startup plot dock.
		for (auto id : {panelIds.details, panelIds.control, panelIds.monteCarloSettings, panelIds.arrhenius})
			if (auto panel = editorLayer.FindPanel(id).lock()) panel->SetVisible(false);
		editorLayer.RegisterPanel<JFMImportDialogPanel>(
			application.GetEventBusRef(),
			workspace,
			importDialog,
			settings,
			application.GetRuntimeServices().commandRegistry,
			application.GetRuntimeServices().undoStack);
		auto jfmSettingsPanel = CreateRef<JFMSettingsPanel>(settings);
		editorLayer.RegisterSettingsTab(SettingsTabExtension{
			"jfm",
			"JFM",
			[jfmSettingsPanel]() {
				jfmSettingsPanel->RenderContent();
			}});
        const auto windows = editorLayer.GetWindows();
        windows->SetDefaultLayout([owner = WeakRef<WindowWorkspace>(windows), panelIds] {
            const auto windows = owner.lock(); if (!windows) return;
            const auto* viewport = ImGui::GetMainViewport();
            ImGuiID center = viewport->ID;
            ImGui::DockBuilderRemoveNode(center);
            ImGui::DockBuilderAddNode(center, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(center, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(center, viewport->WorkSize);
            const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .22f, nullptr, &center);
            const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .25f, nullptr, &center);
            const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .28f, nullptr, &center);
            for (const auto& window : windows->Windows()) {
                const auto& type = window->GetType();
                const bool rawPlot = type.starts_with("hosted.###jfm_raw_plot_");
                if ((!type.starts_with("jfm.") && !rawPlot) || type == "jfm.settings") continue;
                auto state = window->GetState();
                state.mode = state.restoreMode = WindowMode::Normal; state.pinned = false; state.visible = true;
                state.dockId = window->GetId() == panelIds.projectTree ? left :
                    window->GetId() == panelIds.overview || window->GetId() == panelIds.importStatus ? bottom :
                    window->GetId() == panelIds.details || window->GetId() == panelIds.control || window->GetId() == panelIds.monteCarloSettings ? right : center;
                window->ApplyState(state);
                ImGui::DockBuilderDockWindow(window->ImGuiName().c_str(), state.dockId);
            }
            ImGui::DockBuilderFinish(viewport->ID);
        });
        editorLayer.RegisterMenuRenderer("jfm.layout", [windows, initialized = false](EditorServices&) mutable {
            if (!initialized) {
                initialized = true; windows->RestoreDefaultLayoutIfNeeded("jfm.");
                windows->RestoreDefaultLayoutIfNeeded("hosted.###jfm_raw_plot_");
            }
        });
		editorLayer.RegisterMenuRenderer(
			"jfm.menu",
			std::bind(&RenderJFMMenu, std::ref(editorLayer), panelIds, workspace, settings, std::placeholders::_1));
	}
	

	void SetupApplication(Application &application)
	{
		const ApplicationRuntimeServices &runtime = application.GetRuntimeServices();
		DS_ASSERT(runtime.config != nullptr, "JFM setup requires application config");
		Ref<EventBus> eventBus = runtime.eventBus.lock();
		Ref<Notifier> notifier = runtime.notifier.lock();
		DS_ASSERT(eventBus != nullptr, "JFM setup requires EventBus");

		RegisterJFMPythonBindings();

		auto workspace = CreateRef<JFMWorkspaceStore>();
        application.InsertLayerBefore(LayerId::ImGui, CreateUnique<ProjectRecoveryLayer>(runtime.configDirectory/"recovery",runtime.jobSystem.lock(),
            [workspace](ProjectRecoveryPlan& recovery) {
                PrepareJFMRecovery(workspace,recovery);
            },
            [workspace](const RecoveryCandidate& candidate) {
                auto restored=JFMProjectBundleSerializer::Load(candidate.snapshot);
                if (!restored) throw std::runtime_error(restored.Error().userMessage+" "+restored.Error().technicalDetails);
                (void)workspace->AttachProjectBundle(std::move(restored.Value().snapshot),{},true,true);
            }));
		auto settings = CreateRef<JFMUserSettingsStore>(runtime.configDirectory);
		settings->Load();
		workspace->EnsureDefaultScripts(settings->GetDefaultScriptsDirectory());
		auto importDialog = CreateRef<JFMImportDialogState>();
		RegisterJFMIOHandlers(
			application.GetIOLayer(),
			JFMIORegistrationContext{
				eventBus,
				workspace,
				notifier,
				settings,
				runtime.commandRegistry,
				ResolveJFMPythonRoot(*runtime.config)});
		application.InsertLayerBefore(LayerId::ImGui, CreateUnique<JFMDomainLayer>(workspace));
		RegisterJFMEditor(application, workspace, settings, importDialog);
		ConfigureJFMPlotServices(runtime.commandRegistry, application.GetJobSystemHandle());
        ConfigureJFMFitServices(runtime.commandRegistry, application.GetJobSystemHandle());
		RegisterJFMCommands(JFMCommandRegistrationContext{
			eventBus,
			runtime.commandRegistry,
			runtime.keymapResolver,
			workspace,
			settings,
			importDialog});
	}
} // namespace SST
