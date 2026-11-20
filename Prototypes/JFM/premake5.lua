include (_SST_ROOT .. "/../SST-Libraries/sst_libraries.lua")

project "JFM"
    local jfmCudaPath = os.getenv("CUDA_PATH")
    local jfmCudaAvailable = _ACTION == "vs2022"
        and jfmCudaPath ~= nil
        and os.isfile(path.join(jfmCudaPath, "bin/nvcc.exe"))
    local jfmSolverRoot = _SST_ROOT .. "/Prototypes/JFM/src/Analysis/Solver"

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
        jfmSolverRoot,
        jfmSolverRoot .. "/gpu",
        jfmSolverRoot .. "/lambertw-gpu",
        _SST_ROOT .. "/Vendor/ImGui",
        _SST_ROOT .. "/Vendor/ImGui/backends",
        _SST_ROOT .. "/Vendor/implot",
        _SST_ROOT .. "/Vendor/GLFW/include",
        _SST_ROOT .. "/Vendor/GLAD/generated/include",
        _SST_ROOT .. "/Vendor/nanobind/include",
        _SST_ROOT .. "/Vendor/nanobind/ext/robin_map/include",
        _SST_ROOT .. "/Vendor/spdlog/include",
        _SST_ROOT .. "/Vendor/thread-pool/include",
        _SST_ROOT .. "/Vendor/json/include",
        _SST_ROOT .. "/Vendor/yaml-cpp/include"
    }

    if jfmCudaAvailable then
        files {
            jfmSolverRoot .. "/gpu/diode_model_device.cuh",
            jfmSolverRoot .. "/gpu/gpu_backend.cu",
            jfmSolverRoot .. "/lambertw-gpu/lambert_w_device.cuh"
        }
        includedirs { jfmCudaPath .. "/include" }
        libdirs { jfmCudaPath .. "/lib/x64" }
        links { "cudart" }
        defines { "JFM_CUDA_AVAILABLE=1" }
    else
        defines { "JFM_CUDA_AVAILABLE=0" }
    end

    sst_link_libraries()

    links {
        "Core",
        "ImPlot",
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

    if jfmCudaAvailable then
        filter { "files:**.cu" }
            buildmessage "NVCC: %{file.relpath}"
            buildoutputs { "$(IntDir)%{file.basename}.obj" }

        filter { "files:**.cu", "configurations:Debug" }
            buildcommands {
                '"$(CUDA_PATH)/bin/nvcc" -c -G -g -O0 --fmad=false -allow-unsupported-compiler -std=c++17 -Xcompiler "/MDd /Zi /FS /fp:strict /utf-8"'
                .. ' -I"$(CUDA_PATH)/include"'
                .. ' -I"' .. _SST_ROOT .. '/../SST-Libraries/SST-Math/include"'
                .. ' -I"' .. _SST_ROOT .. '/../SST-Libraries/SST-Physics/include"'
                .. ' -I"' .. jfmSolverRoot .. '"'
                .. ' -I"' .. jfmSolverRoot .. '/gpu"'
                .. ' -I"' .. jfmSolverRoot .. '/lambertw-gpu"'
                .. ' -o "$(IntDir)%{file.basename}.obj" "%{file.abspath}"'
            }

        filter { "files:**.cu", "configurations:Release or Dist" }
            buildcommands {
                '"$(CUDA_PATH)/bin/nvcc" -c -O2 --fmad=false -allow-unsupported-compiler -std=c++17 -Xcompiler "/MD /FS /fp:strict /utf-8" -DNDEBUG'
                .. ' -I"$(CUDA_PATH)/include"'
                .. ' -I"' .. _SST_ROOT .. '/../SST-Libraries/SST-Math/include"'
                .. ' -I"' .. _SST_ROOT .. '/../SST-Libraries/SST-Physics/include"'
                .. ' -I"' .. jfmSolverRoot .. '"'
                .. ' -I"' .. jfmSolverRoot .. '/gpu"'
                .. ' -I"' .. jfmSolverRoot .. '/lambertw-gpu"'
                .. ' -o "$(IntDir)%{file.basename}.obj" "%{file.abspath}"'
            }
    end

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
        _SST_ROOT .. "/Prototypes/JFM/src/Analysis/JFMFitEngine.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Analysis/JFMFitEngine.cpp",
		_SST_ROOT .. "/Prototypes/JFM/src/Analysis/JFMMonteCarloStatistics.hpp",
		_SST_ROOT .. "/Prototypes/JFM/src/Analysis/JFMMonteCarloStatistics.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/App/JFMUserSettings.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/App/JFMUserSettings.cpp",
		_SST_ROOT .. "/Prototypes/JFM/src/App/JFMProjectEditCommand.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Workspace/JFMWorkspaceStore.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Workspace/JFMWorkspaceStore.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectYamlSerializer.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectYamlSerializer.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectBundleSerializer.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectJobs.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMProjectJobs.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMIORegistration.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMFitJobs.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportPaths.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportPaths.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonImportBridge.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonImportBridge.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonRuntime.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonRuntime.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonPrefitBridge.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonPrefitBridge.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonBindings.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPythonBindings.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportJob.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportJob.cpp",
		_SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportPreviewJob.hpp",
		_SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportPreviewJob.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPrefitJob.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPrefitJob.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportSources.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMImportSources.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMExportRawTsvJob.hpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMExportRawTsvJob.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Events/JFMEvents.hpp",
        jfmSolverRoot .. "/solver/objective.hpp",
        jfmSolverRoot .. "/solver/objective.cpp",
		jfmSolverRoot .. "/solver/diode_model.hpp",
		jfmSolverRoot .. "/solver/diode_objective.hpp",
		jfmSolverRoot .. "/solver/diode_objective.cpp",
        jfmSolverRoot .. "/gpu/gpu_types.hpp",
        jfmSolverRoot .. "/gpu/gpu_backend.hpp",
        jfmSolverRoot .. "/gpu/gpu_backend_stub.cpp",
        _SST_ROOT .. "/Prototypes/JFM/tests/**.hpp",
        _SST_ROOT .. "/Prototypes/JFM/tests/**.cpp"
    }

    includedirs {
        _SST_ROOT,
        _SST_ROOT .. "/Core",
        _SST_ROOT .. "/Prototypes/JFM/src",
        jfmSolverRoot,
        jfmSolverRoot .. "/gpu",
        _SST_ROOT .. "/Vendor/GoogleTest/googletest/include",
        _SST_ROOT .. "/Vendor/nativefiledialog-extended/src/include",
        _SST_ROOT .. "/Vendor/nanobind/include",
        _SST_ROOT .. "/Vendor/nanobind/ext/robin_map/include",
        _SST_ROOT .. "/Vendor/spdlog/include",
        _SST_ROOT .. "/Vendor/thread-pool/include",
        _SST_ROOT .. "/Vendor/json/include",
        _SST_ROOT .. "/Vendor/yaml-cpp/include"
    }

    defines { "JFM_CUDA_AVAILABLE=0" }

    sst_link_libraries()

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

for _, target in ipairs { "JFM", "JFMTests" } do
    project(target)
    sst_link_plotlab()
end
project "JFMTests"
    files {
        _SST_ROOT .. "/Prototypes/JFM/src/Presentation/JFMPlotDocument.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/Presentation/Panels/MonteCarloCurvesPanel.cpp",
        _SST_ROOT .. "/Prototypes/JFM/src/IO/JFMPlotExport.cpp"
    }
project "JFM"
    postbuildcommands {
        '{COPYDIR} "' .. _SST_ROOT .. '/Core/ScientificRuntime/Python/sst_plot_export" "%{cfg.targetdir}/python/sst_plot_export"',
        '{COPYDIR} "' .. _SST_ROOT .. '/Prototypes/JFM/install/app/assets/fonts" "%{cfg.targetdir}/install/app/assets/fonts"'
    }

for _, target in ipairs { "JFM", "JFMTests" } do
    project(target)
    filter { "system:windows" }
        floatingpoint "Strict"
    filter {}
end
