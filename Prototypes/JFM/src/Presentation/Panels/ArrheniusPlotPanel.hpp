#pragma once
#include "Presentation/Panels/HostedWindows.hpp"
#include <sst/ui/Plot/RangeSelector.hpp>
#include "Presentation/JFMPlotDocument.hpp"

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
			std::uint64_t plotKey = 0;
			std::string name;
			std::string modelName;
			int model = 0;
			int lower = 0;
			int upper = 0;
			double slope = 0.0;
			double intercept = 0.0;
			double eaEv = 0.0;
			double egEv = 0.0;
			double j00 = 0.0;
			double i00 = 0.0;
			double e00 = 0.0;
			double eStar = 0.0;
			double tStar = 0.0;
			double slopeStd = 0.0;
			double interceptStd = 0.0;
			double eaStd = 0.0;
			double egStd = 0.0;
			double j00Std = 0.0;
			double i00Std = 0.0;
			double e00Std = 0.0;
			double eStarStd = 0.0;
			double tStarStd = 0.0;
			std::vector<double> fitX;
			std::vector<double> fitY;
			std::array<float, 4> color{0.90f, 0.42f, 0.18f, 1.0f};
			float lineWeight = 2.5f;
			bool visible = true;
			bool hasEa = false;
			bool hasEg = false;
			bool hasJ00 = false;
			bool hasI00 = false;
			bool hasE00 = false;
			bool hasEStar = false;
			bool hasTStar = false;
			bool hasSlopeStd = false;
			bool hasInterceptStd = false;
			bool hasEaStd = false;
			bool hasEgStd = false;
			bool hasJ00Std = false;
			bool hasI00Std = false;
			bool hasE00Std = false;
			bool hasEStarStd = false;
			bool hasTStarStd = false;
		};

	private:
        HostedWindows m_Windows;
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
			SeriesStyle tunedStyle{{1.0f, 0.52f, 0.10f, 1.0f}, 5.0f, 1.5f, 2, 0, false};
			std::vector<FitLine> fits;
			sst::ui::plot::RangeSelectorState rangeInteraction;
		};

		struct PlotWindow
		{
			Ref<JFMPlotDocument> document = CreateRef<JFMPlotDocument>();
			std::uint64_t id = 0;
			std::string title;
			int quantity = 3;
			int fitModel = 0;
			bool preferFit = true;
			bool autoRefit = true;
			bool open = true;
			bool needsInitialPlacement = false;
			int initialPlacement = 0;
			bool needsDock = false;
			int dockSlot = -1;
			int activeSeries = 0;
			bool fitColumnsNextFrame = false;
			std::vector<SeriesEntry> series;
		};

		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForSeries(ProjectHandle project, const JFMSeries &series) const;
		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForGroup(ProjectHandle projectHandle, const JFMProject &snapshot, const JFMGroup &group) const;
		[[nodiscard]] std::vector<JFMCharacteristicRecord> RecordsForEntry(ProjectHandle projectHandle, const JFMProject &snapshot, const SeriesEntry &entry) const;
		[[nodiscard]] std::vector<JFMArrheniusPoint> PointsForEntry(ProjectHandle projectHandle, const JFMProject &snapshot, const SeriesEntry &entry, JFMArrheniusQuantity quantity, bool preferFit, bool useTuned) const;
		void RenderSelector(ProjectHandle projectHandle, const JFMProject &snapshot);
		void RenderDockspaceHost();
		void RenderDockspace();
		void RenderPlotWindow(ProjectHandle projectHandle, const JFMProject &snapshot, PlotWindow &window);
		void AddSelectedSource(ProjectHandle projectHandle, const JFMProject &snapshot, PlotWindow *target);
		void AddAllPlots(ProjectHandle projectHandle, const JFMProject &snapshot);
		[[nodiscard]] JFMArrheniusLayoutDocument CaptureLayoutDocument() const;
		void RestoreLayoutDocument(const JFMArrheniusLayoutDocument &layout);
		void PersistLayout(ProjectHandle projectHandle) const;

		Ref<JFMWorkspaceStore> m_Store;
		int m_SourceMode = 0;
		int m_SelectedSeries = 0;
		int m_SelectedGroup = 0;
		int m_Quantity = 3;
		int m_FitModel = 0;
		bool m_PreferFit = true;
		bool m_UseTuned = false;
		std::uint64_t m_NextPlotId = 1;
		std::uint64_t m_ActivePlotId = 0;
		unsigned int m_DockspaceId = 0;
		std::array<unsigned int, 6> m_DockIds{};
		bool m_RebuildDockLayout = true;
        bool m_DockHostOpen = true;
		ProjectHandle m_LayoutProject = InvalidProjectHandle;
		std::vector<PlotWindow> m_PlotWindows;
	};
} // namespace SST::Prototypes::JFM
