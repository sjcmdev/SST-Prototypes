#pragma once
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <implot_internal.h>
#include "Presentation/CircuitDisplay.hpp"
#include <cstdio>

namespace SST::Prototypes::CapLab {
struct NyquistScale {
    double xPixelsPerOhm=0, yPixelsPerOhm=0;
};
// Read the rendered ranges after ImPlot has constrained resize and wheel zoom.
inline NyquistScale RenderISPlot(const char* id, const sst::ui::plot::PlotSpec& spec,
    const sst::ui::plot::SeriesEntryView* series, std::size_t count,
    sst::ui::plot::PlotViewState& state, ImVec2 size) {
    state.picked.reset();
    // Native fitting expands both equal-scale axes without cropping the data.
    if (spec.equalAspect && !state.fitted) ImPlot::SetNextAxesToFit();
    if (spec.equalAspect) ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding,ImVec2(.05f,.05f));
    using namespace sst::ui::plot;
    const auto format=[](double value, char* buffer, int size, void*) {
        const auto text=Engineering(value,"",4);
        return std::snprintf(buffer,size,"%s",text.c_str());
    };
    if (ImPlot::BeginPlot(id,size,(spec.equalAspect ? ImPlotFlags_Equal : 0) | (spec.legend.visible ? 0 : ImPlotFlags_NoLegend))) {
        auto xLabel=spec.x.label, yLabel=spec.y.label;
        for (auto* label : {&xLabel,&yLabel}) if (const auto pos=label->find("Ohm"); pos!=std::string::npos) label->replace(pos,3,"\u03a9");
        ImPlot::SetupAxes(xLabel.c_str(),yLabel.c_str(),
            spec.x.grid ? 0 : ImPlotAxisFlags_NoGridLines,spec.y.grid ? 0 : ImPlotAxisFlags_NoGridLines);
        if (spec.x.scale==AxisScale::Log10) ImPlot::SetupAxisScale(ImAxis_X1,ImPlotScale_Log10);
        if (spec.y.scale==AxisScale::Log10) ImPlot::SetupAxisScale(ImAxis_Y1,ImPlotScale_Log10);
        ImPlot::SetupAxisFormat(ImAxis_X1,format);
        ImPlot::SetupAxisFormat(ImAxis_Y1,format);
        ImPlot::SetupLegend(ImPlotLocation_NorthEast,(spec.legend.outside ? ImPlotLegendFlags_Outside : 0) | (spec.legend.interactive ? 0 : ImPlotLegendFlags_NoButtons));
        if (const auto bounds=ResolveBounds(spec,series,count,state))
            ImPlot::SetupAxesLimits(bounds->minX,bounds->maxX,bounds->minY,bounds->maxY,
                spec.autoFit==AutoFit::Always ? ImPlotCond_Always : ImPlotCond_Once);
        for (std::size_t i=0;i<count;++i) {
            const auto visibility=state.visibility.find(series[i].id);
            if (visibility!=state.visibility.end()) ImPlot::HideNextItem(!visibility->second,ImPlotCond_Always);
            RenderSeries(series[i]);
        }
        const auto limits=ImPlot::GetPlotLimits();
        state.viewport=Bounds{limits.X.Min,limits.X.Max,limits.Y.Min,limits.Y.Max}; state.fitted=true;
        auto* plot=ImPlot::GetCurrentPlot();
        for (std::size_t i=0;i<count;++i) if (const auto* item=plot->Items.GetItem(ImGui::GetID(series[i].id.c_str())))
            state.visibility[series[i].id]=item->Show;
        ImPlot::EndPlot();
    }
    if (spec.equalAspect) ImPlot::PopStyleVar();
    const auto* plot=ImPlot::GetPlot(id);
    if (!plot || !spec.equalAspect) return {};
    return {plot->PlotRect.GetWidth()/plot->Axes[ImAxis_X1].Range.Size(),
            plot->PlotRect.GetHeight()/plot->Axes[ImAxis_Y1].Range.Size()};
}
}
