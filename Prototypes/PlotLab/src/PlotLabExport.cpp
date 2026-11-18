#include "PlotLabExport.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Core/ScientificRuntime/Python/PythonGilScope.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#endif
namespace SST::Prototypes::PlotLab
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
        PythonGilAcquireScope gil;
        if (!gil.IsActive()) throw std::runtime_error("Embedded Python is unavailable");
        try {
            PreparePython(pythonRoot);
            auto result = nb::module_::import_("sst_plotlab.exporter").attr("resolve_font")(family.c_str(), file.c_str());
            return {nb::cast<std::string>(result[0]), nb::cast<std::string>(result[1])};
        } catch (const nb::python_error &e) { throw std::runtime_error(e.what()); }
#else
        (void)family; (void)file; (void)pythonRoot;
        throw std::runtime_error("Rebuild with embedded Python to resolve graph fonts");
#endif
    }

    std::string SnapshotJson(const sst::plotlab::GraphSnapshot &s)
    {
        auto axis = [](const auto &spec, const auto &ticks)
        {
            Json t = Json::array();
            for (const auto &tick : ticks)
                t.push_back({{"value", tick.value}, {"label", tick.label}, {"major", tick.major}});
            return Json{{"label", spec.label},
                        {"log", spec.scale == sst::ui::plot::AxisScale::Log10},
                        {"grid", spec.grid},
                        {"ticks", t}};
        };
        Json series = Json::array();
        for (const auto &entry : s.series)
        {
            const auto &st = entry.style;
            series.push_back({{"plot_id", entry.plot},
                              {"label", entry.label},
                              {"x", entry.x},
                              {"y", entry.y},
                              {"y_error", entry.yError},
                              {"kind", entry.kind},
                              {"style",
                               {{"color", st.color},
                                {"fill", st.markerFill},
                                {"outline", st.markerOutline},
                                {"marker", st.marker},
                                {"line_pt", st.lineWidthPt},
                                {"marker_pt", st.markerSizePt},
                                {"cap_pt", st.errorCapPt},
                                {"legend", st.legend}}}});
        }
        return Json{{"project_id", s.project},
                    {"graph_id", s.graph},
                    {"revision", s.revision},
                    {"width_mm", s.widthMm},
                    {"height_mm", s.heightMm},
                    {"font_family", s.fontFamily},
                    {"font_file", s.fontFile},
                    {"font_pt", s.fontSizePt},
                    {"ranges", {s.ranges.minX, s.ranges.maxX, s.ranges.minY, s.ranges.maxY}},
                    {"x_axis", axis(s.spec.x, s.xTicks)},
                    {"y_axis", axis(s.spec.y, s.yTicks)},
                    {"legend", {{"visible", s.spec.legend.visible}, {"outside", s.spec.legend.outside}}},
                    {"series", series}}
            .dump();
    }
    void ExportGraph(const sst::plotlab::GraphSnapshot &snapshot, const std::filesystem::path &destination,
                     const std::string &format, double dpi, const std::filesystem::path &pythonRoot)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        PythonGilAcquireScope gil;
        if (!gil.IsActive())
            throw std::runtime_error("Embedded Python is unavailable");
        namespace nb = nanobind;
        try
        {
            PreparePython(pythonRoot);
            const auto raw = destination.u8string();
            const auto text = SnapshotJson(snapshot);
            auto data = nb::module_::import_("json").attr("loads")(nb::str(text.c_str()));
            nb::module_::import_("sst_plotlab.exporter")
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
} // namespace SST::Prototypes::PlotLab
