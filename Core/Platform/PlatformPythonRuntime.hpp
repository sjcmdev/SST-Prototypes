#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace SST::Platform
{
	[[nodiscard]] Path GetAppPythonRoot();
	[[nodiscard]] Path GetAppPythonPlatformRoot();
	[[nodiscard]] Path ResolvePreferredPythonExecutable();
	[[nodiscard]] Path ResolveEmbeddedPythonHome();
	void SetPythonHomeEnvironment(const Path &pythonHome);
} // namespace SST::Platform
