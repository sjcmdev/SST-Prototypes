#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
#include <sst/plotlab/Extensions.hpp>

namespace SST::Prototypes::CapLab {
// Metadata retains the extensible import schema; numeric columns never enter it.
struct AnalysisMeasurementData {
    nlohmann::json metadata;
    std::vector<std::shared_ptr<const std::vector<double>>> columns;
    std::shared_ptr<const std::vector<sst::plotlab::Id>> rowIds;
    bool empty() const { return metadata.is_null(); }
};
struct AnalysisCurveData {
    std::string name;
    std::vector<double> x,y;
    bool fit=false, candidates=false;
    std::optional<double> temperature_K;
    std::optional<std::uint64_t> integerTemperature;
    sst::ui::plot::Color color{.12f,.45f,.78f,1};
};
struct AnalysisPlotData {
    std::string name,xLabel,yLabel;
    bool logX=false,logY=false,equalAspect=false;
    std::vector<AnalysisCurveData> curves;
};
struct AnalysisResult {
    nlohmann::json record;
    std::vector<AnalysisMeasurementData> inputs;
    std::vector<AnalysisPlotData> plots;
};
}
