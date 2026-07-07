#include "Core/dspch.hpp"

#include "IO/JFMIOLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::JFM
{
	JFMIOLayer::JFMIOLayer(Ref<PrototypeResultStore> store)
		: Layer("JFMIO"), m_Store(std::move(store))
	{
	}

	void JFMIOLayer::OnAttach()
	{
		DS_LOG_INFO("JFM IO layer attached");
	}
} // namespace SST::Prototypes::JFM

