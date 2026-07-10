#pragma once

#include "Core/Utils/Memory.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST
{
	class CommandRegistry;
	class EventBus;
	class KeymapResolver;
}

namespace SST::Prototypes::JFM
{
	struct JFMCommandRegistrationContext
	{
		Ref<EventBus> eventBus;
		WeakRef<CommandRegistry> commandRegistry;
		WeakRef<KeymapResolver> keymapResolver;
		Ref<JFMWorkspaceStore> workspace;
	};

	void RegisterJFMCommands(JFMCommandRegistrationContext context);
} // namespace SST::Prototypes::JFM
