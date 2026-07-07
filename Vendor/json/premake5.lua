project "json"
    kind "Utility"
    language "C++"
    staticruntime "off"

    files {
        "include/**.hpp",
        "include/**.h",
        "single_include/**.hpp",
        "single_include/**.h"
    }

    includedirs { "include", "single_include" }
