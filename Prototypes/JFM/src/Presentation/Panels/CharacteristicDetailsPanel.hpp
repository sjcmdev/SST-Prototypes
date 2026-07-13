#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class CharacteristicDetailsPanel final : public IPanel
	{
	public:
		explicit CharacteristicDetailsPanel(Ref<JFMWorkspaceStore> store);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
	};
} // namespace SST::Prototypes::JFM
