#include "Core/dspch.hpp"

#include "Domain/JFMDomainLayer.hpp"

#include "Core/Logging/Logger.hpp"

namespace SST::Prototypes::JFM
{
	JFMDomainLayer::JFMDomainLayer(Ref<JFMWorkspaceStore> store)
		: Layer("JFMDomain"), m_Store(std::move(store))
	{
	}

	void JFMDomainLayer::OnAttach()
	{
		const JFMProject snapshot = m_Store != nullptr ? m_Store->GetProjectSnapshot() : JFMProject{};
		DS_LOG_INFO("JFM domain layer attached: project={}", snapshot.name);
	}
} // namespace SST::Prototypes::JFM
