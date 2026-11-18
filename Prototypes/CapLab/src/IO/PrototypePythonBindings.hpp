#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Core/JobSystem/JobContext.hpp"
namespace SST::Prototypes::CapLab {
void RegisterPrototypePythonModule();
Json ExportData(const Project &, const Path &, JobContext &);
Json ReadAnalysis(const Json &measurement, const Json &options, const Json &xd, JobContext &context);
Json ReadLabAS(const std::string &text, const Path &source, char decimal, JobContext &context, const std::string &options = "");
}
