#pragma once

#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>
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
			std::uint64_t id = 0;
			std::vector<CharacteristicId> characteristicIds;
			PlotScale scale = PlotScale::Linear;
		};

		[[nodiscard]] PlotArea CreatePlotArea();
		void PlotIds(std::vector<CharacteristicId> ids);
		void PlotSeries(const std::string &seriesId);
		void PlotSelectedGroup();

		Ref<JFMWorkspaceStore> m_Store;
		std::vector<PlotArea> m_PlotAreas;
		std::uint64_t m_NextPlotAreaId = 1;
		std::unordered_map<CharacteristicId, std::array<float, 4>> m_ColorOverrides;
	};
} // namespace SST::Prototypes::JFM
