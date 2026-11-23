#pragma once
#include "PlotLabDocument.hpp"
namespace SST::Prototypes::PlotLab::PythonBinding {
// Python protocol edge, also exercised directly by codec contract tests.
std::string WorksheetCommandJson(Document&, const std::string&);
}
