#include "Core/dspch.hpp"

#include "Storage/JFMStorageLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::JFM
{
	JFMStorageLayer::JFMStorageLayer(Ref<PrototypeResultStore> store)
		: Layer("JFMStorage"), m_Store(std::move(store))
	{
	}

	void JFMStorageLayer::OnAttach()
	{
		DS_LOG_INFO("JFM storage layer attached");
	}
} // namespace SST::Prototypes::JFM

