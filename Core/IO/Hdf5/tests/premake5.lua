newoption { trigger = "to", value = "PATH", description = "Build directory" }
local repo = path.getabsolute("../../../..", _SCRIPT_DIR)
local libraries = repo .. "/../SST-Libraries"
local build = path.getabsolute(_OPTIONS["to"] or (repo .. "/build/hdf5"))
local function python(query)
    return (os.outputof('python -c "' .. query .. '"') or ""):gsub("%s+$", "")
end
local pyInclude = python("import sysconfig; print(sysconfig.get_paths()['include'])")
local pyLib = python("import sys; print(sys.base_prefix + '/libs')")
local pyName = python("import sys; print('python' + str(sys.version_info.major) + str(sys.version_info.minor))")
_SST_LIBRARIES_BUILD_ROOT = build
workspace "Hdf5Tests"
    architecture "x86_64"
    configurations { "Release" }
    location(build .. "/generated/%{_ACTION}")
include(libraries .. "/sst_libraries.lua")
project "Hdf5Tests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    location(build .. "/generated/%{_ACTION}/Hdf5Tests")
    targetdir(build .. "/bin/Release-windows-x86_64/Hdf5Tests")
    objdir(build .. "/bin-int/%{prj.name}")
    runtime "Release"
    optimize "On"
    systemversion "latest"
    flags { "MultiProcessorCompile" }
    buildoptions { "/utf-8", "/bigobj", "/MP6" }
    includedirs { repo, repo .. "/Core", repo .. "/Vendor/json/include", repo .. "/Vendor/spdlog/include",
        repo .. "/Vendor/thread-pool/include", repo .. "/Vendor/GoogleTest/googletest/include",
        repo .. "/Vendor/GoogleTest/googletest", repo .. "/Vendor/nanobind/include",
        repo .. "/Vendor/nanobind/ext/robin_map/include", pyInclude, build .. "/generated/build-info" }
    prebuildcommands { 'python "' .. repo .. '/scripts/generate_build_info.py" --output "' .. build .. '/generated/build-info/BuildInfo.generated.hpp"' }
    files { _SCRIPT_DIR .. "/Hdf5CoreTests.cpp", libraries .. "/tests/Hdf5Tests.cpp",
        repo .. "/Core/IO/Hdf5/Hdf5Session.cpp", repo .. "/Core/IO/Hdf5/Hdf5Python.cpp",
        repo .. "/Core/Commands/Command.cpp", repo .. "/Core/Commands/CommandService.cpp",
        repo .. "/Core/Commands/CommandRegistry.cpp", repo .. "/Core/Undo/UndoStack.cpp",
        repo .. "/Core/Capabilities/CapabilityService.cpp", repo .. "/Core/Capabilities/CapabilityRegistry.cpp",
        repo .. "/Core/JobSystem/JobSystem.cpp", repo .. "/Core/JobSystem/JobContext.cpp",
        repo .. "/Core/EventSystem/BusEventSystem/EventBus.cpp", repo .. "/Core/Threading/ThreadAffinity.cpp",
        repo .. "/Core/Logging/Logger.cpp", repo .. "/Core/Logging/LogRegistry.cpp",
        repo .. "/Core/Logging/LogRegistrySink.cpp", repo .. "/Core/Utils/Path.cpp",
        repo .. "/Core/BuildInfo.cpp", repo .. "/Core/Diagnostics/LocalDiagnostics.cpp", repo .. "/Core/IO/TextFileIO.cpp",
        repo .. "/Vendor/GoogleTest/googletest/src/gtest-all.cc", repo .. "/Vendor/nanobind/src/nb_combined.cpp" }
    defines { "DS_RELEASE", "SPDLOG_WCHAR_FILENAMES", "DS_PYTHON_CAPI_AVAILABLE=1", "NB_STATIC", "Py_NO_LINK_LIB", "NOMINMAX",
        'HDF5_PYTHON_TEST="' .. _SCRIPT_DIR .. '/test_interop.py"',
        'HDF5_TEST_PYTHON="' .. repo .. '/.venv/Scripts/python.exe"',
        'HDF5_TEST_ENV="' .. repo .. '/.venv/Lib/site-packages"' }
    libdirs { pyLib }
    links { pyName, "Psapi", "Shell32" }
    sst_link_hdf5()
