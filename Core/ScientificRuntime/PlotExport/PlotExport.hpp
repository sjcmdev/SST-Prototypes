#pragma once
#include <filesystem>
#include <functional>
namespace nanobind { class object; }
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
    // Invoked with the GIL held; typed app snapshots stay owned until this Python edge.
    std::function<nanobind::object()> pythonSnapshot;
};
// Call from the app's command/job path. Stages the whole batch before replacing destinations.
void ExportPlotsJson(JobContext&, std::span<const PlotExportRequest>);
}
