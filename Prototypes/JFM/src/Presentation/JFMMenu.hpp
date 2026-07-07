#pragma once

#include "Presentation/EditorLayer.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMPanelIds
	{
		PanelId projectTree = 0;
		PanelId overview = 0;
		PanelId importStatus = 0;
		PanelId rawPlot = 0;
	};

	void RenderJFMMenu(EditorLayer &editorLayer, const JFMPanelIds &panelIds, EditorServices &services);
} // namespace SST::Prototypes::JFM
