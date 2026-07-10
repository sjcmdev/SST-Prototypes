#pragma once

#include "App/ApplicationState.hpp"
#include "Core/Utils/Memory.hpp"

namespace SST
{
	class CommandRegistry;
	class ContextManager;
	class EventBus;
	class JobSystem;
	class KeymapResolver;
	class Notifier;
	class ProgressTracker;

	struct ApplicationRuntimeServices
	{
		WeakRef<EventBus> eventBus;
		WeakRef<JobSystem> jobSystem;
		WeakRef<ProgressTracker> progressTracker;
		WeakRef<CommandRegistry> commandRegistry;
		WeakRef<KeymapResolver> keymapResolver;
		WeakRef<ContextManager> contextManager;
		WeakRef<Notifier> notifier;
		const ApplicationConfig *config = nullptr;
		Path configDirectory;
		ApplicationPaths paths;
	};
} // namespace SST
