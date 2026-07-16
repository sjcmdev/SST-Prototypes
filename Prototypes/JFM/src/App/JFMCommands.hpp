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
	inline constexpr const char *JFMApplyProjectEditCommandId = "jfm.project.apply_edit";

	struct JFMProjectEditRequest
	{
		using Apply = std::function<Result<void>(JFMWorkspaceStore &, ProjectHandle)>;

		ProjectHandle project = InvalidProjectHandle;
		std::string description;
		Apply apply;
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
