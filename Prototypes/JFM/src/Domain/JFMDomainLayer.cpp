#include "Core/dspch.hpp"

#include "Domain/JFMDomainLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::JFM
{
	JFMDomainLayer::JFMDomainLayer(Ref<PrototypeResultStore> store)
		: Layer("JFMDomain"), m_Store(std::move(store))
	{
	}

	void JFMDomainLayer::OnAttach()
	{
		PrototypeResult result = PrototypeBackend::Evaluate({});
		m_Store->Save(result);
		DS_LOG_INFO("JFM backend smoke result: conductivity={}", result.conductivity);
	}
} // namespace SST::Prototypes::JFM

