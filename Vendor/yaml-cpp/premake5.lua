project "yaml-cpp"
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"

    targetdir ("../build/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../build/bin-int/" .. outputdir .. "/%{prj.name}")

    files {
        "include/**.hpp",
        "include/**.h",
        "src/**.cpp"
    }

    includedirs { "include" }

    defines { "YAML_CPP_STATIC_DEFINE" }

    filter "system:linux"
        pic "On"

    filter {}
