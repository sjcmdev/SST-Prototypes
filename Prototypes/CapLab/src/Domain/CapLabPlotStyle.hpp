#pragma once
#include <nlohmann/json.hpp>
#include <sst/ui/Theme/ScientificPlotPreset.hpp>
#include <sst/ui/Plot/Model.hpp>
#include <tuple>
namespace SST::Prototypes::CapLab {
inline bool SameStoredView(const sst::ui::plot::PlotViewState &a, const sst::ui::plot::PlotViewState &b) {
    if (a.camera) sst::ui::plot::ValidateCamera(*a.camera);
    if (b.camera) sst::ui::plot::ValidateCamera(*b.camera);
    return sst::ui::plot::StoredViewEqual(a,b);
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
