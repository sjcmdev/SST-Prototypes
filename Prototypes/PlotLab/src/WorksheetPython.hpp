#pragma once
#include "PlotLabDocument.hpp"
namespace SST::Prototypes::PlotLab::PythonBinding {
// Python protocol edge, also exercised directly by codec contract tests.
std::string WorksheetCommandJson(Document&, const std::string&);
}
#if DS_PYTHON_CAPI_AVAILABLE
#include <nanobind/nanobind.h>
namespace SST::Prototypes::PlotLab::PythonBinding {
void BindWorksheetRuntime(nanobind::module_&, nanobind::class_<Document>&);
bool TrustedFile(const std::filesystem::path&);
void RememberTrustedFile(const std::filesystem::path&);
}
#endif
namespace SST::Prototypes::PlotLab {
std::string PythonString(std::string_view);
}
