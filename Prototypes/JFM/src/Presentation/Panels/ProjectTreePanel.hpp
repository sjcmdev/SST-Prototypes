#pragma once

#include "Core/Utils/Memory.hpp"
#include "App/JFMUserSettings.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Presentation/Panels/JFMImportDialogPanel.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class ProjectTreePanel final : public IPanel
	{
	public:
		explicit ProjectTreePanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<JFMImportDialogState> importDialog,
			Ref<EventBus> eventBus,
			std::string title = "Project Tree",
			bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<JFMImportDialogState> m_ImportDialog;
		Ref<EventBus> m_EventBus;
		int m_SortMode = 0;
	};
} // namespace SST::Prototypes::JFM
