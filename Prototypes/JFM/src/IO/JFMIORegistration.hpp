#pragma once

#include "Core/Utils/Memory.hpp"
#include "Storage/JFMWorkspaceStore.hpp"

namespace SST
{
	class IOLayer;
}

namespace SST::Prototypes::JFM
{
	void RegisterJFMIOHandlers(IOLayer &ioLayer, Ref<JFMWorkspaceStore> store);
} // namespace SST::Prototypes::JFM
