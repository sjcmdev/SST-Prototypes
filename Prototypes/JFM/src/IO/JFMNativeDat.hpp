#pragma once
#include "IO/JFMPythonImportBridge.hpp"

namespace SST::Prototypes::JFM {
bool CanImportNativeDat(const std::vector<Path>&, const JFMImportOptions&);
Result<JFMImportBatchResult> ImportNativeDat(const std::vector<Path>&, const JFMImportOptions&,
                                           const JFMPythonImportBridge::ProgressCallback&);
}
