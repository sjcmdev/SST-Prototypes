#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	enum class PlotScale
	{
		Linear,
		SemiLog,
		SignedLog
	};
	class RawPlotPanel final : public IPanel
	{
	public:
		explicit RawPlotPanel(Ref<JFMWorkspaceStore> store, std::string title = "Raw Plot", bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:

		Ref<JFMWorkspaceStore> m_Store;
		PlotScale m_Scale = PlotScale::Linear;
	};
} // namespace SST::Prototypes::JFM
