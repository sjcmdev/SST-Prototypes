#include "Core/dspch.hpp"

#include "IO/__PROTOTYPE_NAME__IOLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	__PROTOTYPE_NAME__IOLayer::__PROTOTYPE_NAME__IOLayer(Ref<PrototypeResultStore> store)
		: Layer("__PROTOTYPE_NAME__IO"), m_Store(std::move(store))
	{
	}

	void __PROTOTYPE_NAME__IOLayer::OnAttach()
	{
		DS_LOG_INFO("__PROTOTYPE_NAME__ IO layer attached");
	}
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

