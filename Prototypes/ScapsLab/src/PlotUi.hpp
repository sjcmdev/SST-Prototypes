#pragma once
#include "PlotSettings.hpp"
#include <implot.h>
namespace SST::ScapsLab {
inline const ImVec4 SingleShotColour{.25f,.65f,.95f,1.f};
inline const ImVec4 LiveCurveColour{.90f,.42f,.07f,1.f};
bool CaptureDockLayout(PlotSettings &settings,bool shuttingDown=false);
bool ApplyDockLayout(const PlotSettings &settings);
bool BeginSavedPlot(PlotSession &session,const char *title,ImVec2 size);
void SetupSavedPlot(PlotSession &session,const char *title,bool compactLegend=false);
bool EndSavedPlot(PlotSession &session,const char *title);
void FitSavedAxis(PlotSession &session,const char *title,ImAxis axis,double lo,double hi);
void BatchColourScale(const char *label,double lo,double hi);
}
