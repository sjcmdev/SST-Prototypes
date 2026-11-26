#pragma once

#include "Core/Utils/Path.hpp"
#include <cstdlib>

namespace SST::Prototypes::JFM
{
    inline Path RealDataPath(const char* relative)
    {
        const auto* root = std::getenv("SST_JFM_REAL_DATA");
        return (root && *root ? Path(root) : Path::FromResolved(FileSystem::CurrentPath()) /
            Path("Prototypes/JFM/example-data/real-measurement")) / Path(relative);
    }
}
