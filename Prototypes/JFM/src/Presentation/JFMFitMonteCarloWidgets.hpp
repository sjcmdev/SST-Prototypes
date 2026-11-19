#pragma once
#include "Presentation/JFMPlotDocument.hpp"

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
		bool hasAxisLimits = false;
		double xMinimum = 0.0;
		double xMaximum = 0.0;
		double yMinimum = 0.0;
		double yMaximum = 0.0;
		int marker = 0;
		std::array<bool, 5> showSigmaClasses = {true, true, true, true, true};
		std::array<bool, 3> showSigmaEllipses = {false, false, false};
		std::array<std::array<float, 4>, 5> sigmaColors = {{
			{1.00f, 0.12156863f, 0.07843137f, 1.00f},
			{1.00f, 0.90196078f, 0.05882353f, 1.00f},
			{0.00f, 1.00f, 0.90980392f, 1.00f},
            {0.65f, 0.45f, 0.85f, 1.0f}, {0.65f, 0.65f, 0.65f, 1.0f}}};
	};

	[[nodiscard]] bool RenderJFMCovariancePlot(
		const JFMMonteCarloResult &result,
		JFMMonteCarloScatterPlotOptions &options,
		const char *id,
		float height, JFMPlotDocument &document);
} // namespace SST::Prototypes::JFM
