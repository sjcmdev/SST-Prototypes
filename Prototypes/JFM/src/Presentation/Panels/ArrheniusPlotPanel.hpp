#pragma once

#include "Analysis/JFMArrheniusAnalysis.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace SST::Prototypes::JFM
{
	class ArrheniusPlotPanel final : public IPanel
	{
	public:
		explicit ArrheniusPlotPanel(Ref<JFMWorkspaceStore> store);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

		struct FitLine
		{
			std::string name;
			double slope = 0.0;
			double intercept = 0.0;
			double eaEv = 0.0;
			double egEv = 0.0;
		};

	private:
		struct PlotState
		{
			int lower = 0;
			int upper = 0;
			std::vector<FitLine> fits;
		};

		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForSeries(ProjectHandle project, const JFMSeries &series) const;
		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForGroup(ProjectHandle project, const JFMProject &snapshot, const JFMGroup &group) const;
		void RenderPlot(const std::string &key, const std::string &title, const std::vector<JFMCharacteristicRecord> &records);

		Ref<JFMWorkspaceStore> m_Store;
		int m_SourceMode = 0;
		int m_Quantity = 3;
		int m_FitModel = 1;
		bool m_PreferFit = true;
		std::unordered_map<std::string, PlotState> m_Plots;
	};
} // namespace SST::Prototypes::JFM
