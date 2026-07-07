#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Domain/PrototypeBackend.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST::Prototypes::JFM
{
	class JFMPrototypePanel final : public IPanel
	{
	public:
		explicit JFMPrototypePanel(
			Ref<PrototypeResultStore> store,
			std::string title = "JFM Prototype",
			bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		Ref<PrototypeResultStore> m_Store;
		PrototypeInput m_Input;
	};
} // namespace SST::Prototypes::JFM

