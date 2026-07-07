#include "Core/dspch.hpp"

#include "Storage/__PROTOTYPE_NAME__StorageLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	__PROTOTYPE_NAME__StorageLayer::__PROTOTYPE_NAME__StorageLayer(Ref<PrototypeResultStore> store)
		: Layer("__PROTOTYPE_NAME__Storage"), m_Store(std::move(store))
	{
	}

	void __PROTOTYPE_NAME__StorageLayer::OnAttach()
	{
		DS_LOG_INFO("__PROTOTYPE_NAME__ storage layer attached");
	}
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

