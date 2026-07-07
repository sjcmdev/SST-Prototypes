#pragma once

#include <vector>

#include "Core/Utils/Path.hpp"

namespace SST::Platform
{
	[[nodiscard]] std::vector<FilePath> GetSystemFontDirectories();
} // namespace SST::Platform
