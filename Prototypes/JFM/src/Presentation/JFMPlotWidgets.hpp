#pragma once
#include "Presentation/JFMPlotDocument.hpp"
namespace SST::Prototypes::JFM
{
    bool BeginJFMPlotFrame(JFMPlotDocument &, ImVec2 available = {});
    void SetupJFMPlotAxes(JFMPlotDocument &, bool seriesReady = false);
    void AddJFMPlotSeries(JFMPlotDocument &, const sst::ui::plot::SeriesEntryView &, bool positiveCurrentOnly = false);
    void EndJFMPlot(JFMPlotDocument &, sst::ui::plot::PlotSpec, const char* plotId);
    void JFMPlotExportControls(JFMPlotDocument &);
}
