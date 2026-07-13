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
#include <vector>

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

		struct TreeRenderState
		{
			CharacteristicId primarySelection = InvalidCharacteristicId;
			std::vector<CharacteristicId> selectedIds;
			std::vector<CharacteristicId> visibleCharacteristicIds;
		};

		void FocusItem(RenameTarget target, std::string id, std::string name);
		void StartRename(RenameTarget target, std::string id, std::string name);
		void StartShortNameEdit(const JFMProject &project, RenameTarget target, std::string id, const std::string &fallbackName);
		void QueuePlot(std::vector<CharacteristicId> ids, std::string title);
		void OpenImportDialog(std::string targetSeriesId = {});
		void OpenImportFolderDialog(std::string targetSeriesId, bool recursive);
		void RenderProjectNode(const JFMProject &project, TreeRenderState &state);
		void RenderGroupNode(const JFMProject &project, const JFMGroup &group, TreeRenderState &state);
		void RenderSeriesNode(const JFMProject &project, const JFMSeries &series, TreeRenderState &state);
		void RenderCharacteristicNode(const JFMSeries &series, CharacteristicId characteristicId, const std::vector<CharacteristicId> &visibleIds, TreeRenderState &state);
		void RenderScriptsNode(const JFMProject &project);
		void RenderRenamePopup();
		void RenderShortNamePopup();
		void RenderPastePopup(const JFMProject &project);
		void HandleKeyboard(const JFMProject &project, TreeRenderState &state);
		void ExportGroup(const JFMProject &project, const JFMGroup &group);
		void CopyFocused(const JFMProject &project, const std::vector<CharacteristicId> &selectedIds);
		void OpenPastePrompt(bool duplicateFocused);
		void PasteClipboard(const JFMProject &project, bool extractToGroup);

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
		RenameTarget m_ShortNameTarget = RenameTarget::None;
		std::string m_ShortNameTargetId;
		bool m_OpenShortNamePopup = false;
		std::array<char, 160> m_ShortNameBuffer = {};
		RenameTarget m_ClipboardTarget = RenameTarget::None;
		std::string m_ClipboardTargetId;
		std::vector<CharacteristicId> m_ClipboardCharacteristicIds;
		bool m_OpenPastePopup = false;
		bool m_PasteDuplicateFocused = false;
	};
} // namespace SST::Prototypes::JFM
