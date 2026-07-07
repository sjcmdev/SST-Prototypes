project "GLAD"
    kind "StaticLib"
    language "C"
    staticruntime "off"

    targetdir ("../build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "generated/include/**.h",
        "generated/src/gl.c"
    }

    includedirs {
        "generated/include"
    }

    filter "system:linux"
        pic "On"

    filter {}