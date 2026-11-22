#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/Diagnostics/StructuredError.hpp"
#include "Domain/JFMTypes.hpp"

#include <string_view>

namespace SST::Prototypes::JFM
{
	struct JFMFitComputation
	{
		JFMFitState state;
		JFMFitCurve curve;
	};

	struct JFMMonteCarloComputation
	{
		JFMMonteCarloState state;
		JFMMonteCarloResult result;
	};

	[[nodiscard]] const std::vector<std::string_view> &JFMModelParameterNames(std::string_view modelId);
	[[nodiscard]] double EvaluateJFMCurrent(
		std::string_view modelId,
		double voltage,
		double temperatureK,
		const std::unordered_map<std::string, double> &parameters) noexcept;
	[[nodiscard]] Result<JFMFitComputation> RunJFMSimplexFit(
		const JFMCharacteristicRecord &characteristic,
		const JFMFitMonteCarloSettings &settings,
		bool useTunedStart = false);
	[[nodiscard]] Result<JFMMonteCarloComputation> RunJFMMonteCarlo(
		const JFMCharacteristicRecord &characteristic,
		const JFMPrefitState &prefit,
		const JFMFitState &fit,
		const JFMFitMonteCarloSettings &settings);
	// One curve of scratch space; live results regenerate noise with the original sample index.
	[[nodiscard]] std::vector<double> JFMMonteCarloNoisyCurve(const JFMMonteCarloResult &result, std::size_t sample);
	[[nodiscard]] bool JFMGpuAvailable(std::string *message = nullptr);
} // namespace SST::Prototypes::JFM
