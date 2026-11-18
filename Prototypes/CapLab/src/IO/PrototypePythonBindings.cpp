#include "IO/PrototypePythonBindings.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Domain/CapLabBundleImport.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Core/ScientificRuntime/Python/PythonGilScope.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/function.h>
NB_MODULE(sst_caplab, module)
{
    using namespace SST::Prototypes::CapLab;
    module.def("project_from_preview", [](const std::string &preview, std::optional<double> temperature)
    {
        Project project;
        const auto j = Json::parse(preview);
        if (j.contains("samples")) { BundleChange change(project,j); }
        else { ImportChange change(project,j,temperature,CFTemplate{}); }
        return Serialize(project);
    });
    module.def("roundtrip", [](const std::string &text) { return Serialize(Parse(text)); });
}
#endif
namespace SST::Prototypes::CapLab
{
    void RegisterPrototypePythonModule()
    {
#if DS_PYTHON_CAPI_AVAILABLE
        static bool registered = false;
        if (registered) return;
        if (Py_IsInitialized() || PyImport_AppendInittab("sst_caplab", PyInit_sst_caplab) != 0)
            throw std::runtime_error("Register sst_caplab before starting Python.");
        registered = true;
#endif
    }
    Json ReadLabAS(const std::string &text, const Path &source, char decimal, JobContext &context, const std::string &options)
    {
#if DS_PYTHON_CAPI_AVAILABLE
        PythonGilAcquireScope gil;
        if (!gil.IsActive()) throw std::runtime_error("Embedded Python is unavailable; rebuild with embedding.");
        namespace nb = nanobind;
        try
        {
            auto paths = nb::module_::import_("sys").attr("path");
            const auto executable = Platform::CurrentExecutablePath();
            if (!executable) throw std::runtime_error(executable.Error().technicalDetails);
            const auto root = executable.Value().parent_path().Join("python");
            const std::string pythonRoot = FileSystem::Exists(root.Join("caplab").Native()) ? root.Utf8() : SST_PROTOTYPE_PYTHON_ROOT;
            if (!nb::cast<bool>(paths.attr("__contains__")(pythonRoot))) paths.attr("insert")(0,pythonRoot);
            const std::function<bool()> cancel = [&] { return context.IsCancellationRequested(); };
            const std::function<void(double)> progress = [&](double v) { context.SetProgress(float(v),1); };
            const auto result = nb::module_::import_("caplab").attr("preview_json")(text,source.Utf8(),std::string(1,decimal),cancel,progress,options);
            return Json::parse(nb::cast<std::string>(result));
        }
        catch (const nb::python_error &e)
        {
            context.ThrowIfCancellationRequested();
            throw std::runtime_error(e.what());
        }
#else
        (void)text; (void)source; (void)decimal; (void)context; (void)options;
        throw std::runtime_error("Embedded Python is unavailable; rebuild with embedding.");
#endif
    }
}
