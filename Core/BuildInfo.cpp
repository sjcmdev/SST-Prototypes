#include "Core/dspch.hpp"
#include "Core/BuildInfo.hpp"
#include "BuildInfo.generated.hpp"

namespace SST {
#define SST_STRINGIFY_IMPL(x) #x
#define SST_STRINGIFY(x) SST_STRINGIFY_IMPL(x)
const BuildInfo& GetBuildInfo() {
    static const BuildInfo info=[] {
        auto result=GeneratedBuildInfo;
#ifdef _MSC_FULL_VER
        result.toolchain="MSVC " SST_STRINGIFY(_MSC_FULL_VER);
#else
        result.toolchain="unqualified";
#endif
#if defined(DS_DIST)
        result.configuration="Dist";
#elif defined(DS_DEBUG)
        result.configuration="Debug";
#else
        result.configuration="Release";
#endif
        result.pythonAvailable=DS_PYTHON_CAPI_AVAILABLE!=0;
        return result;
    }();
    return info;
}
std::string BuildDescription() {
    const auto& b = GetBuildInfo();
    return "Semiconductor Studio " + std::string(b.version) + "\nCommit: " + std::string(b.revision)
        + "\nBuild UTC: " + std::string(b.builtUtc) + "\nSource SHA256: " + std::string(b.sourceSha256)
        + "\nToolchain: " + std::string(b.toolchain) + "\nConfiguration: " + std::string(b.configuration)
        + "\nPython binding: " + (b.pythonAvailable ? "available" : "unavailable")
        + "\nDependencies SHA256:\n" + std::string(b.dependencySha256);
}
}
