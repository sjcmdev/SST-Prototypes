local function readActivePrototypeName()
    local file = io.open(".sst-prototype", "r")
    if file == nil then
        return "Core"
    end

    local name = file:read("*l")
    file:close()
    if name == nil or name == "" then
        return "Core"
    end
    return name
end

workspace "SSTPrototypes"
    architecture "x86_64"
    filter "system:windows"
        defines { "SPDLOG_WCHAR_FILENAMES" }
    filter {}
    startproject (readActivePrototypeName())
    location "build/generated/%{_ACTION}"

    configurations {
        "Debug",
        "Release",
        "Dist"
    }

if _ACTION == "gmake2" then
    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}-gmake2"
else
    outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
end

_SST_ROOT = path.getabsolute(".")
_DS_ROOT = _SST_ROOT

local function trim(value)
    return (value or ""):gsub("^%s+", ""):gsub("%s+$", "")
end

local function pythonConfig(command)
    local executable = _SST_PYTHON_CONFIG_EXE
    if executable:find("%s") then
        executable = "\"" .. executable .. "\""
    end
    return trim(os.outputof(executable .. " -c \"" .. command .. "\"") or "")
end

_SST_PYTHON_CONFIG_EXE = "python"
if os.isfile(".venv/Scripts/python.exe") then
    _SST_PYTHON_CONFIG_EXE = path.getabsolute(".venv/Scripts/python.exe")
elseif os.isfile(".venv/bin/python") then
    _SST_PYTHON_CONFIG_EXE = path.getabsolute(".venv/bin/python")
end

_SST_PYTHON_INCLUDE_DIR = os.getenv("SST_PYTHON_INCLUDE_DIR") or ""
_SST_PYTHON_LIB_DIR = os.getenv("SST_PYTHON_LIB_DIR") or ""
_SST_PYTHON_LIB_NAME = os.getenv("SST_PYTHON_LIB_NAME") or ""
_SST_PYTHON_DLL_PATH = os.getenv("SST_PYTHON_DLL_PATH") or ""

if _SST_PYTHON_INCLUDE_DIR == "" then
    _SST_PYTHON_INCLUDE_DIR = pythonConfig("import sysconfig; print(sysconfig.get_paths().get('include',''))")
end
if _SST_PYTHON_LIB_DIR == "" then
    _SST_PYTHON_LIB_DIR = pythonConfig("import sysconfig; print(sysconfig.get_config_var('LIBDIR') or '')")
end
if _SST_PYTHON_LIB_NAME == "" then
    _SST_PYTHON_LIB_NAME = pythonConfig("import sysconfig; print('python' + (sysconfig.get_config_var('py_version_nodot') or ''))")
end
if _SST_PYTHON_DLL_PATH == "" then
    _SST_PYTHON_DLL_PATH = pythonConfig("import pathlib, sys, sysconfig; print(pathlib.Path(sys.base_prefix) / ('python' + (sysconfig.get_config_var('py_version_nodot') or '') + '.dll'))")
end

if _SST_PYTHON_INCLUDE_DIR ~= "" and not os.isdir(_SST_PYTHON_INCLUDE_DIR) then
    _SST_PYTHON_INCLUDE_DIR = ""
end
if _SST_PYTHON_LIB_DIR ~= "" and not os.isdir(_SST_PYTHON_LIB_DIR) then
    _SST_PYTHON_LIB_DIR = ""
end
if _SST_PYTHON_DLL_PATH ~= "" and not os.isfile(_SST_PYTHON_DLL_PATH) then
    _SST_PYTHON_DLL_PATH = ""
end

_SST_PYTHON_EMBED_AVAILABLE = _SST_PYTHON_INCLUDE_DIR ~= "" and _SST_PYTHON_LIB_DIR ~= "" and _SST_PYTHON_LIB_NAME ~= ""
_DS_PYTHON_INCLUDE_DIR = _SST_PYTHON_INCLUDE_DIR
_DS_PYTHON_LIB_DIR = _SST_PYTHON_LIB_DIR
_DS_PYTHON_LIB_NAME = _SST_PYTHON_LIB_NAME
_DS_PYTHON_EMBED_AVAILABLE = _SST_PYTHON_EMBED_AVAILABLE

group "Dependencies"
include "Vendor/GLFW"
include "Vendor/GLAD"
include "Vendor/ImGui"

project "ImPlot"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"

    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "Vendor/implot/implot.h",
        "Vendor/implot/implot_internal.h",
        "Vendor/implot/implot.cpp",
        "Vendor/implot/implot_items.cpp"
    }

    includedirs {
        "Vendor/ImGui",
        "Vendor/implot"
    }

    filter { "system:windows", "action:vs2022" }
        buildoptions { "/utf-8" }
    filter {}

include "Vendor/thread-pool"
project "ImPlot3D"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"
    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")
    files { "Vendor/implot3d/implot3d*.h", "Vendor/implot3d/implot3d.cpp", "Vendor/implot3d/implot3d_items.cpp", "Vendor/implot3d/implot3d_meshes.cpp" }
    includedirs { "Vendor/ImGui", "Vendor/implot3d" }
    filter { "system:windows", "action:vs2022" }
        buildoptions { "/utf-8" }
    filter {}

include "Vendor/json"
include "Vendor/yaml-cpp"
include "Vendor/ImGuiNotify"
include "Vendor/Tracy"
include "Vendor/GoogleTest/GoogleTest.premake5.lua"
include "Vendor/nanobind"
include "Vendor/nativefiledialog-extended"
group ""

local function applyDependencyRuntimeFilters(projectName)
    project(projectName)
    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release"
        runtime "Release"
        optimize "On"
    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
    filter {}
end

applyDependencyRuntimeFilters("GLAD")
applyDependencyRuntimeFilters("ImGui")
applyDependencyRuntimeFilters("ImPlot")
applyDependencyRuntimeFilters("ImPlot3D")
applyDependencyRuntimeFilters("yaml-cpp")
applyDependencyRuntimeFilters("Tracy")
applyDependencyRuntimeFilters("GoogleTest")
applyDependencyRuntimeFilters("GoogleTestMain")
applyDependencyRuntimeFilters("nfd")
applyDependencyRuntimeFilters("nanobind")
filter {}

project "Core"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }

    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")

    pchheader "Core/dspch.hpp"
    pchsource "Core/dspch.cpp"

    files {
        "Core/dspch.hpp",
        "Core/dspch.cpp",
        "Core/CoreLayer.hpp",
        "Core/CoreLayer.cpp",
        "Core/EntryPoint.hpp",
        "Core/Layer.hpp",
        "Core/Layer.cpp",
        "Core/LayerStack.hpp",
        "Core/LayerStack.cpp",
        "Core/App/Application.hpp",
        "Core/App/Application.cpp",
        "Core/App/ApplicationBootstrap.cpp",
        "Core/App/ApplicationInternals.hpp",
        "Core/App/ApplicationLifecycle.hpp",
        "Core/App/ApplicationLifecycle.cpp",
        "Core/App/ApplicationRuntimeServices.hpp",
        "Core/App/ApplicationState.hpp",
        "Core/App/ApplicationWindow.cpp",
        "Core/App/RendererConfig.hpp",
        "Core/App/RendererTypes.hpp",
        "Core/App/UiConfig.hpp",
        "Core/App/Window.hpp",
        "Core/App/Window.cpp",
        "Core/App/Controllers/**.hpp",
        "Core/App/Controllers/**.cpp",
        "Core/App/Events/**.hpp",
        "Core/App/Events/**.cpp",
        "Core/App/Managers/**.hpp",
        "Core/App/Managers/**.cpp",
        "Core/App/Serialization/**.hpp",
        "Core/App/Serialization/**.cpp",
        "Core/Assets/**.hpp",
        "Core/Assets/**.cpp",
        "Core/Capabilities/**.hpp",
        "Core/Capabilities/**.cpp",
        "Core/Commands/**.hpp",
        "Core/Commands/**.cpp",
        "Core/PlotExport/PlotEditing.hpp",
        "Core/PlotExport/PlotEditing.cpp",
        "Core/Configuration/**.hpp",
        "Core/Diagnostics/**.hpp",
        "Core/Diagnostics/**.cpp",
        "Core/Events/**.hpp",
        "Core/EventSystem/**.hpp",
        "Core/EventSystem/**.cpp",
        "Core/Input/**.hpp",
        "Core/Input/**.cpp",
        "Core/IO/**.hpp",
        "Core/IO/**.cpp",
        "Core/JobSystem/**.hpp",
        "Core/JobSystem/**.cpp",
        "Core/Logging/**.hpp",
        "Core/Logging/**.cpp",
        "Core/Notifications/**.hpp",
        "Core/Notifications/**.cpp",
        "Core/Platform/Platform*.hpp",
        "Core/Platform/Platform*.cpp",
        "Core/Platform/NativeFileDialog.hpp",
        "Core/Platform/NativeFileDialog.cpp",
        "Core/Platform/ProcessRunner.hpp",
        "Core/Platform/ProcessRunner.cpp",
        "Core/Presentation/EditorUiState.hpp",
        "Core/Presentation/EditorLayer.hpp",
        "Core/Presentation/EditorLayer.cpp",
        "Core/Presentation/ImGuiLayer.hpp",
        "Core/Presentation/ImGuiLayer.cpp",
        "Core/Presentation/Panels/**.hpp",
        "Core/Presentation/Panels/**.cpp",
        "Core/ProgressTrackingSystem/**.hpp",
        "Core/ProgressTrackingSystem/**.cpp",
        "Core/ScientificRuntime/PlotExport/**.hpp",
        "Core/ScientificRuntime/PlotExport/**.cpp",
        "Core/ScientificRuntime/ScientificRuntimeLayer.hpp",
        "Core/ScientificRuntime/ScientificRuntimeLayer.cpp",
        "Core/ScientificRuntime/Python/PythonApi.hpp",
        "Core/ScientificRuntime/Python/PythonBridgeBuildConfig.hpp",
        "Core/ScientificRuntime/Python/PythonErrors.hpp",
        "Core/ScientificRuntime/Python/PythonErrors.cpp",
        "Core/ScientificRuntime/Python/PythonGilScope.hpp",
        "Core/ScientificRuntime/Python/PythonGilScope.cpp",
        "Core/ScientificRuntime/Python/PythonInterpreter.hpp",
        "Core/ScientificRuntime/Python/PythonInterpreter.cpp",
        "Core/ScientificRuntime/Python/PythonScriptJob.hpp",
        "Core/ScientificRuntime/Python/PythonScriptJob.cpp",
        "Core/ScientificRuntime/Python/ScientificPythonRuntime.hpp",
        "Core/ScientificRuntime/Python/ScientificPythonRuntime.cpp",
        "Core/ScientificRuntime/Python/ScriptRunner.hpp",
        "Core/ScientificRuntime/Python/ScriptRunner.cpp",
        "Core/Threading/**.hpp",
        "Core/Threading/**.cpp",
        "Core/Types/**.hpp",
        "Core/Undo/**.hpp",
        "Core/Undo/**.cpp",
        "Core/Utils/**.hpp",
        "Core/Utils/**.cpp"
    }

    includedirs {
        ".",
        "Core",
        "Vendor/spdlog/include",
        "Vendor/Tracy/public",
        "Vendor/GLFW/include",
        "Vendor/GLAD/generated/include",
        "Vendor/ImGui",
        "Vendor/ImGui/backends",
        "Vendor/implot",
        "Vendor/implot3d",
        "Vendor/imgui-command-palette",
        "Vendor/ImGuiNotify/win32Example/backends",
        "Vendor/ImGuiNotify/win32Example/fonts",
        "Vendor/nativefiledialog-extended/src/include",
        "Vendor/thread-pool/include",
        "Vendor/json/include",
        "Vendor/yaml-cpp/include"
    }

    defines { 'SST_PLOT_EXPORT_PYTHON_ROOT="' .. _SST_ROOT .. '/Core/ScientificRuntime/Python"' }
    if _SST_PYTHON_EMBED_AVAILABLE == true then
        includedirs { _SST_PYTHON_INCLUDE_DIR, "Vendor/nanobind/include" }
        defines { "DS_PYTHON_CAPI_AVAILABLE=1", "Py_NO_LINK_LIB", "NB_STATIC" }
    else
        defines { "DS_PYTHON_CAPI_AVAILABLE=0" }
    end

    defines {
        "GLFW_INCLUDE_NONE",
        "IMGUI_IMPL_OPENGL_LOADER_GLAD",
        "YAML_CPP_STATIC_DEFINE"
    }

    links {
        "GLFW",
        "GLAD",
        "ImGui",
        "ImPlot",
        "ImPlot3D",
        "nfd",
        "yaml-cpp"
    }

    filter "system:windows"
        files {
            "Core/Platform/Windows/**.hpp",
            "Core/Platform/Windows/**.cpp"
        }
        systemversion "latest"
        defines {
            "DS_PLATFORM_WINDOWS",
            "TRACY_ENABLE",
            "TRACY_ONLY_LOCALHOST" -- loopback only: no Windows Firewall prompt per new exe path
        }
        links { "opengl32", "dwmapi", "gdi32", "user32", "shell32", "Tracy" }

    filter { "system:windows", "action:vs2022" }
        buildoptions { "/utf-8" }

    filter "system:linux"
        files {
            "Core/Platform/Linux/**.hpp",
            "Core/Platform/Linux/**.cpp"
        }
        pic "On"
        defines { "DS_PLATFORM_LINUX" }
        links { "GL", "dl", "pthread", "X11", "Xrandr", "Xi", "Xxf86vm", "Xinerama", "Xcursor" }

    filter "system:macosx"
        files {
            "Core/Platform/MacOS/**.hpp",
            "Core/Platform/MacOS/**.cpp"
        }
        defines { "DS_PLATFORM_MACOS" }

    filter "configurations:Debug"
        defines { "DS_DEBUG" }
        runtime "Debug"
        symbols "On"

    filter "configurations:Release"
        defines { "DS_RELEASE" }
        runtime "Release"
        optimize "On"

    filter "configurations:Dist"
        defines { "DS_DIST" }
        runtime "Release"
        optimize "Full"

    filter {}


include (_SST_ROOT .. "/../SST-Libraries/sst_libraries.lua")
project "Core"
    removefiles { "Core/IO/Hdf5/tests/**" }
    sst_link_hdf5()
    filter "files:Core/IO/Hdf5/**.cpp"
        flags { "NoPCH" }
    filter {}
    sst_link_ui()
    sst_link_plotlab()

    group "Prototypes"
local selectedPrototypes = os.getenv("SST_BUILD_PROTOTYPES")
for _, projectScript in ipairs(os.matchfiles("Prototypes/*/premake5.lua")) do
    local name = path.getname(path.getdirectory(projectScript))
    if not selectedPrototypes or ("," .. selectedPrototypes .. ","):find("," .. name .. ",", 1, true) then
        include(projectScript)
    end
end
group ""

-- Core's HDF5 adapter needs the runtime beside each host executable.
workspace "SSTPrototypes"
    filter "kind:ConsoleApp or WindowedApp"
        sst_link_hdf5()
        if _SST_PYTHON_EMBED_AVAILABLE then
            libdirs { _SST_PYTHON_LIB_DIR }
            links { "nanobind", _SST_PYTHON_LIB_NAME }
            if _SST_PYTHON_DLL_PATH ~= "" then
                postbuildcommands { '{COPYFILE} "' .. _SST_PYTHON_DLL_PATH .. '" "%{cfg.targetdir}"' }
            end
        end
    filter {}


