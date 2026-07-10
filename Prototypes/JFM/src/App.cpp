#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "App/JFMCommands.hpp"
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
#include "Presentation/Panels/ImportStatusPanel.hpp"
#include "Presentation/Panels/JFMOverviewPanel.hpp"
#include "Presentation/Panels/ProjectTreePanel.hpp"
#include "Presentation/Panels/RawPlotPanel.hpp"
#include "Storage/JFMWorkspaceStore.hpp"

namespace SST
{	
	using namespace Prototypes::JFM;

	void RegisterJFMEditor(Application &application, Ref<JFMWorkspaceStore> workspace)
	{
		
		EditorLayer &editorLayer = application.GetEditorLayer();

		JFMPanelIds panelIds;
		panelIds.projectTree = editorLayer.RegisterPanel<ProjectTreePanel>(workspace);
		panelIds.overview = editorLayer.RegisterPanel<JFMOverviewPanel>(workspace);
		panelIds.importStatus = editorLayer.RegisterPanel<ImportStatusPanel>(workspace);
		panelIds.rawPlot = editorLayer.RegisterPanel<RawPlotPanel>(workspace);
		editorLayer.RegisterMenuRenderer(
			"jfm.menu",
			std::bind(&RenderJFMMenu, std::ref(editorLayer), panelIds, std::placeholders::_1));
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
		RegisterJFMIOHandlers(
			application.GetIOLayer(),
			JFMIORegistrationContext{
				eventBus,
				workspace,
				notifier,
				ResolveJFMPythonRoot(*runtime.config)});
		application.InsertLayerBefore(LayerId::ImGui, CreateUnique<JFMDomainLayer>(workspace));
		RegisterJFMEditor(application, workspace);
		RegisterJFMCommands(JFMCommandRegistrationContext{
			eventBus,
			runtime.commandRegistry,
			runtime.keymapResolver,
			workspace});
	}
} // namespace SST
