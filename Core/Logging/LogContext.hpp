#pragma once
#include <cstdint>
#include <string>
#include "Core/Utils/Path.hpp"

namespace SST {
struct LogContext {
    std::string operation, engine, code, runId, memberId;
    std::uint64_t jobId = 0;
    std::uint32_t attempt = 0;
    Path outputDirectory;
};
}
