#pragma once

#include <memory>

#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST::Prototypes::JFM
{
	class JFMIOLayer final : public Layer
	{
	public:
		explicit JFMIOLayer(Ref<PrototypeResultStore> store);

		void OnAttach() override;

	private:
		Ref<PrototypeResultStore> m_Store;
	};
} // namespace SST::Prototypes::JFM

