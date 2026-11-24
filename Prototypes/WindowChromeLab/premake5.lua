project "WindowChromeLab"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }

    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_SST_ROOT .. "/Prototypes/WindowChromeLab")

    files {
        _SST_ROOT .. "/Prototypes/WindowChromeLab/src/**.hpp",
        _SST_ROOT .. "/Prototypes/WindowChromeLab/src/**.cpp"
    }

    includedirs {
        _SST_ROOT,
        _SST_ROOT .. "/Core",
        _SST_ROOT .. "/Prototypes/WindowChromeLab/src",
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
        '{COPYDIR} "' .. _SST_ROOT .. '/Prototypes/WindowChromeLab/install" "%{cfg.targetdir}/install"'
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

project "WindowChromeLabTests"
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
        _SST_ROOT .. "/Prototypes/WindowChromeLab/tests/**.hpp",
        _SST_ROOT .. "/Prototypes/WindowChromeLab/tests/**.cpp",
        _SST_ROOT .. "/Prototypes/WindowChromeLab/src/Presentation/LabWindows.cpp"
    }

    includedirs {
        _SST_ROOT,
        _SST_ROOT .. "/Core",
        _SST_ROOT .. "/Prototypes/WindowChromeLab/src",
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
for _, target in ipairs { "WindowChromeLab", "WindowChromeLabTests" } do
    project(target)
    sst_link_ui()
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
project "WindowChromeLabTests"
    files {
        _SST_ROOT .. "/Prototypes/WindowChromeLab/src/IO/PrototypeWorkflow.cpp",
        _SST_ROOT .. "/Prototypes/WindowChromeLab/src/IO/PrototypePythonBindings.cpp"
    }
    defines { 'SST_PROTOTYPE_PYTHON_ROOT="' .. _SST_ROOT .. '/Prototypes/WindowChromeLab/python"' }
project "WindowChromeLab"
    postbuildcommands { '{COPYDIR} "' .. _SST_ROOT .. '/Prototypes/WindowChromeLab/python" "%{cfg.targetdir}/python"' }
