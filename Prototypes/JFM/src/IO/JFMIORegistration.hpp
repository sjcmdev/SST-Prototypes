#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Notifications/Notifier.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Memory.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST
{
	class IOLayer;
}

namespace SST::Prototypes::JFM
{
	struct JFMIORegistrationContext
	{
		Ref<EventBus> eventBus;
		Ref<JFMWorkspaceStore> store;
		Ref<Notifier> notifier;
		Path pythonRoot;
	};

	void RegisterJFMIOHandlers(IOLayer &ioLayer, JFMIORegistrationContext context);
} // namespace SST::Prototypes::JFM
