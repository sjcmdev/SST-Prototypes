#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <complex>
#include <span>
namespace SST::Prototypes::CapLab {
void EditCircuit(Project &project, const Json &edit);
Json SimulateIS(const Json &measurement, const Json &circuit, const Json &settings, JobContext &context, bool plots = true);
void OverlayISMeasurement(Json &result, const Json &measurement);
using ISEvaluationObserver = std::function<void(const ccui::CircuitDocument &, std::span<const std::complex<double>>, double)>;
Json FitIS(const Json &measurement, const Json &circuit, const Json &settings, JobContext &context, const ISEvaluationObserver &observer = {});
}
