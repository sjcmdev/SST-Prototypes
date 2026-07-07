#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Domain/PrototypeBackend.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Storage/PrototypeResultStore.hpp"

namespace SST::Prototypes::Smoke
{
	class SmokePrototypePanel final : public IPanel
	{
	public:
		explicit SmokePrototypePanel(
			Ref<PrototypeResultStore> store,
			std::string title = "Smoke Prototype",
			bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		Ref<PrototypeResultStore> m_Store;
		PrototypeInput m_Input;
	};
} // namespace SST::Prototypes::Smoke

