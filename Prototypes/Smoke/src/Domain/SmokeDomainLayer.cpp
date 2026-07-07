#include "Core/dspch.hpp"

#include "Domain/SmokeDomainLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::Smoke
{
	SmokeDomainLayer::SmokeDomainLayer(Ref<PrototypeResultStore> store)
		: Layer("SmokeDomain"), m_Store(std::move(store))
	{
	}

	void SmokeDomainLayer::OnAttach()
	{
		PrototypeResult result = PrototypeBackend::Evaluate({});
		m_Store->Save(result);
		DS_LOG_INFO("Smoke backend smoke result: conductivity={}", result.conductivity);
	}
} // namespace SST::Prototypes::Smoke

