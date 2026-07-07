#include "Core/dspch.hpp"

#include "App/Application.hpp"
#include "App/JFMCommands.hpp"
#include "Core/EntryPoint.hpp"
#include "Core/Utils/Memory.hpp"
#include "Domain/JFMDomainLayer.hpp"
#include "IO/JFMIOLayer.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/JFMMenu.hpp"
#include "Presentation/Panels/ImportStatusPanel.hpp"
#include "Presentation/Panels/JFMOverviewPanel.hpp"
#include "Presentation/Panels/ProjectTreePanel.hpp"
#include "Presentation/Panels/RawPlotPanel.hpp"
#include "Storage/JFMStorageLayer.hpp"
#include "Storage/JFMWorkspaceStore.hpp"

namespace SST
{
	Application CreateApplication(int argc, char **argv)
	{
		// TODO: to nie powinno być jako lambda. Ta funkcja powinna być maksymalnie prosta
		return Application::Create(argc, argv, [](Application &application) {
			auto workspace = CreateRef<Prototypes::JFM::JFMWorkspaceStore>();
			EditorLayer &editorLayer = application.GetEditorLayer();

			Prototypes::JFM::JFMPanelIds panelIds;
			panelIds.projectTree = editorLayer.RegisterPanel<Prototypes::JFM::ProjectTreePanel>(workspace);
			panelIds.overview = editorLayer.RegisterPanel<Prototypes::JFM::JFMOverviewPanel>(workspace);
			panelIds.importStatus = editorLayer.RegisterPanel<Prototypes::JFM::ImportStatusPanel>(workspace);
			panelIds.rawPlot = editorLayer.RegisterPanel<Prototypes::JFM::RawPlotPanel>(workspace);
			editorLayer.RegisterMenuRenderer(
				"jfm.menu",
				[&editorLayer, panelIds](EditorServices &services) {
					Prototypes::JFM::RenderJFMMenu(editorLayer, panelIds, services);
				});

			Prototypes::JFM::RegisterJFMCommands(application, workspace);
			application.PushLayer(CreateUnique<Prototypes::JFM::JFMStorageLayer>(workspace));
			application.PushLayer(CreateUnique<Prototypes::JFM::JFMIOLayer>(workspace));
			application.PushLayer(CreateUnique<Prototypes::JFM::JFMDomainLayer>(workspace));
		});
	}
} // namespace SST
