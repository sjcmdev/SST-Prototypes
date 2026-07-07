project "Tracy"
    kind "StaticLib"
    language "C++"
    cppdialect "C++latest"
    staticruntime "off"

    targetdir ("../build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "public/TracyClient.cpp"
    }

    includedirs { "public" }

    defines { "TRACY_ENABLE", "TRACY_NO_SYSTEM_TRACING" }

    filter "system:linux"
        pic "On"

    filter {}
