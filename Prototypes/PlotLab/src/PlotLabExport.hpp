#pragma once
#include <filesystem>
#include <string_view>
#include <sst/plotlab/GraphSnapshot.hpp>
namespace SST::Prototypes::PlotLab
{
    std::string FriendlyFontError(std::string_view details);
    std::pair<std::string, std::string> ResolveFont(const std::string &family, const std::string &file,
                                                  const std::filesystem::path &pythonRoot);
    std::string SnapshotJson(const sst::plotlab::GraphSnapshot &);
    void ExportGraph(const sst::plotlab::GraphSnapshot &, const std::filesystem::path &destination,
                     const std::string &format, double dpi, const std::filesystem::path &pythonRoot);
} // namespace SST::Prototypes::PlotLab
