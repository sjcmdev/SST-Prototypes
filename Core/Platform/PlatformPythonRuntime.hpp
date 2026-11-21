#pragma once

#include <string>

#include "Core/Utils/Path.hpp"

namespace SST::Platform
{
	[[nodiscard]] Path GetAppPythonRoot();
	[[nodiscard]] Path GetAppPythonPlatformRoot();
	[[nodiscard]] Path ResolvePreferredPythonExecutable();
	[[nodiscard]] Path ResolveEmbeddedPythonHome();
	// Venv discovery. PYTHONHOME points at the venv's base interpreter, so the venv's own site-packages must be added separately.
	[[nodiscard]] Path FindNearbyVenvRoot(Path startDir);
	[[nodiscard]] Path GetVenvSitePackages(const Path &venvRoot);
	[[nodiscard]] Path ResolveEmbeddedPythonSitePackages();
	void SetPythonHomeEnvironment(const Path &pythonHome);
} // namespace SST::Platform
