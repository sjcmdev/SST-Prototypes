#pragma once
#include "Presentation/JFMPlotDocument.hpp"
namespace SST::Prototypes::JFM
{
    bool BeginJFMPlotFrame(JFMPlotDocument &);
    void AddJFMPlotSeries(JFMPlotDocument &, const sst::ui::plot::SeriesEntryView &, bool absoluteCurrent = false);
    void EndJFMPlot(JFMPlotDocument &, sst::ui::plot::PlotSpec);
    void JFMPlotExportControls(JFMPlotDocument &);
}
