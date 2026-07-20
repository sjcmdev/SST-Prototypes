#pragma once

#include "App/JFMUserSettings.hpp"
#include "Domain/JFMTypes.hpp"

#include <array>
#include <string_view>

namespace SST::Prototypes::JFM
{
	[[nodiscard]] bool RenderJFMFitMonteCarloSettings(
		JFMFitMonteCarloSettings &settings,
		std::string_view modelId,
		const char *id,
		bool showParameterBounds = true);
	[[nodiscard]] bool RenderJFMMonteCarloScatterDefaults(JFMFitMonteCarloSettings &settings, const char *id);

	struct JFMMonteCarloScatterPlotOptions
	{
		int xParameter = 0;
		int yParameter = 1;
		bool autoFit = true;
		float zoomOut = 0.08f;
		std::array<bool, 3> showSigmaClasses = {true, true, true};
		std::array<bool, 3> showSigmaEllipses = {true, true, true};
		std::array<std::array<float, 4>, 3> sigmaColors = {{
			{1.00f, 0.12f, 0.08f, 1.00f},
			{1.00f, 0.48f, 0.06f, 1.00f},
			{0.05f, 0.85f, 0.95f, 1.00f}}};
	};

	[[nodiscard]] bool RenderJFMCovariancePlot(
		const JFMMonteCarloResult &result,
		JFMMonteCarloScatterPlotOptions &options,
		const char *id,
		float height = 520.0f);
} // namespace SST::Prototypes::JFM
