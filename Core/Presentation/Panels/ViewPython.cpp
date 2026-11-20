#include "Core/dspch.hpp"
#include "Presentation/Panels/ViewHost.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"

#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#endif

namespace SST::ViewDetail
{
    static WeakRef<ViewHost> pythonHost;
    void BindPythonHost(WeakRef<ViewHost> host) { pythonHost = std::move(host); }
}

#if DS_PYTHON_CAPI_AVAILABLE
NB_MODULE(sst_views, module)
{
    module.def("execute", [](const char *request) {
        using Json = nlohmann::json;
        Json response;
        try
        {
            auto host = SST::ViewDetail::pythonHost.lock();
            response = host ? host->Execute(Json::parse(request)) : Json{{"ok", false}, {"error", {{"code", "view.unavailable"}, {"message", "No view host is bound."}}}};
        }
        catch (const std::exception &error)
        {
            response = {{"ok", false}, {"error", {{"code", "view.invalid"}, {"message", error.what()}}}};
        }
        return nanobind::str(response.dump().c_str());
    }, nanobind::arg("request_json"));
}
#endif

namespace SST
{
    void RegisterViewPythonModule()
    {
#if DS_PYTHON_CAPI_AVAILABLE
        static const bool registered = PyImport_AppendInittab("sst_views", PyInit_sst_views) == 0;
        if (!registered) throw std::runtime_error("Cannot register sst_views before Python startup.");
#endif
    }
}
