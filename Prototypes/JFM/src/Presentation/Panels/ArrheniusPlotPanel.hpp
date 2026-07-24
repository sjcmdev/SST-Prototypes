#pragma once

#include "Analysis/JFMArrheniusAnalysis.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <array>
#include <cstdint>
#include <string>
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
			int model = 0;
			int lower = 0;
			int upper = 0;
			double slope = 0.0;
			double intercept = 0.0;
			double eaEv = 0.0;
			double egEv = 0.0;
			double j00 = 0.0;
			double i00 = 0.0;
			double tStar = 0.0;
			bool hasEa = false;
			bool hasEg = false;
			bool hasJ00 = false;
			bool hasI00 = false;
			bool hasTStar = false;
		};

	private:
		struct SeriesStyle
		{
			std::array<float, 4> color{0.26f, 0.47f, 0.86f, 1.0f};
			float pointSize = 5.0f;
			float lineWeight = 1.5f;
			int drawMode = 2;
			int marker = 0;
			bool visible = true;
		};

		struct SeriesEntry
		{
			int sourceMode = 0;
			std::string sourceId;
			std::string title;
			int lower = 0;
			int upper = 0;
			SeriesStyle style;
			std::vector<FitLine> fits;
		};

		struct PlotWindow
		{
			std::uint64_t id = 0;
			std::string title;
			int quantity = 3;
			int fitModel = 0;
			bool preferFit = true;
			bool autoRefit = true;
			bool open = true;
			bool needsInitialPlacement = false;
			int initialPlacement = 0;
			int activeSeries = 0;
			std::vector<SeriesEntry> series;
		};

		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForSeries(ProjectHandle project, const JFMSeries &series) const;
		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForGroup(ProjectHandle projectHandle, const JFMProject &snapshot, const JFMGroup &group) const;
		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForEntry(ProjectHandle projectHandle, const JFMProject &snapshot, const SeriesEntry &entry) const;
		[[nodiscard]] std::vector<JFMArrheniusPoint> PointsForEntry(ProjectHandle projectHandle, const JFMProject &snapshot, const SeriesEntry &entry, JFMArrheniusQuantity quantity, bool preferFit) const;
		void RenderSelector(ProjectHandle projectHandle, const JFMProject &snapshot);
		void RenderPlotWindow(ProjectHandle projectHandle, const JFMProject &snapshot, PlotWindow &window);
		void AddSelectedSource(ProjectHandle projectHandle, const JFMProject &snapshot, PlotWindow *target);
		void AddAllPlots(ProjectHandle projectHandle, const JFMProject &snapshot);

		Ref<JFMWorkspaceStore> m_Store;
		int m_SourceMode = 0;
		int m_SelectedSeries = 0;
		int m_SelectedGroup = 0;
		int m_Quantity = 3;
		int m_FitModel = 0;
		bool m_PreferFit = true;
		std::uint64_t m_NextPlotId = 1;
		std::uint64_t m_ActivePlotId = 0;
		std::vector<PlotWindow> m_PlotWindows;
	};
} // namespace SST::Prototypes::JFM
