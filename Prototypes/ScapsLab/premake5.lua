newoption { trigger = "scaps-core", value = "PATH", description = "Read-only scaps_core checkout (default ../scaps_core)" }
local scaps = path.getabsolute(_OPTIONS["scaps-core"] or "../scaps_core", _SST_ROOT)
local root = _SST_ROOT .. "/Prototypes/ScapsLab"
local solverStamp = os.outputof('powershell -NoProfile -Command "$files=Get-ChildItem -Path \'' .. scaps .. '/src\',\'' .. scaps .. '/include\' -Recurse -File | Sort-Object FullName; $joined=($files | Get-FileHash -Algorithm SHA256 | ForEach-Object Hash) -join \'\'; [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($joined))).Replace(\'-\',\'\')"')
if not solverStamp or not solverStamp:match("^%x+") then error("Cannot identify scaps_core source version") end
solverStamp = solverStamp:sub(1,16)

project "ScapsCore"
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")
    files { scaps .. "/src/**.cpp", scaps .. "/src/**.hpp", scaps .. "/include/**.hpp" }
    removefiles { scaps .. "/src/*_python.cpp" }
    includedirs { scaps .. "/include", _SST_ROOT .. "/Vendor/yaml-cpp/include" }
    defines { "YAML_CPP_STATIC_DEFINE" }
    links { "yaml-cpp" }
    filter "system:windows"
        buildoptions { "/fp:precise", "/utf-8", "/permissive-" }
    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
    filter "configurations:Release or Dist"
        runtime "Release"
        optimize "On"
    filter {}
for _, name in ipairs { "ScapsLab", "ScapsLabTests", "ScapsLabHeadless" } do
    project(name)
    location (_SST_ROOT .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    targetdir (_SST_ROOT .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (_SST_ROOT .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")
    includedirs { _SST_ROOT, _SST_ROOT .. "/Core", root .. "/src", scaps .. "/include",
        _SST_ROOT .. "/Vendor/ImGui", _SST_ROOT .. "/Vendor/implot", _SST_ROOT .. "/Vendor/GLFW/include",
        _SST_ROOT .. "/Vendor/GLAD/generated/include", _SST_ROOT .. "/Vendor/spdlog/include",
        _SST_ROOT .. "/Vendor/thread-pool/include", _SST_ROOT .. "/Vendor/json/include",
        _SST_ROOT .. "/Vendor/yaml-cpp/include", _SST_ROOT .. "/Vendor/GoogleTest/googletest/include" }
    links { "Core", "ScapsCore" }
    if name ~= "ScapsLabHeadless" then
        sst_link_ui(); sst_link_plotlab()
        files { _SST_ROOT .. "/Prototypes/PlotLab/src/PlotLabExport.cpp" }
        defines { 'SCAPSLAB_PLOTLAB_PYTHON="' .. _SST_ROOT .. '/Prototypes/PlotLab/python"' }
    end
    defines { 'SCAPSLAB_HEADLESS="' .. _SST_ROOT .. '/build/bin/' .. outputdir .. '/ScapsLabHeadless/ScapsLabHeadless.exe"', "YAML_CPP_STATIC_DEFINE", 'SCAPSLAB_TUTORIAL_ARTIFACTS="' .. _SST_ROOT .. '/build/tutorial-artifacts"', 'SCAPSLAB_TUTORIALS="' .. root .. '/tutorials"', 'SCAPSLAB_EXAMPLES="' .. root .. '/examples"', 'SCAPSLAB_SOLVER_VERSION="scaps_core-ui-' .. solverStamp .. '"', 'SCAPSLAB_PROJECT="' .. scaps .. '/device_project.yaml"', 'SCAPSLAB_PYTHON="' .. root .. '/python"', 'SCAPSLAB_PYTHON_TEST="' .. root .. '/tests/test_headless.py"' }
    if name == "ScapsLab" then
        files { root .. "/src/**.cpp", root .. "/src/**.hpp" }
        removefiles { root .. "/src/Headless.cpp" }
        debugdir ("%{cfg.targetdir}")
        postbuildcommands { '{COPYDIR} "' .. _SST_ROOT .. '/PrototypeTemplate/install" "%{cfg.targetdir}/install"', '{COPYDIR} "' .. root .. '/install" "%{cfg.targetdir}/install"', '{COPYDIR} "' .. root .. '/python" "%{cfg.targetdir}/python"', '{COPYDIR} "' .. root .. '/examples" "%{cfg.targetdir}/examples"', '{COPYDIR} "' .. root .. '/tutorials" "%{cfg.targetdir}/tutorials"' }
    elseif name == "ScapsLabTests" then
        files { root .. "/tests/**.cpp", root .. "/src/Workflow.cpp", root .. "/src/Workspace.cpp", root .. "/src/Python.cpp", root .. "/src/Panel.cpp", root .. "/src/Batch.cpp", root .. "/src/ResultsIO.cpp", root .. "/src/Tutorials.cpp" }
        links { "GoogleTest", "GoogleTestMain" }
    else
        files { scaps .. "/apps/scaps_headless.cpp" }
    end
    if _SST_PYTHON_EMBED_AVAILABLE then
        includedirs { _SST_PYTHON_INCLUDE_DIR, _SST_ROOT .. "/Vendor/nanobind/include" }
        libdirs { _SST_PYTHON_LIB_DIR }
        defines { "DS_PYTHON_CAPI_AVAILABLE=1", "Py_NO_LINK_LIB", "NB_STATIC" }
        links { "nanobind", _SST_PYTHON_LIB_NAME }
        if _SST_PYTHON_DLL_PATH ~= "" then
            postbuildcommands { '{COPYFILE} "' .. _SST_PYTHON_DLL_PATH .. '" "%{cfg.targetdir}"' }
        end
    else
        defines { "DS_PYTHON_CAPI_AVAILABLE=0" }
    end
    filter "system:windows"
        defines { "DS_PLATFORM_WINDOWS" }
        systemversion "latest"
        buildoptions { "/utf-8", "/fp:precise" }
    filter "configurations:Debug"
        defines { "DS_DEBUG" }
        runtime "Debug"
        symbols "On"
    filter "configurations:Release or Dist"
        defines { "DS_RELEASE" }
        runtime "Release"
        optimize "On"
    filter {}
end


