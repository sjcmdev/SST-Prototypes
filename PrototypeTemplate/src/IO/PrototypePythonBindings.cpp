#include "IO/PrototypePythonBindings.hpp"
#include "Core/ScientificRuntime/Python/PythonApi.hpp"
#include "Storage/PrototypeDocument.hpp"
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
NB_MODULE(sst___PROTOTYPE_NAME_LOWER__, module)
{
    module.def("conductivity", [](double voltage, double temperature)
    {
        const SST::Prototypes::__PROTOTYPE_NAME__::PrototypeInput input{voltage, temperature};
        SST::Prototypes::__PROTOTYPE_NAME__::ValidateInput(input);
        return SST::Prototypes::__PROTOTYPE_NAME__::PrototypeBackend::Evaluate(input).conductivity;
    });
}
#endif
namespace SST::Prototypes::__PROTOTYPE_NAME__
{
    void RegisterPrototypePythonModule()
    {
#if DS_PYTHON_CAPI_AVAILABLE
        static bool registered = false;
        if (registered) return;
        if (Py_IsInitialized() || PyImport_AppendInittab("sst___PROTOTYPE_NAME_LOWER__", PyInit_sst___PROTOTYPE_NAME_LOWER__) != 0)
            throw std::runtime_error("Register sst___PROTOTYPE_NAME_LOWER__ before starting Python.");
        registered = true;
#endif
    }
}
