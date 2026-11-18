#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Core/JobSystem/JobContext.hpp"
namespace SST::Prototypes::CapLab {
void EditCircuit(Project &project, const Json &edit);
Json SimulateIS(const Json &measurement, const Json &circuit, const Json &settings, JobContext &context, bool plots = true);
Json FitIS(const Json &measurement, const Json &circuit, const Json &settings, JobContext &context);
}
