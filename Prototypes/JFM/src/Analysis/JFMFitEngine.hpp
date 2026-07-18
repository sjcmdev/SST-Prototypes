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
		std::vector<double> voltage;
		std::vector<double> idealCurrent;
		std::vector<double> sigma;
		std::vector<double> noisyCurrent;
	};

	[[nodiscard]] const std::vector<std::string_view> &JFMModelParameterNames(std::string_view modelId);
	[[nodiscard]] double EvaluateJFMCurrent(
		std::string_view modelId,
		double voltage,
		double temperatureK,
		const std::unordered_map<std::string, double> &parameters) noexcept;
	[[nodiscard]] Result<JFMFitComputation> RunJFMSimplexFit(
		const JFMCharacteristicRecord &characteristic,
		const JFMFitMonteCarloSettings &settings);
	[[nodiscard]] Result<JFMMonteCarloComputation> RunJFMMonteCarlo(
		const JFMCharacteristicRecord &characteristic,
		const JFMPrefitState &prefit,
		const JFMFitState &fit,
		const JFMFitMonteCarloSettings &settings);
	[[nodiscard]] bool JFMGpuAvailable(std::string *message = nullptr);
} // namespace SST::Prototypes::JFM
