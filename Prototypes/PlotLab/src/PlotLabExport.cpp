#include "PlotLabExport.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include "Core/Utils/Path.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/ndarray.h>
#endif
namespace SST::Prototypes::PlotLab
{
    sst::plotlab::ImageGrid LoadIntensityImage(const std::filesystem::path& path,sst::plotlab::ImageGrid grid) {
#if DS_PYTHON_CAPI_AVAILABLE
        nanobind::gil_scoped_acquire gil;
        namespace nb=nanobind;
        const auto text=path.u8string();auto image=nb::module_::import_("PIL.Image").attr("open")(nb::str(reinterpret_cast<const char*>(text.c_str())));
        try {
            const auto mode=nb::cast<std::string>(image.attr("mode"));
            if(mode!="L" && mode!="I" && mode!="F" && !mode.starts_with("I;16"))throw std::invalid_argument("Choose a single-channel intensity image; RGB conversion requires an explicit channel");
            auto values=nb::module_::import_("numpy").attr("ascontiguousarray")(image,nb::arg("dtype")="float64");
            auto array=nb::cast<nb::ndarray<nb::numpy,const double,nb::c_contig>>(values);
            if(array.ndim()!=2)throw std::invalid_argument("Expected a two-dimensional intensity image");
            grid.rows=array.shape(0);grid.columns=array.shape(1);grid.pixels.assign(array.data(),array.data()+array.size());grid.name=Path(path.stem()).Utf8();grid.sourcePath=path;image.attr("close")();return grid;
        }catch(...){image.attr("close")();throw;}
#else
        (void)path;(void)grid;throw std::runtime_error("Rebuild with embedded Python for image import");
#endif
    }
    using Json = nlohmann::json;
    std::string FriendlyFontError(std::string_view details)
    {
        if (details.find("ModuleNotFoundError") != std::string_view::npos || details.find("ImportError") != std::string_view::npos || details.find("No module named") != std::string_view::npos)
            return "Graph fonts need the Python export dependencies. Install them and click Retry.";
        if (details.find("Embedded Python") != std::string_view::npos)
            return "Graph fonts need Python. The default UI font is used for this preview.";
        while (!details.empty() && (details.back() == '\n' || details.back() == '\r')) details.remove_suffix(1);
        const auto end = details.find_last_of("\r\n");
        if (end != std::string_view::npos) details.remove_prefix(end + 1);
        return "Graph font unavailable: " + std::string(details.substr(0, 200));
    }
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
            append(std::filesystem::path(SST_PLOTLAB_PYTHON_ROOT).parent_path().parent_path().parent_path() / "Core/ScientificRuntime/Python");

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
            auto result = nb::module_::import_("sst_plotlab.exporter").attr("resolve_font")(family.c_str(), file.c_str());
            return {nb::cast<std::string>(result[0]), nb::cast<std::string>(result[1])};
        } catch (const nb::python_error &e) { throw std::runtime_error(FriendlyFontError(e.what())); }
#else
        (void)family; (void)file; (void)pythonRoot;
        throw std::runtime_error("Rebuild with embedded Python to resolve graph fonts");
#endif
    }

    std::string SnapshotJson(const sst::plotlab::GraphSnapshot &snapshot) { return sst::plotlab::snapshotJson(snapshot); }
    void ExportGraph(const sst::plotlab::GraphSnapshot &snapshot, const std::filesystem::path &destination,
                     const std::string &format, double dpi, const std::filesystem::path &pythonRoot,
                     const std::array<double,3>& backgroundRgb)
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
            auto background = nb::make_tuple(backgroundRgb[0], backgroundRgb[1], backgroundRgb[2]);
            nb::module_::import_("sst_plotlab.exporter")
                .attr("export_graph")(data, nb::str(reinterpret_cast<const char *>(raw.c_str())),
                                      nb::str(format.c_str()), dpi, background);
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
        (void)backgroundRgb;
        throw std::runtime_error("Rebuild with embedded Python to export graphs");
#endif
    }
    void ExportData(const sst::plotlab::DataExportSnapshot& snapshot,const std::filesystem::path& destination,
                    const std::string& format,const std::filesystem::path& pythonRoot,const std::function<bool()>& cancelled)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        if(!PythonInterpreter::IsRuntimeInitialized()) throw std::runtime_error("Embedded Python is unavailable");
        nb::gil_scoped_acquire gil;
        try {
            PreparePython(pythonRoot);
            const auto& sheet=std::get<sst::plotlab::Worksheet>(snapshot.worksheet->data);
            nb::dict data;nb::list columns,rows,sources;
            for(std::size_t row=0;row<snapshot.rows.size();++row) rows.append(row);
            for(const auto& [key,value]:sheet.source) if(key=="path" || key=="file" || key=="filename") sources.append(nb::str(value.c_str()));
            for(const auto& object:snapshot.columns) {
                const auto& c=std::get<sst::plotlab::Column>(object->data);nb::dict column;nb::list values,masks;
                column["id"]=object->id;column["name"]=nb::str(object->name.c_str());column["unit"]=nb::str(c.units.c_str());
                std::visit([&](const auto& v){for(auto row:snapshot.rows) values.append(v.at(row));},c.values);
                for(auto index:snapshot.rows) {const auto row=sheet.rows.at(index);masks.append(sheet.masks.contains(row)||c.masks.contains(row));}
                column["values"]=values;column["mask"]=masks;columns.append(column);
            }
            using namespace sst::plotlab;
            data["columns"]=columns;data["rows"]=rows;data["source_paths"]=sources;
            data["mask_policy"]=snapshot.masks==DataMaskPolicy::Preserve?"preserve":snapshot.masks==DataMaskPolicy::Missing?"missing":"omit_rows";
            data["missing_policy"]=snapshot.missing==DataMissingPolicy::Empty?"empty":"nan";
            const auto raw=destination.u8string();
            auto stop=nb::cpp_function([&]{return cancelled && cancelled();});
            nb::module_::import_("sst_plotlab.exporter").attr("export_data")(data,nb::str(reinterpret_cast<const char*>(raw.c_str())),format.c_str(),stop);
        } catch(const nb::python_error& e) {throw std::runtime_error(e.what());}
#else
        (void)snapshot;(void)destination;(void)format;(void)pythonRoot;(void)cancelled;
        throw std::runtime_error("Rebuild with embedded Python to export data");
#endif
    }
} // namespace SST::Prototypes::PlotLab
