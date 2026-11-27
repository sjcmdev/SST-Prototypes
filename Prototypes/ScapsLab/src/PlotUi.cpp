#include "PlotUi.hpp"
#include <implot_internal.h>
#include "Core/Presentation/Panels/WindowWorkspace.hpp"
namespace SST::ScapsLab {
std::optional<PlotBounds> FitPlotBounds(std::span<const double> x,std::span<const double> y,bool logX,bool logY) {
    if(x.size()!=y.size())return {};
    std::optional<PlotBounds> bounds;
    for(std::size_t i=0;i<x.size();++i) {
        if(!std::isfinite(x[i]) || !std::isfinite(y[i]) || (logX && x[i]<=0) || (logY && y[i]<=0))continue;
        if(!bounds)bounds=PlotBounds{{x[i],x[i]},{y[i],y[i]}};
        else {bounds->x={std::min(bounds->x.min,x[i]),std::max(bounds->x.max,x[i])};bounds->y={std::min(bounds->y.min,y[i]),std::max(bounds->y.max,y[i])};}
    }
    return bounds;
}
void FitDrawnSeries(const char *label,std::span<const double> x,std::span<const double> y) {
    const auto *item=ImPlot::GetItem(label);if(!item || !item->Show || !ImPlot::FitThisFrame())return;
    auto *plot=ImPlot::GetCurrentPlot();auto &xx=plot->Axes[plot->CurrentX],&yy=plot->Axes[plot->CurrentY];
    if(const auto bounds=FitPlotBounds(x,y,xx.Scale==ImPlotScale_Log10,yy.Scale==ImPlotScale_Log10)) {
        xx.ExtendFit(bounds->x.min);xx.ExtendFit(bounds->x.max);yy.ExtendFit(bounds->y.min);yy.ExtendFit(bounds->y.max);
    }
}
namespace {
struct DrawPoints {
    const double *x,*y;
    double floor;
    explicit DrawPoints(const double* x,const double* y):x(x),y(y) {
        auto* plot=ImPlot::GetCurrentPlot();auto& axis=plot->Axes[plot->CurrentY];
        floor=axis.Scale==ImPlotScale_Log10?axis.Range.Min:0.;
    }
    static ImPlotPoint At(int i,void* data) {
        const auto& p=*static_cast<const DrawPoints*>(data);
        return {p.x[i],p.y[i]==0?p.floor:p.y[i]};
    }
};
}
void DrawPlotLine(const char *label,const double *x,const double *y,int count,ImPlotSpec spec) {
    const bool fit=!(spec.Flags&ImPlotItemFlags_NoFit);spec.Flags|=ImPlotItemFlags_NoFit;
    ImPlot::SetupLock();DrawPoints points(x,y);
    ImPlot::PlotLineG(label,DrawPoints::At,&points,count,spec);if(fit && count>0)FitDrawnSeries(label,{x,std::size_t(count)},{y,std::size_t(count)});
}
void DrawPlotScatter(const char *label,const double *x,const double *y,int count,ImPlotSpec spec) {
    const bool fit=!(spec.Flags&ImPlotItemFlags_NoFit);spec.Flags|=ImPlotItemFlags_NoFit;
    ImPlot::SetupLock();DrawPoints points(x,y);
    ImPlot::PlotScatterG(label,DrawPoints::At,&points,count,spec);if(fit && count>0)FitDrawnSeries(label,{x,std::size_t(count)},{y,std::size_t(count)});
}
void DrawPlotBars(const char *label,const double *x,const double *y,int count,double width,ImPlotSpec spec) {
    const bool fit=!(spec.Flags&ImPlotItemFlags_NoFit);spec.Flags|=ImPlotItemFlags_NoFit;
    ImPlot::PlotBars(label,x,y,count,width,spec);
    if(fit)for(int i=0;i<count;++i){const double xx[]={x[i]-width/2,x[i]+width/2},yy[]={y[i],y[i]};FitDrawnSeries(label,xx,yy);if(std::isfinite(y[i])){const double zero=0;FitDrawnSeries(label,{x+i,1},{&zero,1});}}
}
bool CaptureDockLayout(PlotSettings &settings,bool shuttingDown) {
    const auto *context=ImGui::GetCurrentContext();
    if(!context || shuttingDown || !context->WithinFrameScope || ImGui::GetTopMostPopupModal())return false;
    const auto *root=ImGui::DockBuilderGetNode(ImGui::GetMainViewport()->ID);
    if(!root || !root->IsDockSpace() || root->LastFrameAlive!=ImGui::GetFrameCount())return false;
    std::size_t size=0;const auto *ini=ImGui::SaveIniSettingsToMemory(&size);
    if(!ValidDockLayout(std::string_view(ini,size)))return false;
    settings.layout.assign(ini,size);return true;
}
bool ApplyDockLayout(const PlotSettings &settings,WindowWorkspace &workspace) {
    const auto *context=ImGui::GetCurrentContext();
    if(!context || context->WithinFrameScope)return false;
    const bool valid=!settings.layout.empty() && ValidDockLayout(settings.layout);
    // Reloading after DockSpace() replaces the live root and orphans its children.
    if(valid)ImGui::LoadIniSettingsFromMemory(settings.layout.data(),settings.layout.size());
    // A saved global lock must not silently disable every panel in a reopened project.
    if(workspace.IsLocked())(void)workspace.Apply({WindowAction::LockLayout});
    return valid;
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
        auto &smooth=r.axes[i];const bool log=a.Scale==ImPlotScale_Log10;
        if(v.manual && v.ranges[i]){if(r.hasData)a.SetRange(v.ranges[i]->min,v.ranges[i]->max);smooth.Set(*v.ranges[i],log);}
        else if(s.smoothFit && smooth.initialized && smooth.log==log){const auto range=smooth.Step(ImGui::GetIO().DeltaTime,s.holdFit);a.SetRange(range.min,range.max);}
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
    const auto finiteExtent=[](const ImPlotAxis &a){return std::isfinite(a.FitExtents.Min) && std::isfinite(a.FitExtents.Max) && a.FitExtents.Min<=a.FitExtents.Max;};
    bool xData=false,yData=false;for(const auto &a:plot->Axes)if(a.Enabled && finiteExtent(a)){if(a.Vertical)yData=true;else xData=true;}
    const bool data=xData && yData;
    for(int i=0;i<ImAxis_COUNT;++i){auto &a=plot->Axes[i];if(!a.Enabled)continue;
        const bool log=a.Scale==ImPlotScale_Log10;
        const bool manualAxis=v.manual && v.ranges[i].has_value();
        if(manualAxis){a.FitThisFrame=false;if(data)a.SetRange(v.ranges[i]->min,v.ranges[i]->max);}
        if(!manualAxis && a.FitThisFrame) {
            PlotRange raw{a.FitExtents.Min,a.FitExtents.Max};
            if(r.explicitFit[i]){const auto extra=*r.explicitFit[i];raw=finiteExtent(a)?PlotRange{std::min(raw.min,extra.min),std::max(raw.max,extra.max)}:extra;}
            if(log && a.Vertical) {
                if(!std::isnormal(raw.max) || raw.max<=0)raw={1e-12,1.};
                else {
                    if(raw.min==raw.max)raw={raw.min/std::sqrt(10.),raw.max*std::sqrt(10.)};
                    raw.min=std::max(raw.min,1e-200);
                }
            }
            else if(log && raw.min>0 && raw.min==raw.max)raw={raw.min/std::sqrt(10.),raw.max*std::sqrt(10.)};
            else if(!log && std::isfinite(raw.min) && raw.min==raw.max){const double pad=std::max(.5,std::abs(raw.min)*.05);raw={raw.min-pad,raw.max+pad};}
            if(raw.Valid(log)){
                const auto padded=PadPlotRange(raw,s.settings.margin,log);auto &smooth=r.axes[i];const bool ready=smooth.initialized && smooth.log==log;
                if(s.smoothFit)smooth.Fit(padded,log,s.holdFit);else smooth.Set(padded,log);
                if(s.smoothFit && smooth.current!=smooth.target)s.morphFrame=ImGui::GetFrameCount();
                if(!s.smoothFit || !ready)a.SetRange(smooth.current.min,smooth.current.max);
            }
            // Fit extents are collected every frame; applying them here would snap over the eased range.
            a.FitThisFrame=false;
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
