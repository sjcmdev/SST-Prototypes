#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "App/JFMCommands.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/JFMDomainLayer.hpp"
#include "IO/IOLayer.hpp"
#include "IO/JFMIORegistration.hpp"
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
		auto workspace = CreateRef<JFMWorkspaceStore>();
		RegisterJFMIOHandlers(application.GetIOLayer(), workspace);
		application.InsertLayerBefore(LayerId::ImGui, CreateUnique<JFMDomainLayer>(workspace));
		RegisterJFMEditor(application, workspace);
		RegisterJFMCommands(application, workspace);
	}
} // namespace SST
