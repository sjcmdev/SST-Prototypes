#pragma once

#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class JFMDomainLayer final : public Layer
	{
	public:
		explicit JFMDomainLayer(Ref<JFMWorkspaceStore> store);

		void OnAttach() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
	};
} // namespace SST::Prototypes::JFM
