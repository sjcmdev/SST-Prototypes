#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace SST
{
	struct ConfigProfileEntry
	{
		std::string name;
		Path path;
	};
} // namespace SST
