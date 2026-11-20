#include "Core/dspch.hpp"
#include "Hdf5Session.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
NB_MODULE(sst_hdf5, module) {
    nanobind::class_<SST::Hdf5Session>(module, "Session")
        .def(nanobind::init<>())
        .def("start", [](SST::Hdf5Session& session, const std::string& request) {
            try { return session.Start(nlohmann::json::parse(request)).dump(); }
            catch (const nlohmann::json::exception& e) {
                return nlohmann::json{{"ok", false}, {"error", {{"code", "hdf5.invalid_request"}, {"message", e.what()}}}}.dump();
            }
        }, nanobind::arg("request_json"))
        .def("poll", [](SST::Hdf5Session& session) { return session.Poll().dump(); })
        .def("cancel", [](SST::Hdf5Session& session) { return session.Cancel().dump(); });
}
#endif
namespace SST {
void RegisterHdf5PythonModule() {
#if DS_PYTHON_CAPI_AVAILABLE
    static bool registered = false;
    if (registered) return;
    if (Py_IsInitialized() || PyImport_AppendInittab("sst_hdf5", PyInit_sst_hdf5) != 0)
        throw std::runtime_error("Register sst_hdf5 before starting embedded Python");
    registered = true;
#endif
}
}
