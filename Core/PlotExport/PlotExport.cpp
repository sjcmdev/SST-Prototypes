#include "Core/PlotExport/PlotExport.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#endif
namespace SST::PlotExport
{
    using Json = nlohmann::json;
#if DS_PYTHON_CAPI_AVAILABLE
    namespace nb = nanobind;
    static void PreparePython(const std::filesystem::path &pythonRoot)
    {
            auto sys = nb::module_::import_("sys");
            auto paths = nb::borrow<nb::list>(sys.attr("path"));
            auto append = [&](const std::filesystem::path &p)
            {
                const auto raw = p.u8string();
                const auto text = nb::str(reinterpret_cast<const char *>(raw.c_str()));
                if (!nb::cast<bool>(paths.attr("__contains__")(text)))
                    paths.append(text);
            };
            append(pythonRoot);
            // Use the repository environment during development; app-local runtime remains Core-owned.
            auto parent = pythonRoot;
            for (int depth = 0; depth < 8 && parent.has_parent_path(); ++depth)
            {
                auto site = parent / ".venv" / "Lib" / "site-packages";
                if (std::filesystem::is_directory(site))
                    append(site);
                auto next = parent.parent_path();
                if (next == parent)
                    break;
                parent = next;
            }
    }
#endif
    std::pair<std::string, std::string> ResolveFont(const std::string &family, const std::string &file,
                                                  const std::filesystem::path &pythonRoot)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        if (!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python is unavailable");
        nb::gil_scoped_acquire gil;
        try {
            PreparePython(pythonRoot);
            auto result = nb::module_::import_("sst_plot_export.exporter").attr("resolve_font")(family.c_str(), file.c_str());
            return {nb::cast<std::string>(result[0]), nb::cast<std::string>(result[1])};
        } catch (const nb::python_error &e) { throw std::runtime_error(e.what()); }
#else
        (void)family; (void)file; (void)pythonRoot;
        throw std::runtime_error("Rebuild with embedded Python to resolve graph fonts");
#endif
    }

    std::string SnapshotJson(const sst::plotlab::GraphSnapshot &s) { return sst::plotlab::snapshotJson(s); }
    void ExportGraph(const sst::plotlab::GraphSnapshot &snapshot, const std::filesystem::path &destination,
                     const std::string &format, double dpi, const std::filesystem::path &pythonRoot)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        if (!PythonInterpreter::IsRuntimeInitialized())
            throw std::runtime_error("Embedded Python is unavailable");
        // A Python command may wait for this job after releasing the GIL; do not reacquire its Core job mutex.
        nb::gil_scoped_acquire gil;
        namespace nb = nanobind;
        try
        {
            PreparePython(pythonRoot);
            const auto raw = destination.u8string();
            const auto text = SnapshotJson(snapshot);
            auto data = nb::module_::import_("json").attr("loads")(nb::str(text.c_str()));
            nb::module_::import_("sst_plot_export.exporter")
                .attr("export_graph")(data, nb::str(reinterpret_cast<const char *>(raw.c_str())),
                                      nb::str(format.c_str()), dpi);
        }
        catch (const nb::python_error &e)
        {
            throw std::runtime_error(e.what());
        }
#else
        (void)snapshot;
        (void)destination;
        (void)format;
        (void)dpi;
        (void)pythonRoot;
        throw std::runtime_error("Rebuild with embedded Python to export graphs");
#endif
    }
} // namespace SST::PlotExport
