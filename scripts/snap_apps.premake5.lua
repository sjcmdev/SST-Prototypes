-- Optional gallery targets; normal product project definitions stay untouched.
local root = path.getabsolute(path.join(_SCRIPT_DIR, ".."))
os.chdir(root)
dofile(path.join(root, "premake5.lua"))

-- Materialized by snap_apps.py from the current template, under the build directory.
local file = assert(io.open(path.join(root, "PrototypeTemplate/premake5.lua"), "r"))
local template = file:read("*a")
file:close()
template = template:gsub("^\239\187\191", "")
template = template:gsub("/Prototypes/__PROTOTYPE_NAME__", "/build/gallery-template")
template = template:gsub("__PROTOTYPE_NAME__", "GalleryTemplate")
assert(load(template))()

-- Smoke predates the shared embedded-Python link contract used by other apps.
if _SST_PYTHON_EMBED_AVAILABLE then
    project "Smoke"
        libdirs { _SST_PYTHON_LIB_DIR }
        links { "nanobind", _SST_PYTHON_LIB_NAME }
end

project "CoreScreenshotTests"
    location (root .. "/build/generated/%{_ACTION}/%{prj.name}")
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"
    targetdir (root .. "/build/bin/" .. outputdir .. "/%{prj.name}")
    objdir (root .. "/build/bin-int/" .. outputdir .. "/%{prj.name}")
    files { root .. "/scripts/tests/ApplicationScreenshotTests.cpp" }
    includedirs { root, root .. "/Core", root .. "/Vendor/GoogleTest/googletest/include",
        root .. "/Vendor/spdlog/include", root .. "/Vendor/thread-pool/include", root .. "/Vendor/json/include",
        root .. "/Vendor/yaml-cpp/include", root .. "/Vendor/GLAD/generated/include", root .. "/Vendor/GLFW/include" }
    links { "Core", "GoogleTest", "GoogleTestMain" }
    sst_link_ui()
    filter "system:windows"
        defines { "DS_PLATFORM_WINDOWS" }
        buildoptions { "/utf-8" }
    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "DS_DEBUG" }
    filter "configurations:Release or Dist"
        runtime "Release"
        optimize "On"
    filter {}
    if _SST_PYTHON_EMBED_AVAILABLE then
        includedirs { _SST_PYTHON_INCLUDE_DIR, root .. "/Vendor/nanobind/include" }
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
