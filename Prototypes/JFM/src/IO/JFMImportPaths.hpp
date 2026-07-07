#pragma once

#include <vector>

#include "App/ApplicationState.hpp"
#include "Core/Utils/Path.hpp"

namespace SST::Prototypes::JFM
{
	[[nodiscard]] std::vector<Path> BuildJFMPythonRootCandidates(
		const ApplicationConfig &config,
		const Path &searchRoot);
	[[nodiscard]] Path ResolveJFMPythonRoot(const ApplicationConfig &config);
	[[nodiscard]] Path ResolveJFMPythonRoot(const ApplicationConfig &config, const Path &searchRoot);
} // namespace SST::Prototypes::JFM
