#pragma once
#include <sst/plotlab/GraphSnapshot.hpp>
#include <nlohmann/json.hpp>
#include "Core/Utils/Memory.hpp"
namespace SST { class CommandRegistry; class JobSystem; }
namespace SST::Prototypes::JFM
{
    void ConfigureJFMPlotServices(WeakRef<CommandRegistry>, WeakRef<JobSystem>);
    // JSON boundary shared by embedded Python and the GUI export controls.
    nlohmann::json RunJFMPlotRequest(const nlohmann::json &request);
    nlohmann::json ExportJFMPlot(const sst::plotlab::GraphSnapshot &, const std::string &path,
                               const std::string &format, double dpi);
}
