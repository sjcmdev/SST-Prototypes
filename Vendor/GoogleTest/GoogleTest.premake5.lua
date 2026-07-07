project "GoogleTest"
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"

    targetdir ("../build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../build/bin-int/" .. outputdir .. "/%{prj.name}")

    files { "googletest/src/gtest-all.cc" }

    includedirs {
        "googletest",
        "googletest/include"
    }

    filter "system:linux"
        pic "On"

    filter {}

project "GoogleTestMain"
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"

    targetdir ("../build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../build/bin-int/" .. outputdir .. "/%{prj.name}")

    files { "googletest/src/gtest_main.cc" }

    includedirs {
        "googletest",
        "googletest/include"
    }

    filter "system:linux"
        pic "On"

    filter {}
