project "CapLab"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }

    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_SST_ROOT .. "/Prototypes/CapLab")

    files {
        _SST_ROOT .. "/Prototypes/CapLab/src/**.hpp",
        _SST_ROOT .. "/Prototypes/CapLab/src/**.cpp"
    }

    includedirs {
        _SST_ROOT,
        _SST_ROOT .. "/Core",
        _SST_ROOT .. "/Prototypes/CapLab/src",
        _SST_ROOT .. "/Vendor/ImGui",
        _SST_ROOT .. "/Vendor/implot",
        _SST_ROOT .. "/Vendor/ImGui/backends",
        _SST_ROOT .. "/Vendor/GLFW/include",
        _SST_ROOT .. "/Vendor/GLAD/generated/include",
        _SST_ROOT .. "/Vendor/spdlog/include",
        _SST_ROOT .. "/Vendor/thread-pool/include",
        _SST_ROOT .. "/Vendor/json/include",
        _SST_ROOT .. "/Vendor/yaml-cpp/include"
    }

    links {
        "Core"
    }

    postbuildcommands {
        '{COPYDIR} "' .. _SST_ROOT .. '/Prototypes/CapLab/install" "%{cfg.targetdir}/install"'
    }

    filter { "system:windows", "action:vs2022" }
        kind "WindowedApp"
        entrypoint "mainCRTStartup"
        systemversion "latest"
        buildoptions { "/utf-8" }

    filter "system:windows"
        defines { "DS_PLATFORM_WINDOWS" }

    filter "system:linux"
        defines { "DS_PLATFORM_LINUX" }

    filter "system:macosx"
        defines { "DS_PLATFORM_MACOS" }

    filter "configurations:Debug"
        defines { "DS_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "DS_RELEASE" }
        optimize "On"

    filter "configurations:Dist"
        defines { "DS_DIST" }
        optimize "Full"

    filter {}

project "CapLabTests"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }

    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        _SST_ROOT .. "/Prototypes/CapLab/tests/**.hpp",
        _SST_ROOT .. "/Prototypes/CapLab/tests/**.cpp"
    }

    includedirs {
        _SST_ROOT,
        _SST_ROOT .. "/Core",
        _SST_ROOT .. "/Prototypes/CapLab/src",
        _SST_ROOT .. "/Vendor/GoogleTest/googletest/include",
        _SST_ROOT .. "/Vendor/spdlog/include",
        _SST_ROOT .. "/Vendor/thread-pool/include",
        _SST_ROOT .. "/Vendor/json/include",
        _SST_ROOT .. "/Vendor/yaml-cpp/include"
    }

    links {
        "Core",
        "GoogleTest",
        "GoogleTestMain"
    }

    filter { "system:windows", "action:vs2022" }
        systemversion "latest"
        buildoptions { "/utf-8" }

    filter "system:windows"
        defines { "DS_PLATFORM_WINDOWS" }

    filter "system:linux"
        defines { "DS_PLATFORM_LINUX" }

    filter "system:macosx"
        defines { "DS_PLATFORM_MACOS" }

    filter "configurations:Debug"
        defines { "DS_DEBUG" }
        symbols "On"

    filter "configurations:Release"
        defines { "DS_RELEASE" }
        optimize "On"

    filter "configurations:Dist"
        defines { "DS_DIST" }
        optimize "Full"

    filter {}


-- Keep app/tests on the same embedding and CRT contract as Core.
for _, target in ipairs { "CapLab", "CapLabTests" } do
    project(target)
    sst_link_libraries()
    defines { 'SST_CAPLAB_MATERIALS_JSON="' .. _SST_ROOT .. '/../SST-Libraries/SST-Physics/data/caplab-materials.json"' }
    for _, name in ipairs { "caplab-materials.json", "caplab-propho-compat.json" } do
        postbuildcommands { '{COPYFILE} "' .. _SST_ROOT .. '/../SST-Libraries/SST-Physics/data/' .. name .. '" "%{cfg.targetdir}/' .. name .. '"' }
    end
    sst_link_plotlab()
    defines { 'SST_PLOT_EXPORT_PYTHON_ROOT="' .. _SST_ROOT .. '/Core/ScientificRuntime/Python"' }
    defines { 'SST_PROTOTYPE_PYTHON_ROOT="' .. _SST_ROOT .. '/Prototypes/CapLab/python"' }
    filter "configurations:Debug"
        runtime "Debug"
    filter "configurations:Release or Dist"
        runtime "Release"
    filter {}
    if _SST_PYTHON_EMBED_AVAILABLE then
        includedirs { _SST_PYTHON_INCLUDE_DIR, _SST_ROOT .. "/Vendor/nanobind/include" }
        libdirs { _SST_PYTHON_LIB_DIR }
        defines { "DS_PYTHON_CAPI_AVAILABLE=1", "Py_NO_LINK_LIB", "NB_STATIC" }
        links { "nanobind", _SST_PYTHON_LIB_NAME }
        filter "system:windows"
            if _SST_PYTHON_DLL_PATH ~= "" then
                postbuildcommands { '{COPYFILE} "' .. _SST_PYTHON_DLL_PATH .. '" "%{cfg.targetdir}"' }
            end
        filter {}
    else
        defines { "DS_PYTHON_CAPI_AVAILABLE=0" }
    end
end
project "CapLabTests"
    files {
        _SST_ROOT .. "/Prototypes/CapLab/src/CapLabCommands.cpp",
        _SST_ROOT .. "/Prototypes/CapLab/src/Presentation/CapLabSchematicEditor.cpp",
        _SST_ROOT .. "/Prototypes/CapLab/src/Presentation/CapLabPrototypePanel.cpp",
        _SST_ROOT .. "/Prototypes/CapLab/src/Presentation/CapLabISPanel.cpp",
        _SST_ROOT .. "/Prototypes/CapLab/src/IO/PrototypeWorkflow.cpp",
        _SST_ROOT .. "/Prototypes/CapLab/src/IO/PrototypePythonBindings.cpp"
    }
    defines { 'SST_PROTOTYPE_PYTHON_ROOT="' .. _SST_ROOT .. '/Prototypes/CapLab/python"' }
project "CapLab"
    postbuildcommands { '{COPYDIR} "' .. _SST_ROOT .. '/Prototypes/CapLab/python" "%{cfg.targetdir}/python"' }

project "CapLab"
    postbuildcommands { '{COPYDIR} "' .. _SST_ROOT .. '/Core/ScientificRuntime/Python/sst_plot_export" "%{cfg.targetdir}/python/sst_plot_export"' }

-- Embed only the reusable CircuitCore-UI model/widget/runner, without its host.
local circuitRoot = os.getenv("SST_CIRCUITCORE_ROOT") or (_SST_ROOT .. "/../CircuitCore")
local circuitUiRoot = os.getenv("SST_CIRCUIT_UI_ROOT") or (_SST_ROOT .. "/../SST-Experiments/CircuitCore-UI/src")
if not os.isdir(circuitRoot) then circuitRoot = _SST_ROOT .. "/../../../CircuitCore" end
if not os.isdir(circuitUiRoot) then circuitUiRoot = _SST_ROOT .. "/../../../SST-Experiments/CircuitCore-UI/src" end
for _, target in ipairs { "CapLab", "CapLabTests" } do
    project(target)
    includedirs { circuitRoot .. "/include", circuitRoot .. "/src", circuitUiRoot }
    files { circuitRoot .. "/src/**.cpp",
        circuitUiRoot .. "/CircuitDocument.cpp",
        circuitUiRoot .. "/AnalysisRunner.cpp",
        _SST_ROOT .. "/Prototypes/CapLab/src/Domain/CapLabIS.cpp" }
end
project "CapLabTests"
    includedirs { _SST_ROOT .. "/Vendor/ImGui", _SST_ROOT .. "/Vendor/implot" }
