#pragma once
#include <nlohmann/json.hpp>
#include <memory>
#include <filesystem>
namespace SST { struct PlotExportRequest; }
namespace sst::plotlab { struct GraphSnapshot; }

namespace SST::Prototypes::JFM
{
    struct JFMPlotSize;
    // JSON adapters are private to the Python boundary and its contract tests.
    nlohmann::json RunJFMFitRequest(const nlohmann::json &request);
    SST::PlotExportRequest MakeJFMPlotExportRequest(std::shared_ptr<const sst::plotlab::GraphSnapshot>,
        const std::filesystem::path &path, const std::string &format, double dpi, JFMPlotSize size);
	bool RegisterJFMPythonBindings();
} // namespace SST::Prototypes::JFM
