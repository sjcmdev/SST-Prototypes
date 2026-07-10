#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class JFMOverviewPanel final : public IPanel
	{
	public:
		explicit JFMOverviewPanel(Ref<JFMWorkspaceStore> store, std::string title = "JFM Overview", bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
	};
} // namespace SST::Prototypes::JFM
