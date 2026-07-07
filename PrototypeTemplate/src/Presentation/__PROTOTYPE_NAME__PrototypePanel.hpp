#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Domain/PrototypeBackend.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	class __PROTOTYPE_NAME__PrototypePanel final : public IPanel
	{
	public:
		explicit __PROTOTYPE_NAME__PrototypePanel(
			Ref<PrototypeResultStore> store,
			std::string title = "__PROTOTYPE_NAME__ Prototype",
			bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		Ref<PrototypeResultStore> m_Store;
		PrototypeInput m_Input;
	};
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

