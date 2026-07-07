#include "Core/dspch.hpp"

#include "IO/SmokeIOLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::Smoke
{
	SmokeIOLayer::SmokeIOLayer(Ref<PrototypeResultStore> store)
		: Layer("SmokeIO"), m_Store(std::move(store))
	{
	}

	void SmokeIOLayer::OnAttach()
	{
		DS_LOG_INFO("Smoke IO layer attached");
	}
} // namespace SST::Prototypes::Smoke

