#pragma once

#include <memory>

#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST::Prototypes::Smoke
{
	class SmokeIOLayer final : public Layer
	{
	public:
		explicit SmokeIOLayer(Ref<PrototypeResultStore> store);

		void OnAttach() override;

	private:
		Ref<PrototypeResultStore> m_Store;
	};
} // namespace SST::Prototypes::Smoke

