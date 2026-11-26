#pragma once
#include <string>
#include <string_view>

namespace SST {
struct BuildInfo {
    std::string_view version, revision, builtUtc, sourceSha256, dependencySha256;
    std::string_view toolchain, configuration;
    bool pythonAvailable = false;
};
[[nodiscard]] const BuildInfo& GetBuildInfo();
[[nodiscard]] std::string BuildDescription();
}
