#pragma once
#include <nlohmann/json.hpp>
#include <sst/ui/Theme/ScientificPlotPreset.hpp>
#include <sst/ui/Plot/Model.hpp>
#include <tuple>
namespace SST::Prototypes::CapLab {
inline bool SameStoredView(const sst::ui::plot::PlotViewState &a, const sst::ui::plot::PlotViewState &b) {
    if (a.camera) sst::ui::plot::ValidateCamera(*a.camera);
    if (b.camera) sst::ui::plot::ValidateCamera(*b.camera);
    auto bounds=[](const auto &v) {return std::tie(v.minX,v.maxX,v.minY,v.maxY);};
    auto ticks=[](const auto &x,const auto &y) {return std::equal(x.begin(),x.end(),y.begin(),y.end(),[](const auto &l,const auto &r) {
        return std::tie(l.value,l.label,l.major)==std::tie(r.value,r.label,r.major);
    });};
    return a.visibility==b.visibility && a.viewport.has_value()==b.viewport.has_value() &&
        (!a.viewport || bounds(*a.viewport)==bounds(*b.viewport)) && a.camera.has_value()==b.camera.has_value() &&
        (!a.camera || (a.camera->rotation==b.camera->rotation && a.camera->limits==b.camera->limits)) &&
        ticks(a.xTicks,b.xTicks) && ticks(a.yTicks,b.yTicks) && ticks(a.zTicks,b.zTicks);
}
inline sst::ui::plot::Color SampleTemperatureColor(std::uint64_t sample, const nlohmann::json &temperature) {
    std::uint64_t hash=14695981039346656037ull;
    const auto key=std::to_string(sample)+":"+(temperature.is_number() ? nlohmann::json(temperature.get<double>()).dump() : temperature.dump());
    for (unsigned char c:key) {hash^=c; hash*=1099511628211ull;}
    return sst::ui::plot::ScientificColor(hash);
}
inline void FormatCompareAxes(sst::ui::plot::PlotSpec &spec) {
    spec.customAxes=true;
    for (auto *axis:{&spec.x,&spec.y,&spec.z})
        axis->labels.format=axis->scale==sst::ui::plot::AxisScale::Log10 ?
            sst::ui::plot::LabelFormat::Power10 : sst::ui::plot::LabelFormat::Engineering;
}
}
