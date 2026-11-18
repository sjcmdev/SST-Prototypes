#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Domain/PrototypeBackend.hpp"
#include "Presentation/Panels/EditorWindow.hpp"
#include "IO/PrototypeWorkflow.hpp"

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
	class __PROTOTYPE_NAME__PrototypePanel final : public EditorWindow
	{
	public:
		explicit __PROTOTYPE_NAME__PrototypePanel(
			PrototypeWorkflow *store,
			std::string title = "__PROTOTYPE_NAME__ Prototype",
			bool visibleByDefault = true);

		[[nodiscard]] WindowDuplicate Duplicate() const override;
		bool IsDirty() const override { return m_Store->document.dirty; }
		void RenderContent() override;

	private:
		PrototypeWorkflow *m_Store;
	};
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

