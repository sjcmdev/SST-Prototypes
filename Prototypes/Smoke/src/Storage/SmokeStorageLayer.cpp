#include "Core/dspch.hpp"

#include "Storage/SmokeStorageLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::Smoke
{
	SmokeStorageLayer::SmokeStorageLayer(Ref<PrototypeResultStore> store)
		: Layer("SmokeStorage"), m_Store(std::move(store))
	{
	}

	void SmokeStorageLayer::OnAttach()
	{
		DS_LOG_INFO("Smoke storage layer attached");
	}
} // namespace SST::Prototypes::Smoke

