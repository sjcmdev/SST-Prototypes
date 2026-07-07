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

_SST_PYTHON_EMBED_AVAILABLE = false
_DS_PYTHON_EMBED_AVAILABLE = _SST_PYTHON_EMBED_AVAILABLE

group "Dependencies"
include "Vendor/GLFW"
include "Vendor/GLAD"
include "Vendor/ImGui"
include "Vendor/thread-pool"
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
        "Core/Presentation/EditorUiState.hpp",
        "Core/Presentation/EditorLayer.hpp",
        "Core/Presentation/EditorLayer.cpp",
        "Core/Presentation/ImGuiLayer.hpp",
        "Core/Presentation/ImGuiLayer.cpp",
        "Core/Presentation/Panels/**.hpp",
        "Core/Presentation/Panels/**.cpp",
        "Core/ProgressTrackingSystem/**.hpp",
        "Core/ProgressTrackingSystem/**.cpp",
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
        "Vendor/imgui-command-palette",
        "Vendor/ImGuiNotify/win32Example/backends",
        "Vendor/ImGuiNotify/win32Example/fonts",
        "Vendor/nativefiledialog-extended/src/include",
        "Vendor/thread-pool/include",
        "Vendor/json/include",
        "Vendor/yaml-cpp/include"
    }

    defines { "DS_PYTHON_CAPI_AVAILABLE=0" }

    defines {
        "GLFW_INCLUDE_NONE",
        "IMGUI_IMPL_OPENGL_LOADER_GLAD",
        "YAML_CPP_STATIC_DEFINE"
    }

    links {
        "GLFW",
        "GLAD",
        "ImGui",
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
            "TRACY_ENABLE"
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


    group "Prototypes"
for _, projectScript in ipairs(os.matchfiles("Prototypes/*/premake5.lua")) do
    include(projectScript)
end
group ""


