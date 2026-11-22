#pragma once
#include <sst/plotlab/GraphSnapshot.hpp>
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
namespace SST { class CommandRegistry; class JobSystem; }
namespace SST::Prototypes::JFM
{
    Path JFMPlotExportDirectory();
    void ConfigureJFMPlotServices(WeakRef<CommandRegistry>, WeakRef<JobSystem>);
    struct JFMPlotSize { double widthMm, heightMm; };
    Result<JobSnapshot> JFMPlotExportStatus(JobId id);
    Result<JobId> ExportJFMPlot(std::shared_ptr<const sst::plotlab::GraphSnapshot>, const std::string &path,
                               const std::string &format, double dpi, std::optional<JFMPlotSize> size = {});
}
