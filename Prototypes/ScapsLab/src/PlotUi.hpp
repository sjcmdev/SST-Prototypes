#pragma once
#include "PlotSettings.hpp"
#include <implot.h>
namespace SST::ScapsLab {
inline const ImVec4 SingleShotColour{.25f,.65f,.95f,1.f};
inline const ImVec4 LiveCurveColour{.90f,.42f,.07f,1.f};
inline ImVec4 XColour(const std::string &key,int shade=0) {
    static const std::pair<const char*,ImVec4> table[]={
        {"Ec_eV",{.35f,.62f,1.f,1.f}},{"EFn_eV",{.35f,.62f,1.f,1.f}},{"n_m3",{.35f,.62f,1.f,1.f}},
        {"Ev_eV",{1.f,.38f,.38f,1.f}},{"EFp_eV",{1.f,.38f,.38f,1.f}},{"p_m3",{1.f,.38f,.38f,1.f}},
        {"generation_m3_s",{.30f,.80f,.35f,1.f}},{"G",{.30f,.80f,.35f,1.f}},
        {"srh_bulk",{.75f,.50f,.95f,1.f}},{"interface",{1.f,.65f,.15f,1.f}},{"radiative",{.20f,.80f,.80f,1.f}},{"auger",{.95f,.45f,.75f,1.f}}};
    ImVec4 c{.8f,.8f,.8f,1.f};
    for(const auto &[k,v]:table) if(key==k) c=v;
    const float t=std::min(.6f,.25f*shade);  // lighter tint for extra channels of the same mechanism
    return {c.x+(1-c.x)*t,c.y+(1-c.y)*t,c.z+(1-c.z)*t,1.f};
}
inline bool DashedQuantity(std::string_view key) {return key=="EFn_eV" || key=="EFp_eV";}
bool CaptureDockLayout(PlotSettings &settings,bool shuttingDown=false);
bool ApplyDockLayout(const PlotSettings &settings);
bool BeginSavedPlot(PlotSession &session,const char *title,ImVec2 size);
void SetupSavedPlot(PlotSession &session,const char *title,bool compactLegend=false);
bool EndSavedPlot(PlotSession &session,const char *title);
void FitSavedAxis(PlotSession &session,const char *title,ImAxis axis,double lo,double hi);
void BatchColourScale(const char *label,double lo,double hi);
}
