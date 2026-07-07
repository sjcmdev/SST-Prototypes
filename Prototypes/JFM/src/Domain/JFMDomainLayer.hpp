#pragma once

#include <memory>

#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST::Prototypes::JFM
{
	class JFMDomainLayer final : public Layer
	{
	public:
		explicit JFMDomainLayer(Ref<PrototypeResultStore> store);

		void OnAttach() override;

	private:
		Ref<PrototypeResultStore> m_Store;
	};
} // namespace SST::Prototypes::JFM

