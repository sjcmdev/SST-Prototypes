#pragma once

#include "App/JFMUserSettings.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMPanelIds
	{
		PanelId projectTree = 0;
		PanelId overview = 0;
		PanelId importStatus = 0;
		PanelId rawPlot = 0;
		PanelId details = 0;
		PanelId control = 0;
		PanelId monteCarloSettings = 0;
		PanelId monteCarloScatter = 0;
		PanelId monteCarloCurves = 0;
	};

	void RenderJFMMenu(
		EditorLayer &editorLayer,
		const JFMPanelIds &panelIds,
		Ref<JFMWorkspaceStore> workspace,
		Ref<JFMUserSettingsStore> settings,
		EditorServices &services);
} // namespace SST::Prototypes::JFM
