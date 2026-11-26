#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"
#include <functional>

namespace SST::Prototypes::JFM {
// File edges only; the store and jobs share the immutable typed result.
Result<void> SaveJFMMonteCarloHdf5(const Path& file, const JFMMonteCarloResult& result,
    const std::function<void()>& checkpoint = {});
Result<JFMMonteCarloResult> ReadJFMMonteCarloHdf5(const Path& file);
Result<JFMMonteCarloResult> ReadJFMMonteCarloTsv(const Path& directory);
Result<void> ExportJFMMonteCarloTsv(const Path& directory, const JFMMonteCarloResult& result, double noisePercent, const std::function<void()>& checkpoint = {});
}
