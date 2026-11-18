#pragma once

#include <string>

#include "Core/Utils/Memory.hpp"
#include "Domain/PrototypeBackend.hpp"
#include "Presentation/Panels/EditorWindow.hpp"
#include "IO/PrototypeWorkflow.hpp"

namespace SST::Prototypes::WindowChromeLab
{
	class WindowChromeLabPrototypePanel final : public EditorWindow
	{
	public:
		explicit WindowChromeLabPrototypePanel(
			PrototypeWorkflow *store,
			std::string title = "WindowChromeLab Prototype",
			bool visibleByDefault = true);

		[[nodiscard]] WindowDuplicate Duplicate() const override;
		bool IsDirty() const override { return m_Store->document.dirty; }
		void RenderContent() override;

	private:
		PrototypeWorkflow *m_Store;
	};
} // namespace SST::Prototypes::WindowChromeLab

