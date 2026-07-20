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
	[[nodiscard]] bool RenderJFMMonteCarloScatterDefaults(JFMFitMonteCarloSettings &settings, std::string_view modelId, const char *id);

	struct JFMMonteCarloScatterPlotOptions
	{
		int xParameter = 0;
		int yParameter = 1;
		bool autoFit = true;
		float zoomOut = 0.08f;
		std::array<bool, 3> showSigmaClasses = {true, true, true};
		std::array<bool, 3> showSigmaEllipses = {false, false, false};
		std::array<std::array<float, 4>, 3> sigmaColors = {{
			{1.00f, 0.12156863f, 0.07843137f, 1.00f},
			{1.00f, 0.90196078f, 0.05882353f, 1.00f},
			{0.00f, 1.00f, 0.90980392f, 1.00f}}};
	};

	[[nodiscard]] bool RenderJFMCovariancePlot(
		const JFMMonteCarloResult &result,
		JFMMonteCarloScatterPlotOptions &options,
		const char *id,
		float height = 520.0f);
} // namespace SST::Prototypes::JFM
