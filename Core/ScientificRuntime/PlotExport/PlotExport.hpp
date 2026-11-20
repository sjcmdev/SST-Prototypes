#pragma once
#include <filesystem>
#include <span>
#include <string>
#include <utility>

namespace SST {
class JobContext;
std::pair<std::string, std::string> ResolvePlotFont(const std::string& family, const std::string& file);
struct PlotExportRequest {
    std::string snapshotJson;
    std::filesystem::path destination;
    std::string format;
    double dpi = 300;
};
// Call from the app's command/job path. Stages the whole batch before replacing destinations.
void ExportPlotsJson(JobContext&, std::span<const PlotExportRequest>);
}
