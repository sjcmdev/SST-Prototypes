#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/EditorWindow.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class CharacteristicDetailsPanel final : public EditorWindow
	{
	public:
		explicit CharacteristicDetailsPanel(Ref<JFMWorkspaceStore> store);

		[[nodiscard]] WindowDuplicate Duplicate() const override;
		void RenderContent() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
	};
} // namespace SST::Prototypes::JFM
