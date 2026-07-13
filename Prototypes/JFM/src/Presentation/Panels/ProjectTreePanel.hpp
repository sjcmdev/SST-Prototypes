#pragma once

#include "Core/Utils/Memory.hpp"
#include "App/JFMUserSettings.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Presentation/JFMPlotRequestState.hpp"
#include "Presentation/Panels/JFMImportDialogPanel.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <array>
#include <string>

namespace SST::Prototypes::JFM
{
	class ProjectTreePanel final : public IPanel
	{
	public:
		explicit ProjectTreePanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<JFMImportDialogState> importDialog,
			Ref<JFMPlotRequestState> plotRequests,
			Ref<EventBus> eventBus,
			std::string title = "Project Tree",
			bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		enum class RenameTarget
		{
			None,
			Project,
			Group,
			Series,
			Characteristic
		};

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<JFMImportDialogState> m_ImportDialog;
		Ref<JFMPlotRequestState> m_PlotRequests;
		Ref<EventBus> m_EventBus;
		int m_SortMode = 0;
		RenameTarget m_FocusedTarget = RenameTarget::None;
		std::string m_FocusedTargetId;
		std::string m_FocusedTargetName;
		RenameTarget m_RenameTarget = RenameTarget::None;
		std::string m_RenameTargetId;
		bool m_OpenRenamePopup = false;
		std::array<char, 160> m_RenameBuffer = {};
	};
} // namespace SST::Prototypes::JFM
