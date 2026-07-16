#pragma once

#include "Core/Utils/Memory.hpp"
#include "App/JFMUserSettings.hpp"
#include "App/JFMCommands.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Input/ContextManager.hpp"
#include "Presentation/Panels/JFMImportDialogPanel.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <array>
#include <string>
#include <vector>

namespace SST::Prototypes::JFM
{
	class ProjectTreePanel final : public IPanel, public EventReceiver
	{
	public:
		explicit ProjectTreePanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<JFMImportDialogState> importDialog,
			Ref<EventBus> eventBus,
			WeakRef<CommandRegistry> commandRegistry,
			WeakRef<ContextManager> contextManager,
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

		struct TreeRenderState
		{
			CharacteristicId primarySelection = InvalidCharacteristicId;
			std::vector<CharacteristicId> selectedIds;
			std::vector<CharacteristicId> visibleCharacteristicIds;
		};

		void FocusItem(RenameTarget target, std::string id);
		[[nodiscard]] JFMTreeItemRef MakeTreeItem(RenameTarget target, std::string id, std::vector<CharacteristicId> characteristicIds = {}) const;
		void StartRename(RenameTarget target, std::string id, std::string name);
		void StartRename(const JFMTreeItemRef &item);
		void StartShortNameEdit(const JFMProject &project, RenameTarget target, std::string id, const std::string &fallbackName);
		void ExecuteCommand(const char *commandId, CommandContext context = {});
		void QueuePlot(std::vector<CharacteristicId> ids, std::string title);
		void OpenImportDialog(std::string targetSeriesId = {}, std::string targetGroupId = {});
		void OpenImportFolderDialog(std::string targetSeriesId, std::string targetGroupId, bool recursive);
		void QueuePlotsForProjectGroups(const JFMProject &project);
		void QueuePlotsForChildGroups(const JFMProject &project, const std::string &parentGroupId);
		void RenderProjectNode(ProjectHandle projectHandle, const JFMProject &project, TreeRenderState &state);
		void RenderGroupNode(const JFMProject &project, const JFMGroup &group, TreeRenderState &state);
		void RenderSeriesNode(const JFMProject &project, const JFMSeries &series, TreeRenderState &state);
		void RenderCharacteristicNode(const JFMSeries &series, CharacteristicId characteristicId, const std::vector<CharacteristicId> &visibleIds, TreeRenderState &state);
		void RenderGroupingsNode(const JFMProject &project);
		void OpenGroupingPopup(const JFMProject &project, const JFMGroupingDefinition *grouping = nullptr);
		void RenderGroupingPopup(const JFMProject &project);
		void QueueGroupingPlot(const JFMGroupingDefinition &grouping);
		void RenderScriptsNode(const JFMProject &project);
		void RenderRenamePopup();
		void CommitRename();
		void RenderShortNamePopup();
		void CommitShortName();
		void RenderPastePopup(const JFMProject &project);
		void HandleKeyboard(const JFMProject &project, TreeRenderState &state);
		void ExportGroup(const JFMProject &project, const JFMGroup &group);
		void CopyFocused(const JFMProject &project, const std::vector<CharacteristicId> &selectedIds);
		void OpenPastePrompt(bool duplicateFocused);
		void PasteClipboard(const JFMProject &project, bool extractToGroup);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<JFMImportDialogState> m_ImportDialog;
		Ref<EventBus> m_EventBus;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<ContextManager> m_ContextManager;
		ProjectHandle m_CurrentProject = InvalidProjectHandle;
		int m_SortMode = 0;
		RenameTarget m_FocusedTarget = RenameTarget::None;
		std::string m_FocusedTargetId;
		RenameTarget m_RenameTarget = RenameTarget::None;
		std::string m_RenameTargetId;
		bool m_OpenRenamePopup = false;
		std::array<char, 160> m_RenameBuffer = {};
		RenameTarget m_ShortNameTarget = RenameTarget::None;
		std::string m_ShortNameTargetId;
		std::vector<CharacteristicId> m_ShortNameTargetIds;
		bool m_OpenShortNamePopup = false;
		std::array<char, 160> m_ShortNameBuffer = {};
		bool m_OpenPastePopup = false;
		bool m_PasteDuplicateFocused = false;
		bool m_OpenGroupingPopup = false;
		std::string m_EditingGroupingId;
		std::array<char, 128> m_GroupingName = {};
		std::array<char, 128> m_GroupingKey = {};
		int m_GroupingMode = 1;
		float m_GroupingStep = 1.0f;
		bool m_GroupingIncludeMissing = true;
		std::vector<std::string> m_GroupingSeriesIds;
	};
} // namespace SST::Prototypes::JFM
