#pragma once
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <implot_internal.h>

namespace SST::Prototypes::CapLab {
struct NyquistScale {
    double xPixelsPerOhm=0, yPixelsPerOhm=0;
};
// Read the rendered ranges after ImPlot has constrained resize and wheel zoom.
inline NyquistScale RenderISPlot(const char* id, const sst::ui::plot::PlotSpec& spec,
    const sst::ui::plot::SeriesEntryView* series, std::size_t count,
    sst::ui::plot::PlotViewState& state, ImVec2 size) {
    // Native fitting expands both equal-scale axes without cropping the data.
    if (spec.equalAspect && !state.fitted) ImPlot::SetNextAxesToFit();
    if (spec.equalAspect) ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding,ImVec2(.05f,.05f));
    sst::ui::plot::RenderPlot(id,spec,series,count,state,{},size);
    if (spec.equalAspect) ImPlot::PopStyleVar();
    const auto* plot=ImPlot::GetPlot(id);
    if (!plot || !spec.equalAspect) return {};
    return {plot->PlotRect.GetWidth()/plot->Axes[ImAxis_X1].Range.Size(),
            plot->PlotRect.GetHeight()/plot->Axes[ImAxis_Y1].Range.Size()};
}
}
