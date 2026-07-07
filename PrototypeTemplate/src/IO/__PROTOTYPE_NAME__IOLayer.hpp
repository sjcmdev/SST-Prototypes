#pragma once

#include <memory>

#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	class __PROTOTYPE_NAME__IOLayer final : public Layer
	{
	public:
		explicit __PROTOTYPE_NAME__IOLayer(Ref<PrototypeResultStore> store);

		void OnAttach() override;

	private:
		Ref<PrototypeResultStore> m_Store;
	};
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

