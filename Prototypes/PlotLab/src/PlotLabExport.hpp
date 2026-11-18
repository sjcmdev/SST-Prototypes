#pragma once
#include <filesystem>
#include <sst/plotlab/GraphSnapshot.hpp>
namespace SST::Prototypes::PlotLab
{
    std::string SnapshotJson(const sst::plotlab::GraphSnapshot &);
    void ExportGraph(const sst::plotlab::GraphSnapshot &, const std::filesystem::path &destination,
                     const std::string &format, double dpi, const std::filesystem::path &pythonRoot);
} // namespace SST::Prototypes::PlotLab
