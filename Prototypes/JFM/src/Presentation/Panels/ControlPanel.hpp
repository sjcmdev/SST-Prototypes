#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/Utils/Memory.hpp"
#include "App/JFMUserSettings.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <array>
#include <vector>

namespace SST
{
	class CommandRegistry;
	class UndoStack;
}

namespace SST::Prototypes::JFM
{
	class ControlPanel final : public IPanel
	{
	public:
		ControlPanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<EventBus> eventBus,
			WeakRef<CommandRegistry> commandRegistry,
			WeakRef<UndoStack> undoStack);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		void Execute(const char *commandId, CommandContext context = {}) const;
		void FinishRangeUndoGroup(bool cancel = false);
		void RenderModelControls(ProjectHandle project, const std::vector<CharacteristicId> &ids);
		void RenderPrefitDisplayControls();
		void RenderColorControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicInfo &activeInfo);
		void RenderRangeControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicRecord &activeRecord);
		void RenderSolverControls(ProjectHandle project, const std::vector<CharacteristicId> &ids, const JFMCharacteristicInfo &activeInfo);
		void RenderParameterTable(const JFMCharacteristicInfo &activeInfo) const;

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<EventBus> m_EventBus;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<UndoStack> m_UndoStack;
		CharacteristicId m_RangeCharacteristic = InvalidCharacteristicId;
		int m_LowerIndex = 0;
		int m_UpperIndex = 0;
		bool m_RangeUndoGroupOpen = false;
		bool m_RangeHeldLastFrame = false;
		std::array<float, 4> m_ColorDraft = {0.95f, 0.45f, 0.08f, 1.0f};
		int m_CovarianceX = 0;
		int m_CovarianceY = 1;
	};
} // namespace SST::Prototypes::JFM
