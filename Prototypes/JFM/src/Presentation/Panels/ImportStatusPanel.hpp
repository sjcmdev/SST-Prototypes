#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Storage/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class ImportStatusPanel final : public IPanel
	{
	public:
		explicit ImportStatusPanel(Ref<JFMWorkspaceStore> store, std::string title = "Import Status", bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
	};
} // namespace SST::Prototypes::JFM
