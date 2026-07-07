project "JFM"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }
    defines { "YAML_CPP_STATIC_DEFINE" }

    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir (_SST_ROOT .. "/Prototypes/JFM")

    files {
        _SST_ROOT .. "/Prototypes/JFM/src/**.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/**.cpp"
    }

    includedirs {
        _SST_ROOT,
        _SST_ROOT .. "/Core",
        _SST_ROOT .. "/Prototypes/JFM/src",
        _SST_ROOT .. "/Vendor/ImGui",
        _SST_ROOT .. "/Vendor/ImGui/backends",
        _SST_ROOT .. "/Vendor/implot",
        _SST_ROOT .. "/Vendor/GLFW/include",
        _SST_ROOT .. "/Vendor/GLAD/generated/include",
        _SST_ROOT .. "/Vendor/nativefiledialog-extended/src/include",
        _SST_ROOT .. "/Vendor/nanobind/include",
        _SST_ROOT .. "/Vendor/nanobind/ext/robin_map/include",
        _SST_ROOT .. "/Vendor/spdlog/include",
        _SST_ROOT .. "/Vendor/thread-pool/include",
        _SST_ROOT .. "/Vendor/json/include",
        _SST_ROOT .. "/Vendor/yaml-cpp/include"
    }

    links {
        "Core",
        "ImPlot",
        "nfd",
        "yaml-cpp"
    }

    if _SST_PYTHON_EMBED_AVAILABLE == true then
        includedirs { _SST_PYTHON_INCLUDE_DIR }
        libdirs { _SST_PYTHON_LIB_DIR }
        links { "nanobind", _SST_PYTHON_LIB_NAME }
        defines { "DS_PYTHON_CAPI_AVAILABLE=1", "NB_STATIC", "Py_NO_LINK_LIB" }
        linkoptions { "/NODEFAULTLIB:python313_d.lib", "/NODEFAULTLIB:python313t_d.lib" }
        if _SST_PYTHON_DLL_PATH ~= "" then
            postbuildcommands {
                '{COPYFILE} "' .. _SST_PYTHON_DLL_PATH .. '" "%{cfg.targetdir}"'
            }
        end
    else
        defines { "DS_PYTHON_CAPI_AVAILABLE=0" }
    end

    postbuildcommands {
        '{COPYDIR} "' .. _SST_ROOT .. '/Prototypes/JFM/install" "%{cfg.targetdir}/install"',
        '{COPYDIR} "' .. _SST_ROOT .. '/Prototypes/JFM/python" "%{cfg.targetdir}/python"'
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

project "JFMTests"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    warnings "Extra"
    flags { "MultiProcessorCompile" }
    defines { "YAML_CPP_STATIC_DEFINE" }

    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        _SST_ROOT .. "/Prototypes/JFM/src/Domain/JFMTypes.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Storage/JFMWorkspaceStore.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Storage/JFMWorkspaceStore.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectYamlSerializer.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectYamlSerializer.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectJobs.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectJobs.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportPaths.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportPaths.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonImportBridge.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonImportBridge.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportJob.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportJob.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Events/JFMEvents.hpp",
        _SST_ROOT .. "/Prototypes/JFM/tests/**.hpp",
        _SST_ROOT .. "/Prototypes/JFM/tests/**.cpp"
    }

    includedirs {
        _SST_ROOT,
        _SST_ROOT .. "/Core",
        _SST_ROOT .. "/Prototypes/JFM/src",
        _SST_ROOT .. "/Vendor/GoogleTest/googletest/include",
        _SST_ROOT .. "/Vendor/nanobind/include",
        _SST_ROOT .. "/Vendor/nanobind/ext/robin_map/include",
        _SST_ROOT .. "/Vendor/spdlog/include",
        _SST_ROOT .. "/Vendor/thread-pool/include",
        _SST_ROOT .. "/Vendor/json/include",
        _SST_ROOT .. "/Vendor/yaml-cpp/include"
    }

    links {
        "Core",
        "yaml-cpp",
        "GoogleTest",
        "GoogleTestMain"
    }

    if _SST_PYTHON_EMBED_AVAILABLE == true then
        includedirs { _SST_PYTHON_INCLUDE_DIR }
        libdirs { _SST_PYTHON_LIB_DIR }
        links { "nanobind", _SST_PYTHON_LIB_NAME }
        defines { "DS_PYTHON_CAPI_AVAILABLE=1", "NB_STATIC", "Py_NO_LINK_LIB" }
        linkoptions { "/NODEFAULTLIB:python313_d.lib", "/NODEFAULTLIB:python313t_d.lib" }
        if _SST_PYTHON_DLL_PATH ~= "" then
            postbuildcommands {
                '{COPYFILE} "' .. _SST_PYTHON_DLL_PATH .. '" "%{cfg.targetdir}"'
            }
        end
    else
        defines { "DS_PYTHON_CAPI_AVAILABLE=0" }
    end

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
