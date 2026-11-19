#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Presentation/Panels/EditorWindow.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST
{
	class CommandRegistry;
}

namespace SST::Prototypes::JFM
{
	class MonteCarloSettingsPanel final : public EditorWindow
	{
	public:
		MonteCarloSettingsPanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<EventBus> eventBus,
			WeakRef<CommandRegistry> commandRegistry,
			std::string title = "MC Settings",
			bool visibleByDefault = true);

		[[nodiscard]] WindowDuplicate Duplicate() const override;
		void RenderContent() override;

	private:
		void Execute(const char *commandId, CommandContext context = {}) const;
		void ApplyDraft();
		void RunMonteCarlo(ProjectHandle project, const std::vector<CharacteristicId> &ids);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<EventBus> m_EventBus;
		WeakRef<CommandRegistry> m_CommandRegistry;
		JFMFitMonteCarloSettings m_Draft;
		bool m_DraftInitialized = false;
		bool m_BulkMode = false;
		std::string m_Status;
	};
} // namespace SST::Prototypes::JFM
