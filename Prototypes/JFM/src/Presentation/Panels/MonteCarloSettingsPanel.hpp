#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST
{
	class CommandRegistry;
}

namespace SST::Prototypes::JFM
{
	class MonteCarloSettingsPanel final : public IPanel
	{
	public:
		MonteCarloSettingsPanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<EventBus> eventBus,
			WeakRef<CommandRegistry> commandRegistry,
			std::string title = "MC Settings",
			bool visibleByDefault = false);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		void Execute(const char *commandId, CommandContext context = {}) const;
		void ApplyDraft();

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<EventBus> m_EventBus;
		WeakRef<CommandRegistry> m_CommandRegistry;
		JFMFitMonteCarloSettings m_Draft;
		bool m_DraftInitialized = false;
		std::string m_Status;
	};
} // namespace SST::Prototypes::JFM
