#include "Core/dspch.hpp"
#include "PlotExport.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/function.h>
#include <nanobind/stl/string.h>
#endif

namespace SST {
#if DS_PYTHON_CAPI_AVAILABLE
namespace {
namespace nb = nanobind;
nb::module_ PreparePython() {
        auto paths = nb::module_::import_("sys").attr("path");
        auto append = [&](const std::filesystem::path& path) {
            const auto raw = path.u8string();
            const auto text = nb::str(reinterpret_cast<const char*>(raw.c_str()));
            if (!nb::cast<bool>(paths.attr("__contains__")(text))) paths.attr("append")(text);
        };
        const auto executable = Platform::CurrentExecutablePath();
        if (!executable) throw std::runtime_error(executable.Error().technicalDetails);
        const auto local = executable.Value().parent_path().Join("python").Native();
        const auto root = std::filesystem::is_directory(local / "sst_plot_export") ? local : std::filesystem::path(SST_PLOT_EXPORT_PYTHON_ROOT);
        append(root);
        auto parent = std::filesystem::path(SST_PLOT_EXPORT_PYTHON_ROOT);
        for (int depth = 0; depth < 8 && parent.has_parent_path(); ++depth) {
            const auto site = parent / ".venv/Lib/site-packages";
            if (std::filesystem::is_directory(site)) append(site);
            parent = parent.parent_path();
        }
    return nb::module_::import_("sst_plot_export.exporter");
}
}
#endif
std::pair<std::string, std::string> ResolvePlotFont(const std::string& family, const std::string& file) {
#if DS_PYTHON_CAPI_AVAILABLE
    if (!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python is unavailable");
    nb::gil_scoped_acquire gil;
    try {
        auto result=PreparePython().attr("resolve_font")(family.c_str(),file.c_str());
        return {nb::cast<std::string>(result[0]),nb::cast<std::string>(result[1])};
    } catch (const nb::python_error& error) { throw std::runtime_error(error.what()); }
#else
    (void)family; (void)file;
    throw std::runtime_error("Rebuild with embedded Python to resolve graph fonts");
#endif
}
void ExportPlotsJson(JobContext& context, std::span<const PlotExportRequest> requests) {
    context.ThrowIfCancellationRequested();
#if DS_PYTHON_CAPI_AVAILABLE
    if (!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python is unavailable");
    // ponytail: PlotLab keeps its paused app copy; migrate it to this exporter when work resumes.
    // Acquire only the GIL: Python callers release it while holding Core's outer scope mutex.
    namespace nb = nanobind;
    nb::gil_scoped_acquire gil;
    try {
        auto exporter=PreparePython();
        auto json = nb::module_::import_("json");
        nb::list exports;
        for (const auto& request : requests) {
            nb::dict item;
            item["snapshot"] = json.attr("loads")(nb::str(request.snapshotJson.c_str()));
            const auto raw = request.destination.u8string();
            item["destination"] = nb::str(reinterpret_cast<const char*>(raw.c_str()));
            item["format"] = nb::str(request.format.c_str());
            item["dpi"] = nb::float_(request.dpi);
            exports.append(item);
        }
        const std::function<bool()> cancelled = [&] { return context.IsCancellationRequested(); };
        exporter.attr("export_plots")(exports, cancelled);
        context.SetProgress(1, 1);
    } catch (const nb::python_error& error) {
        context.ThrowIfCancellationRequested();
        throw std::runtime_error(error.what());
    }
#else
    (void)requests;
    throw std::runtime_error("Rebuild with embedded Python to export graphs");
#endif
}
}

