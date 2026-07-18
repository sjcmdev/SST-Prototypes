#pragma once

#include "App/JFMUserSettings.hpp"
#include "Domain/JFMTypes.hpp"

#include <string_view>

namespace SST::Prototypes::JFM
{
	[[nodiscard]] bool RenderJFMFitMonteCarloSettings(
		JFMFitMonteCarloSettings &settings,
		std::string_view modelId,
		const char *id);

	void RenderJFMCovariancePlot(
		const JFMMonteCarloResult &result,
		int &xParameter,
		int &yParameter,
		const char *id,
		float height = 260.0f);
} // namespace SST::Prototypes::JFM
