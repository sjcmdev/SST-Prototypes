#include "Core/dspch.hpp"

#include "Domain/__PROTOTYPE_NAME__DomainLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	__PROTOTYPE_NAME__DomainLayer::__PROTOTYPE_NAME__DomainLayer(Ref<PrototypeResultStore> store)
		: Layer("__PROTOTYPE_NAME__Domain"), m_Store(std::move(store))
	{
	}

	void __PROTOTYPE_NAME__DomainLayer::OnAttach()
	{
		PrototypeResult result = PrototypeBackend::Evaluate({});
		m_Store->Save(result);
		DS_LOG_INFO("__PROTOTYPE_NAME__ backend smoke result: conductivity={}", result.conductivity);
	}
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

