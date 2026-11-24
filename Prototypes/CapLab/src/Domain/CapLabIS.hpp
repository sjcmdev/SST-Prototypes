#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <complex>
#include <span>
namespace SST::Prototypes::CapLab {
void EditCircuit(Project &project, const Json &edit);
AnalysisResult SimulateIS(const AnalysisMeasurementData &measurement, const Json &circuit, const Json &settings, JobContext &context, bool plots = true);
void OverlayISMeasurement(AnalysisResult &result, const AnalysisMeasurementData &measurement);
using ISEvaluationObserver = std::function<void(const ccui::CircuitDocument &, std::span<const std::complex<double>>, double)>;
AnalysisResult FitIS(const AnalysisMeasurementData &measurement, const Json &circuit, const Json &settings, JobContext &context, const ISEvaluationObserver &observer = {});
}
