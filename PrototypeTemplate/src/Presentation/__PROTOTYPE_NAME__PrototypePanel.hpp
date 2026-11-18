#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Domain/PrototypeBackend.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "IO/PrototypeWorkflow.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	class __PROTOTYPE_NAME__PrototypePanel final : public IPanel
	{
	public:
		explicit __PROTOTYPE_NAME__PrototypePanel(
			PrototypeWorkflow *store,
			std::string title = "__PROTOTYPE_NAME__ Prototype",
			bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		PrototypeWorkflow *m_Store;
	};
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

