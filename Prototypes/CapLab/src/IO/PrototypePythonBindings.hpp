#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include <sst/plotlab/GraphSnapshot.hpp>
namespace SST::Prototypes::CapLab {
struct GraphExport { std::shared_ptr<const pl::GraphSnapshot> snapshot; Path path; };
struct GraphExportResult { bool batch = false; std::vector<GraphExport> graphs; };
void ExportGraphs(const GraphExportResult &, double dpi, JobContext &);
Json GraphExportJson(const GraphExportResult &);
void RegisterPrototypePythonModule();
Json ExportData(const Project &, const Path &, JobContext &);
Json ReadAnalysis(const Json &measurement, const Json &options, const Json &xd, JobContext &context);
Json ReadLabAS(const std::string &text, const Path &source, char decimal, JobContext &context, const Json &options = Json::object(), bool provenanceOnly = false);
}
