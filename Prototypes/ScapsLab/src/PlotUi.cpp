#include "PlotUi.hpp"
#include <implot_internal.h>
namespace SST::ScapsLab {
bool CaptureDockLayout(PlotSettings &settings,bool shuttingDown) {
    const auto *context=ImGui::GetCurrentContext();
    if(!context || shuttingDown || !context->WithinFrameScope || ImGui::GetTopMostPopupModal())return false;
    const auto *root=ImGui::DockBuilderGetNode(ImGui::GetMainViewport()->ID);
    if(!root || !root->IsDockSpace() || root->LastFrameAlive!=ImGui::GetFrameCount())return false;
    std::size_t size=0;const auto *ini=ImGui::SaveIniSettingsToMemory(&size);
    if(!ValidDockLayout(std::string_view(ini,size)))return false;
    settings.layout.assign(ini,size);return true;
}
bool ApplyDockLayout(const PlotSettings &settings) {
    const auto *context=ImGui::GetCurrentContext();
    if(!context || context->WithinFrameScope || settings.layout.empty() || !ValidDockLayout(settings.layout))return false;
    // Reloading after DockSpace() replaces the live root and orphans its children.
    ImGui::LoadIniSettingsFromMemory(settings.layout.data(),settings.layout.size());return true;
}
bool BeginSavedPlot(PlotSession &s,const char *title,ImVec2 size) {
    auto &r=s.runtime[title];r.explicitFit={};
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding,ImVec2(s.settings.margin*.02f,s.settings.margin*.02f)); // ImPlot uses half the span
    ImPlot::SetNextAxesToFit(); // collect extents even in manual mode; only auto mode applies them
    if(ImPlot::BeginPlot(title,size))return true;
    ImPlot::PopStyleVar();return false;
}
void SetupSavedPlot(PlotSession &s,const char *title,bool compact) {
    auto *plot=ImPlot::GetCurrentPlot();auto &v=s.settings.plots[title];auto &r=s.runtime[title];
    for(int i=0;i<ImAxis_COUNT;++i)r.enabled[i]=plot->Axes[i].Enabled;
    for(int i=0;i<ImAxis_COUNT;++i)if(plot->Axes[i].Enabled){auto &a=plot->Axes[i];
        if(v.log[i])ImPlot::SetupAxisScale(i,*v.log[i]?ImPlotScale_Log10:ImPlotScale_Linear);
        if(a.Scale==ImPlotScale_Log10)ImPlot::SetupAxisLimitsConstraints(i,1e-200,std::numeric_limits<double>::max());
        if(v.manual && v.ranges[i] && !v.ranges[i]->Valid(a.Scale==ImPlotScale_Log10)){v.manual=false;v.ranges={};}
        r.before[i]={a.Range.Min,a.Range.Max};r.logBefore[i]=a.Scale==ImPlotScale_Log10;
    }
    if(v.manual && std::none_of(v.ranges.begin(),v.ranges.end(),[](auto &r){return r.has_value();}))v.manual=false;
    if(v.legend){if(*v.legend)plot->Flags&=~ImPlotFlags_NoLegend;else plot->Flags|=ImPlotFlags_NoLegend;}
    if(v.legendLocation)plot->Items.Legend.Location=*v.legendLocation;
    if(v.legendFlags)plot->Items.Legend.Flags=*v.legendFlags;
    if(compact)plot->Flags|=ImPlotFlags_NoLegend;else if(!v.legend)plot->Flags&=~ImPlotFlags_NoLegend;
    auto &legend=plot->Items.Legend;
    if(legend.Flags&ImPlotLegendFlags_Outside) {
        const auto &style=ImPlot::GetStyle();
        const auto size=ImPlot::CalcLegendSize(plot->Items,style.LegendInnerPadding,style.LegendSpacing,!(legend.Flags&ImPlotLegendFlags_Horizontal));
        if(CompactBatchLegend(plot->Items.GetLegendCount()) || size.y>plot->FrameRect.GetHeight()*.4f || size.x>plot->FrameRect.GetWidth()*.8f)legend.Flags&=~ImPlotLegendFlags_Outside;
    }
    r.legendShown=compact?v.legend.value_or(true):!(plot->Flags&ImPlotFlags_NoLegend);r.legendLocation=plot->Items.Legend.Location;
    if(!r.initialized){for(int i=0;i<plot->Items.GetItemCount();++i)plot->Items.GetItemByIndex(i)->Show=true;for(auto id:v.hidden){auto *item=plot->Items.GetOrAddItem(id);item->ID=id;item->Show=false;}}
    r.initialized=true;
}
void FitSavedAxis(PlotSession &s,const char *title,ImAxis axis,double lo,double hi) {
    s.runtime[title].explicitFit[axis]=PlotRange{lo,hi};
}
bool EndSavedPlot(PlotSession &s,const char *title) {
    auto *plot=ImPlot::GetCurrentPlot();ImPlot::SetupLock();
    auto &v=s.settings.plots[title];auto &r=s.runtime[title];bool changed=false;
    const auto &io=ImGui::GetIO();
    const bool hovered=plot->Hovered || ImPlot::AnyAxesHovered(plot->Axes,ImAxis_COUNT);
    const bool fit=hovered && ImGui::IsMouseDoubleClicked(ImPlot::GetInputMap().Fit);
    if(fit){if(v.manual){v.manual=false;v.ranges={};changed=true;}}
    bool moved=false;for(int i=0;i<ImAxis_COUNT;++i)if(plot->Axes[i].Enabled)moved|=r.before[i]!=PlotRange{plot->Axes[i].Range.Min,plot->Axes[i].Range.Max};
    const bool pan=plot->Held || ImPlot::AnyAxesHeld(plot->Axes,ImAxis_COUNT);
    const bool zoom=hovered && io.MouseWheel!=0;
    const bool selection=r.selecting && ImGui::IsMouseReleased(ImPlot::GetInputMap().Select);
    r.selecting=plot->Selecting;
    if(!fit && r.hasData && moved && (pan || zoom || selection)){
        v.manual=true;for(int i=0;i<ImAxis_COUNT;++i)if(plot->Axes[i].Enabled){PlotRange range{plot->Axes[i].Range.Min,plot->Axes[i].Range.Max};if(range.Valid(plot->Axes[i].Scale==ImPlotScale_Log10))v.ranges[i]=range;}
        changed=true;plot->FitThisFrame=false;for(auto &a:plot->Axes)a.FitThisFrame=false;
    }
    const auto finiteExtent=[](const ImPlotAxis &a){return std::isfinite(a.FitExtents.Min) && std::isfinite(a.FitExtents.Max);};
    bool xData=false,yData=false;for(const auto &a:plot->Axes)if(a.Enabled && finiteExtent(a)){if(a.Vertical)yData=true;else xData=true;}
    const bool data=xData && yData;
    for(int i=0;i<ImAxis_COUNT;++i){auto &a=plot->Axes[i];if(!a.Enabled)continue;
        const bool log=a.Scale==ImPlotScale_Log10;
        const bool manualAxis=v.manual && v.ranges[i].has_value();
        if(manualAxis){a.FitThisFrame=false;if(data)a.SetRange(v.ranges[i]->min,v.ranges[i]->max);}
        if(!manualAxis && a.FitThisFrame) {
            auto raw=r.explicitFit[i].value_or(PlotRange{a.FitExtents.Min,a.FitExtents.Max});
            if(log && a.Vertical) {
                if(!std::isnormal(raw.max) || raw.max<=0)raw={1e-12,1.};
                else {
                    if(raw.min==raw.max)raw={raw.min/std::sqrt(10.),raw.max*std::sqrt(10.)};
                    raw.min=std::max({raw.min,raw.max*1e-12,1e-200});
                }
            }
            else if(log && raw.min>0 && raw.min==raw.max)raw={raw.min/std::sqrt(10.),raw.max*std::sqrt(10.)};
            if(raw.Valid(log)){const auto padded=PadPlotRange(raw,s.settings.margin,log);a.SetRange(padded.min,padded.max);a.FitThisFrame=false;}
        }
        if(log!=r.logBefore[i]){v.log[i]=log;changed=true;}
    }
    r.hasData=data;
    const auto flags=plot->Flags;const auto location=plot->Items.Legend.Location;const auto legendFlags=plot->Items.Legend.Flags;
    ImPlot::EndPlot();ImPlot::PopStyleVar();
    // ImPlot applies legend/context-menu edits in EndPlot. Persist only those edits, never default rendering state.
    if((flags^plot->Flags)&ImPlotFlags_NoLegend){v.legend=!(plot->Flags&ImPlotFlags_NoLegend);changed=true;}
    if(location!=plot->Items.Legend.Location){v.legendLocation=plot->Items.Legend.Location;changed=true;}
    if(legendFlags!=plot->Items.Legend.Flags){v.legendFlags=plot->Items.Legend.Flags;changed=true;}
    for(int i=0;i<ImAxis_COUNT;++i)if(plot->Axes[i].Enabled && (plot->Axes[i].Scale==ImPlotScale_Log10)!=r.logBefore[i]){v.log[i]=plot->Axes[i].Scale==ImPlotScale_Log10;changed=true;}
    for(int i=0;i<plot->Items.GetItemCount();++i){const auto *item=plot->Items.GetItemByIndex(i);const bool hidden=v.hidden.contains(item->ID);if(hidden==item->Show){if(item->Show)v.hidden.erase(item->ID);else v.hidden.insert(item->ID);changed=true;}}
    if(changed)++s.revision;
    return changed;
}
void BatchColourScale(const char *label,double lo,double hi) {
    ImGui::Text("%s: %.3g ... %.3g",label,lo,hi);
    ImGui::Dummy(ImVec2(std::max(1.f,ImGui::GetContentRegionAvail().x),12));
    auto &maps=ImPlot::GetCurrentContext()->ColormapData;
    ImPlot::RenderColorBar(maps.GetKeys(ImPlotColormap_Viridis),maps.GetKeyCount(ImPlotColormap_Viridis),*ImGui::GetWindowDrawList(),ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax()),false,false,true);
}
}
