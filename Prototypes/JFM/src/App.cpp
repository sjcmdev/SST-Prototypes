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
	namespace
	{
		void RegisterJFMEditor(Application &application, Ref<Prototypes::JFM::JFMWorkspaceStore> workspace)
		{
			EditorLayer &editorLayer = application.GetEditorLayer();

			Prototypes::JFM::JFMPanelIds panelIds;
			panelIds.projectTree = editorLayer.RegisterPanel<Prototypes::JFM::ProjectTreePanel>(workspace);
			panelIds.overview = editorLayer.RegisterPanel<Prototypes::JFM::JFMOverviewPanel>(workspace);
			panelIds.importStatus = editorLayer.RegisterPanel<Prototypes::JFM::ImportStatusPanel>(workspace);
			panelIds.rawPlot = editorLayer.RegisterPanel<Prototypes::JFM::RawPlotPanel>(workspace);
			editorLayer.RegisterMenuRenderer(
				"jfm.menu",
				std::bind(&Prototypes::JFM::RenderJFMMenu, std::ref(editorLayer), panelIds, std::placeholders::_1));
		}
	}

	void SetupApplication(Application &application)
	{
		auto workspace = CreateRef<Prototypes::JFM::JFMWorkspaceStore>();
		Prototypes::JFM::RegisterJFMIOHandlers(application.GetIOLayer(), workspace);
		application.InsertLayerBefore(LayerId::ImGui, CreateUnique<Prototypes::JFM::JFMDomainLayer>(workspace));
		RegisterJFMEditor(application, workspace);
		Prototypes::JFM::RegisterJFMCommands(application, workspace);
	}
} // namespace SST
