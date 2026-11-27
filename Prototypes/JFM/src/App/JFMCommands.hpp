#pragma once

#include <functional>
#include <string>

#include "Core/Commands/Command.hpp"
#include "Core/Utils/Memory.hpp"
#include "App/JFMUserSettings.hpp"
#include "Presentation/Panels/JFMImportDialogPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST
{
	class CommandRegistry;
	class EventBus;
	class KeymapResolver;
}

namespace SST::Prototypes::JFM
{
    inline constexpr const char *JFMExportMonteCarloTsvCommandId = "jfm.export.mc_samples_tsv";
	inline constexpr const char *JFMApplyProjectEditCommandId = "jfm.project.apply_edit";
	inline constexpr const char *JFMImportApplyCommandId = "jfm.import.apply";
	inline constexpr const char *JFMAddGroupCommandId = "jfm.tree.add_group";
	inline constexpr const char *JFMAddSeriesCommandId = "jfm.tree.add_series";
	inline constexpr const char *JFMDeleteTreeItemCommandId = "jfm.tree.delete";
	inline constexpr const char *JFMMoveCharacteristicsCommandId = "jfm.tree.move_characteristics";
	inline constexpr const char *JFMRenameTreeItemCommandId = "jfm.tree.rename";
	inline constexpr const char *JFMSetShortNameCommandId = "jfm.tree.set_short_name";
	inline constexpr const char *JFMCopyTreeItemCommandId = "jfm.tree.copy";
	inline constexpr const char *JFMPasteTreeItemCommandId = "jfm.tree.paste";
	inline constexpr const char *JFMDuplicateTreeItemCommandId = "jfm.tree.duplicate";
	inline constexpr const char *JFMRequestRenameTreeItemCommandId = "jfm.tree.request_rename";
	inline constexpr const char *JFMRequestPasteTreeItemCommandId = "jfm.tree.request_paste";
	inline constexpr const char *JFMRequestDuplicateTreeItemCommandId = "jfm.tree.request_duplicate";
	inline constexpr const char *JFMAddScriptCommandId = "jfm.script.add";
	inline constexpr const char *JFMReloadScriptCommandId = "jfm.script.reload";
	inline constexpr const char *JFMRemoveScriptCommandId = "jfm.script.remove";
	inline constexpr const char *JFMUpsertGroupingCommandId = "jfm.grouping.upsert";
	inline constexpr const char *JFMDeleteGroupingCommandId = "jfm.grouping.delete";
	inline constexpr const char *JFMSetPlotColorCommandId = "jfm.characteristic.set_plot_color";
	inline constexpr const char *JFMSetCharacteristicMetadataCommandId = "jfm.characteristic.set_metadata";
	inline constexpr const char *JFMSetPrefitModelCommandId = "jfm.prefit.set_model";
	inline constexpr const char *JFMSetFitRangeCommandId = "jfm.prefit.set_range";
	inline constexpr const char *JFMResetFitRangeCommandId = "jfm.prefit.reset_range";
	inline constexpr const char *JFMAutoDetectPrefitCommandId = "jfm.prefit.auto_detect";
	inline constexpr const char *JFMExportArrheniusTsvCommandId = "jfm.export.arrhenius_tsv";
	inline constexpr const char *JFMExportCharacteristicsFitTsvCommandId = "jfm.export.characteristics_fit_tsv";
	inline constexpr const char *JFMOpenProjectBundleCommandId = "jfm.project.open_bundle";
	inline constexpr const char *JFMSaveProjectBundleAsCommandId = "jfm.project.save_bundle_as";

	struct JFMProjectEditRequest
	{
		using Apply = std::function<Result<void>(JFMWorkspaceStore &, ProjectHandle)>;
		using AfterChange = std::function<void()>;

		ProjectHandle project = InvalidProjectHandle;
		std::string description;
		Apply apply;
		AfterChange afterChange;
	};

	struct JFMCommandRegistrationContext
	{
		Ref<EventBus> eventBus;
		WeakRef<CommandRegistry> commandRegistry;
		WeakRef<KeymapResolver> keymapResolver;
		Ref<JFMWorkspaceStore> workspace;
		Ref<JFMUserSettingsStore> settings;
		Ref<JFMImportDialogState> importDialog;
	};

	[[nodiscard]] Unique<ICommand> CreateJFMProjectEditCommand(Ref<JFMWorkspaceStore> workspace, JFMProjectEditRequest request);
	void RegisterJFMCommands(JFMCommandRegistrationContext context);
} // namespace SST::Prototypes::JFM
