project "thread-pool"
    kind "Utility"
    language "C++"

    files {
        "include/**.hpp",
        "include/**.h"
    }

    includedirs { "include" }
