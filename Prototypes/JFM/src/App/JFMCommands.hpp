#pragma once

#include "App/Application.hpp"
#include "Core/Utils/Memory.hpp"
#include "Storage/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	void RegisterJFMCommands(Application &application, Ref<JFMWorkspaceStore> workspace);
} // namespace SST::Prototypes::JFM
