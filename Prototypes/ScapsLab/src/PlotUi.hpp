#pragma once
#include "PlotSettings.hpp"
#include <implot.h>
namespace SST::ScapsLab {
bool CaptureDockLayout(PlotSettings &settings,bool shuttingDown=false);
bool ApplyDockLayout(const PlotSettings &settings);
bool BeginSavedPlot(PlotSession &session,const char *title,ImVec2 size);
void SetupSavedPlot(PlotSession &session,const char *title,bool compactLegend=false);
bool EndSavedPlot(PlotSession &session,const char *title);
void FitSavedAxis(PlotSession &session,const char *title,ImAxis axis,double lo,double hi);
void BatchColourScale(const char *label,double lo,double hi);
}
