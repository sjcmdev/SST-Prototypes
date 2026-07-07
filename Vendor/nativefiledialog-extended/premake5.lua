project "nfd"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"

    targetdir ("../build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "src/include/**.h",
        "src/include/**.hpp"
    }

    includedirs {
        "src/include"
    }

    filter "system:windows"
        files {
            "src/nfd_win.cpp"
        }
        defines { "NFD_STATIC" }

    filter "system:linux"
        files {
            "src/nfd_gtk.cpp",
            "src/nfd_portal.cpp"
        }
        pic "On"
        defines { "NFD_STATIC" }

    filter "system:macosx"
        files {
            "src/nfd_cocoa.m"
        }
        defines { "NFD_STATIC" }

    filter {}
