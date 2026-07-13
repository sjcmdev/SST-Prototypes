#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <vector>

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
		struct PlotArea
		{
			std::vector<CharacteristicId> characteristicIds;
			PlotScale scale = PlotScale::Linear;
		};

		Ref<JFMWorkspaceStore> m_Store;
		std::vector<PlotArea> m_PlotAreas;
	};
} // namespace SST::Prototypes::JFM
