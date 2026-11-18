#include "IO/PrototypePythonBindings.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Storage/PrototypeDocument.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
NB_MODULE(sst_windowchromelab, module)
{
    module.def("conductivity", [](double voltage, double temperature)
    {
        const SST::Prototypes::WindowChromeLab::PrototypeInput input{voltage, temperature};
        SST::Prototypes::WindowChromeLab::ValidateInput(input);
        return SST::Prototypes::WindowChromeLab::PrototypeBackend::Evaluate(input).conductivity;
    });
}
#endif
namespace SST::Prototypes::WindowChromeLab
{
    void RegisterPrototypePythonModule()
    {
#if DS_PYTHON_CAPI_AVAILABLE
        static bool registered = false;
        if (registered) return;
        if (Py_IsInitialized() || PyImport_AppendInittab("sst_windowchromelab", PyInit_sst_windowchromelab) != 0)
            throw std::runtime_error("Register sst_windowchromelab before starting Python.");
        registered = true;
#endif
    }
}
