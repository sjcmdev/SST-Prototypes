#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/EditorWindow.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class JFMOverviewPanel final : public EditorWindow
	{
	public:
		explicit JFMOverviewPanel(Ref<JFMWorkspaceStore> store, std::string title = "JFM Overview", bool visibleByDefault = true);

		[[nodiscard]] WindowDuplicate Duplicate() const override;
		void RenderContent() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
	};
} // namespace SST::Prototypes::JFM
