#pragma once
#include <filesystem>
#include <string_view>
#include <sst/plotlab/GraphSnapshot.hpp>
#include <sst/plotlab/PlotEditing.hpp>
namespace SST::Prototypes::PlotLab
{
    sst::plotlab::ImageGrid LoadIntensityImage(const std::filesystem::path&,sst::plotlab::ImageGrid calibration);
    std::string FriendlyFontError(std::string_view details);
    std::pair<std::string, std::string> ResolveFont(const std::string &family, const std::string &file,
                                                  const std::filesystem::path &pythonRoot);
    std::string SnapshotJson(const sst::plotlab::GraphSnapshot &);
    void ExportGraph(const sst::plotlab::GraphSnapshot &, const std::filesystem::path &destination,
                     const std::string &format, double dpi, const std::filesystem::path &pythonRoot,
                     const std::array<double,3>& backgroundRgb = {1,1,1});
    void ExportData(const sst::plotlab::DataExportSnapshot&, const std::filesystem::path& destination,
                    const std::string& format, const std::filesystem::path& pythonRoot,
                    const std::function<bool()>& cancelled = {});
} // namespace SST::Prototypes::PlotLab
