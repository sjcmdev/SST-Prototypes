#include "Core/dspch.hpp"

#include "Analysis/JFMFitEngine.hpp"

#include "gpu/gpu_backend.hpp"
#include <sst/math/nelder_mead.hpp>
#include <sst/physics/iv.hpp>
#include "solver/objective.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>

namespace SST::Prototypes::JFM
{
	namespace FitEngineDetail
	{
		enum ParameterIndex : std::size_t
		{
			I0,
			A,
			Rs,
			Rsh,
			Alpha,
			Rsh2,
			Iph,
			ParameterCount
		};

		using ParameterArray = std::array<double, ParameterCount>;
		using FreeMask = std::array<bool, ParameterCount>;
		using CurrentFunction = double (*)(double, double, const ParameterArray &, bool) noexcept;

		constexpr std::array<std::string_view, ParameterCount> ParameterNames = {
			"I0", "A", "Rs", "Rsh", "alpha", "Rsh2", "I_ph"};

		struct ModelSpec
		{
			std::string_view id;
			std::vector<std::string_view> parameters;
			CurrentFunction current = nullptr;
			bool dark = true;
			GpuModelType gpuType = GpuModelType::SD0;
		};

		std::size_t IndexOf(std::string_view name)
		{
			const auto found = std::find(ParameterNames.begin(), ParameterNames.end(), name);
			return found == ParameterNames.end() ? ParameterCount : static_cast<std::size_t>(std::distance(ParameterNames.begin(), found));
		}

		bool HasParameter(const ModelSpec &model, std::size_t index)
		{
			return std::find(model.parameters.begin(), model.parameters.end(), ParameterNames[index]) != model.parameters.end();
		}

		ParameterArray Defaults()
		{
			return {1.0e-10, 1.5, 0.0, 1.0e15, 2.0, 1.0e15, 0.0};
		}

		ParameterArray ParametersFrom(const std::unordered_map<std::string, JFMParameterEstimate> &values)
		{
			ParameterArray result = Defaults();
			for (std::size_t index = 0; index < ParameterCount; ++index)
			{
				const auto found = values.find(std::string(ParameterNames[index]));
				if (found != values.end() && std::isfinite(found->second.value))
					result[index] = found->second.value;
			}
			return result;
		}

		void ApplyTunedMetadata(ParameterArray &parameters, const std::unordered_map<std::string, std::string> &metadata)
		{
			for (std::size_t index = 0; index < ParameterCount; ++index)
			{
				const auto found = metadata.find("tuned." + std::string(ParameterNames[index]));
				if (found == metadata.end())
					continue;
				char *end = nullptr;
				const double value = std::strtod(found->second.c_str(), &end);
				if (end != found->second.c_str() && std::isfinite(value))
					parameters[index] = value;
			}
		}

		double DiodeCurrent(double voltage, double temperature, const ParameterArray &parameters, bool dark) noexcept
		{
			return sst::physics::diodeCurrent(voltage, temperature, parameters[I0], parameters[A], dark ? 0.0 : parameters[Iph]);
		}

		double SeriesDiodeCurrent(double voltage, double temperature, const ParameterArray &parameters, bool dark) noexcept
		{
			// Preserve FitEngine's undefined Rs=0 result until a separate model change.
			if (parameters[Rs] == 0.0)
				return std::numeric_limits<double>::quiet_NaN();
			return sst::physics::seriesDiodeCurrent(voltage, temperature, parameters[I0], parameters[A], parameters[Rs], dark ? 0.0 : parameters[Iph]);
		}

		double ShuntDiodeCurrent(double voltage, double temperature, const ParameterArray &parameters, bool dark) noexcept
		{
			return sst::physics::shuntDiodeCurrent(voltage, temperature, parameters[I0], parameters[A], parameters[Rsh], dark ? 0.0 : parameters[Iph]);
		}

		double SeriesShuntDiodeCurrent(double voltage, double temperature, const ParameterArray &parameters, bool dark) noexcept
		{
			// Preserve FitEngine's undefined Rs=0 result until a separate model change.
			if (parameters[Rs] == 0.0)
				return std::numeric_limits<double>::quiet_NaN();
			return sst::physics::seriesShuntDiodeCurrent(voltage, temperature, parameters[I0], parameters[A], parameters[Rs], parameters[Rsh], dark ? 0.0 : parameters[Iph]);
		}

		double NonlinearShuntDiodeCurrent(double voltage, double temperature, const ParameterArray &parameters, bool dark) noexcept
		{
			// Preserve FitEngine's undefined Rs=0 result until a separate model change.
			if (parameters[Rs] == 0.0)
				return std::numeric_limits<double>::quiet_NaN();
			return sst::physics::nonlinearShuntDiodeCurrent(voltage, temperature, parameters[I0], parameters[A], parameters[Rs], parameters[Rsh], parameters[Alpha], parameters[Rsh2], dark ? 0.0 : parameters[Iph]);
		}

		const std::array<ModelSpec, 10> Models = {{
			{"SD-0", {"I0", "A"}, &DiodeCurrent, true, GpuModelType::SD0},
			{"SD-Rs", {"I0", "A", "Rs"}, &SeriesDiodeCurrent, true, GpuModelType::SDRs},
			{"SD-Rsh", {"I0", "A", "Rsh"}, &ShuntDiodeCurrent, true, GpuModelType::SDRsh},
			{"SD-RsRsh", {"I0", "A", "Rs", "Rsh"}, &SeriesShuntDiodeCurrent, true, GpuModelType::SDRsRsh},
			{"SD-0-I_ph", {"I0", "A", "I_ph"}, &DiodeCurrent, false, GpuModelType::SD0Iph},
			{"SD-Rs-I_ph", {"I0", "A", "Rs", "I_ph"}, &SeriesDiodeCurrent, false, GpuModelType::SDRsIph},
			{"SD-Rsh-I_ph", {"I0", "A", "Rsh", "I_ph"}, &ShuntDiodeCurrent, false, GpuModelType::SDRshIph},
			{"SD-RsRsh-I_ph", {"I0", "A", "Rs", "Rsh", "I_ph"}, &SeriesShuntDiodeCurrent, false, GpuModelType::SDRsRshIph},
			{"SD-RsRsh-alpha-Rsh2", {"I0", "A", "Rs", "Rsh", "alpha", "Rsh2"}, &NonlinearShuntDiodeCurrent, true, GpuModelType::SDRsRshAlphaRsh2},
			{"SD-RsRsh-alpha-Rsh2-I_ph", {"I0", "A", "Rs", "Rsh", "alpha", "Rsh2", "I_ph"}, &NonlinearShuntDiodeCurrent, false, GpuModelType::SDRsRshAlphaRsh2Iph},
		}};

		const ModelSpec *FindModel(std::string_view id)
		{
			const auto model = std::find_if(Models.begin(), Models.end(), [id](const ModelSpec &candidate) {
				return candidate.id == id;
			});
			return model == Models.end() ? nullptr : &*model;
		}

		double Evaluate(const ModelSpec &model, double voltage, double temperature, const ParameterArray &parameters) noexcept
		{
			const double current = model.current(voltage, temperature, parameters, model.dark);
			return std::isfinite(current) ? current : std::numeric_limits<double>::quiet_NaN();
		}

		double Evaluate(const ModelSpec &model, double voltage, double temperature, const double *parameters) noexcept
		{
			ParameterArray values{};
			std::copy_n(parameters, values.size(), values.begin());
			return Evaluate(model, voltage, temperature, values);
		}

		StructuredError MakeError(std::string details)
		{
			return StructuredError{
				ErrorCategory::Validation,
				Severity::Error,
				"JFM fit failed.",
				std::move(details),
				"Check model, fit range, parameter bounds, and temperature.",
				"JFMFitEngine",
				"jfm.fit"};
		}

		const std::vector<double> &Current(const JFMCharacteristicRecord &characteristic)
		{
			return characteristic.data->currentDensity.empty() ? characteristic.data->current : characteristic.data->currentDensity;
		}

		struct FitData
		{
			std::vector<double> voltage;
			std::vector<double> current;
			std::vector<double> sigma;
		};

		FitData BuildFitData(const JFMCharacteristicRecord &characteristic, const JFMPrefitState &prefit, const JFMPrefitCurve *prefitCurve, double noisePercent)
		{
			FitData result;
			const std::vector<double> &sourceCurrent = Current(characteristic);
			const std::size_t count = std::min(characteristic.data->voltage.size(), sourceCurrent.size());
			std::size_t lower = std::min(prefit.fitRange.lowerIndex, count);
			std::size_t upper = std::min(prefit.fitRange.upperIndex, count);
			if (upper <= lower + 1)
			{
				lower = 0;
				upper = count;
			}
			const double factor = std::clamp(noisePercent, 0.0, 100.0) / 100.0;
			result.voltage.reserve(upper - lower);
			result.current.reserve(upper - lower);
			result.sigma.reserve(upper - lower);
			for (std::size_t index = lower; index < upper; ++index)
			{
				const double ideal = prefitCurve != nullptr && index < prefitCurve->current.size()
					? prefitCurve->current[index]
					: sourceCurrent[index];
				result.voltage.push_back(characteristic.data->voltage[index]);
				result.current.push_back(sourceCurrent[index]);
				result.sigma.push_back(std::max(std::abs(ideal * factor), 1.0e-30));
			}
			return result;
		}

		JFMParameterSolverSetting ParameterSetting(const JFMFitMonteCarloSettings &settings, std::size_t index)
		{
			const auto found = settings.parameters.find(std::string(ParameterNames[index]));
			if (found != settings.parameters.end() && std::isfinite(found->second.minimum) && std::isfinite(found->second.maximum) && found->second.minimum < found->second.maximum)
				return found->second;
			const std::array<JFMParameterSolverSetting, ParameterCount> fallback = {{
				{1.0e-300, 1.0, true}, {0.0, 5.0, true}, {0.0, 1.0e9, true},
				{1.0e-6, 1.0e15, true}, {0.1, 10.0, true}, {1.0e-6, 1.0e15, true}, {0.0, 1.0e6, true}}};
			return fallback[index];
		}

		JFMParameterSolverSetting ParameterSettingIncludingValue(const JFMFitMonteCarloSettings &settings, std::size_t index, double value)
		{
			JFMParameterSolverSetting setting = ParameterSetting(settings, index);
			if (!std::isfinite(value))
				return setting;
			const JFMParameterSolverSetting fallback = ParameterSetting(JFMFitMonteCarloSettings{}, index);
			if (value < setting.minimum)
				setting.minimum = std::min(fallback.minimum, value);
			if (value > setting.maximum)
				setting.maximum = std::max(fallback.maximum, value);
			if (setting.minimum == setting.maximum)
				setting.maximum = std::nextafter(setting.minimum, std::numeric_limits<double>::infinity());
			return setting;
		}

		struct SolverOutput
		{
			ParameterArray parameters{};
			sst::math::FitResult result;
		};

		SolverOutput Solve(
			const ModelSpec &model,
			const FitData &data,
			double temperature,
			ParameterArray start,
			const ParameterArray &fixed,
			const FreeMask &freeMask,
			const JFMFitMonteCarloSettings &settings,
			int maxIterations)
		{
			std::vector<sst::math::FitParam> fitParameters;
			fitParameters.reserve(ParameterCount);
			int freeCount = 0;
			for (std::size_t index = 0; index < ParameterCount; ++index)
			{
				const bool inModel = HasParameter(model, index);
				const bool free = inModel && freeMask[index];
				double value = free ? start[index] : fixed[index];
				const JFMParameterSolverSetting setting = inModel ? ParameterSettingIncludingValue(settings, index, value) : ParameterSetting(settings, index);
				value = std::clamp(value, setting.minimum, setting.maximum);
				fitParameters.emplace_back(std::string(ParameterNames[index]), value, setting.minimum, setting.maximum, free);
				freeCount += free ? 1 : 0;
			}

			SolverOutput output;
			if (freeCount == 0)
			{
				output.parameters = fixed;
				output.result.chi2_min = computeChiSquared(
					fixed.data(), data.voltage.data(), data.current.data(), data.sigma.data(), static_cast<int>(data.voltage.size()),
					[&model, temperature](double voltage, const double *parameters) { return Evaluate(model, voltage, temperature, parameters); });
				output.result.reduced_chi2_min = output.result.chi2_min / static_cast<double>(std::max<std::size_t>(1, data.voltage.size()));
				output.result.converged = std::isfinite(output.result.chi2_min);
				output.result.stop_reason = "fixed_parameters";
				return output;
			}

			auto objective = [model, data, temperature, fitParameters](const std::vector<double> &freeValues) {
				ParameterArray full{};
				std::size_t freeIndex = 0;
				for (std::size_t index = 0; index < ParameterCount; ++index)
					full[index] = fitParameters[index].free ? freeValues[freeIndex++] : fitParameters[index].value;
				return computeChiSquared(
					full.data(), data.voltage.data(), data.current.data(), data.sigma.data(), static_cast<int>(data.voltage.size()),
					[&model, temperature](double voltage, const double *parameters) { return Evaluate(model, voltage, temperature, parameters); });
			};
			const double degreesOfFreedom = static_cast<double>(std::max(1, static_cast<int>(data.voltage.size()) - freeCount));
			sst::math::SANelderMead solver(std::move(fitParameters), std::move(objective), 1.0, 2.0, 0.5, 0.5, 1.0e-12, false, {}, 0, false, degreesOfFreedom);
			solver.initSimplex();
			// JFM's negative tolerance disables early stopping; this objective is nonnegative.
			output.result = solver.runUntilConvergence(std::max(1, maxIterations), std::max(0.0, settings.fitReducedChiSquaredTolerance));
			const std::vector<double> full = solver.bestFullParams();
			std::copy_n(full.begin(), std::min(full.size(), output.parameters.size()), output.parameters.begin());
			return output;
		}

		FreeMask AllModelParameters(const ModelSpec &model)
		{
			FreeMask result{};
			for (std::size_t index = 0; index < ParameterCount; ++index)
				result[index] = HasParameter(model, index);
			return result;
		}

		FreeMask MonteCarloParameters(const ModelSpec &model, const JFMFitMonteCarloSettings &settings)
		{
			FreeMask result{};
			for (std::size_t index = 0; index < ParameterCount; ++index)
				result[index] = HasParameter(model, index) && ParameterSetting(settings, index).monteCarloFree;
			return result;
		}

		std::unordered_map<std::string, double> ParameterMap(const ModelSpec &model, const ParameterArray &values)
		{
			std::unordered_map<std::string, double> result;
			for (std::string_view name : model.parameters)
				result[std::string(name)] = values[IndexOf(name)];
			return result;
		}

		std::vector<double> EvaluateCurve(const ModelSpec &model, const std::vector<double> &voltage, double temperature, const ParameterArray &parameters)
		{
			std::vector<double> current(voltage.size());
			for (std::size_t index = 0; index < voltage.size(); ++index)
				current[index] = Evaluate(model, voltage[index], temperature, parameters);
			return current;
		}

		double ChiSquaredFor(const ModelSpec &model, const FitData &data, double temperature, const ParameterArray &parameters)
		{
			return computeChiSquared(
				parameters.data(), data.voltage.data(), data.current.data(), data.sigma.data(), static_cast<int>(data.voltage.size()),
				[&model, temperature](double voltage, const double *values) { return Evaluate(model, voltage, temperature, values); });
		}

		double FinalMonteCarloChiSquared(
			const ModelSpec &model,
			const std::vector<double> &voltage,
			const std::vector<double> &trueCurrent,
			double temperature,
			const ParameterArray &parameters,
			double noisePercent)
		{
			const double noise = std::max(std::clamp(noisePercent, 0.0, 100.0) / 100.0, 1.0e-30);
			double chiSquared = 0.0;
			const std::size_t count = std::min(voltage.size(), trueCurrent.size());
			for (std::size_t index = 0; index < count; ++index)
			{
				const double fitCurrent = Evaluate(model, voltage[index], temperature, parameters);
				if (!(trueCurrent[index] > 0.0) || !(fitCurrent > 0.0) || !std::isfinite(trueCurrent[index]) || !std::isfinite(fitCurrent))
					return std::numeric_limits<double>::infinity();
				const double residual = (std::log(fitCurrent) - std::log(trueCurrent[index])) / noise;
				if (!std::isfinite(residual))
					return std::numeric_limits<double>::infinity();
				chiSquared += residual * residual;
			}
			return chiSquared;
		}
	} // namespace FitEngineDetail

	using namespace FitEngineDetail;

	const std::vector<std::string_view> &JFMModelParameterNames(std::string_view modelId)
	{
		static const std::vector<std::string_view> empty;
		const ModelSpec *model = FindModel(modelId);
		return model == nullptr ? empty : model->parameters;
	}

	double EvaluateJFMCurrent(std::string_view modelId, double voltage, double temperatureK, const std::unordered_map<std::string, double> &parameters) noexcept
	{
		const ModelSpec *model = FindModel(modelId);
		if (model == nullptr)
			return std::numeric_limits<double>::quiet_NaN();
		ParameterArray values = Defaults();
		for (std::size_t index = 0; index < ParameterCount; ++index)
		{
			if (const auto found = parameters.find(std::string(ParameterNames[index])); found != parameters.end())
				values[index] = found->second;
		}
		return Evaluate(*model, voltage, temperatureK, values);
	}

	Result<JFMFitComputation> RunJFMSimplexFit(
		const JFMCharacteristicRecord &characteristic,
		const JFMFitMonteCarloSettings &settings,
		bool useTunedStart)
	{
		if (characteristic.data == nullptr || characteristic.data->voltage.empty())
			return MakeError("Characteristic data is unavailable.");
		if (!characteristic.info.temperatureK)
			return MakeError("Characteristic temperature is missing.");
		if (!characteristic.info.prefit || characteristic.info.prefit->status != "completed")
			return MakeError("Completed prefit is required as the Simplex start point.");
		const ModelSpec *model = FindModel(characteristic.info.prefit->modelId);
		if (model == nullptr)
			return MakeError("Unsupported model: " + characteristic.info.prefit->modelId);

		const ParameterArray prefitStart = ParametersFrom(characteristic.info.prefit->parameters);
		ParameterArray tuned = prefitStart;
		ApplyTunedMetadata(tuned, characteristic.info.metadata);
		const ParameterArray start = useTunedStart ? tuned : prefitStart;
		const FitData data = BuildFitData(characteristic, *characteristic.info.prefit, nullptr, settings.monteCarloNoisePercent);
		if (data.voltage.size() < model->parameters.size() + 2)
			return MakeError("Fit range contains too few points.");
		const SolverOutput solved = Solve(*model, data, *characteristic.info.temperatureK, start, tuned, MonteCarloParameters(*model, settings), settings, settings.fitMaxIterations);

		JFMFitComputation output;
		output.state.modelId = std::string(model->id);
		output.state.status = "completed";
		output.state.fitRange = characteristic.info.prefit->fitRange;
		output.state.chiSquared = solved.result.chi2_min;
		output.state.reducedChiSquared = solved.result.reduced_chi2_min;
		output.state.iterations = solved.result.iterations;
		output.state.converged = solved.result.converged;
		output.state.backend = "cpu-simplex";
		if (!solved.result.converged)
			output.state.warnings.push_back("Simplex stopped: " + solved.result.stop_reason);
		for (std::string_view name : model->parameters)
			output.state.parameters[std::string(name)] = JFMParameterEstimate{solved.parameters[IndexOf(name)], std::nullopt, "simplex_cpu", {}};
		output.curve.voltage = characteristic.data->voltage;
		output.curve.current = EvaluateCurve(*model, output.curve.voltage, *characteristic.info.temperatureK, solved.parameters);
		return output;
	}

	bool JFMGpuAvailable(std::string *message)
	{
		const GpuDeviceStatus status = gpuQueryDevice();
		if (message != nullptr)
			*message = status.message.empty() ? status.name : status.message;
		return status.available;
	}

	Result<JFMMonteCarloComputation> RunJFMMonteCarlo(
		const JFMCharacteristicRecord &characteristic,
		const JFMPrefitState &prefit,
		const JFMFitState &fit,
		const JFMFitMonteCarloSettings &settings)
	{
		if (characteristic.data == nullptr || !characteristic.info.temperatureK)
			return MakeError("Characteristic data or temperature is unavailable.");
		const ModelSpec *model = FindModel(fit.modelId);
		if (model == nullptr || prefit.modelId != fit.modelId)
			return MakeError("Prefit and fit models do not match.");
		ParameterArray prefitValues = ParametersFrom(prefit.parameters);
		ParameterArray fitValues = ParametersFrom(fit.parameters);
		const FreeMask freeMask = MonteCarloParameters(*model, settings);
		JFMFitMonteCarloSettings monteCarloSolverSettings = settings;
		monteCarloSolverSettings.fitReducedChiSquaredTolerance = -1.0;

		JFMMonteCarloComputation output;
		output.state.modelId = fit.modelId;
		output.state.sampleCount = settings.monteCarloSamples;
		output.state.noisePercent = settings.monteCarloNoisePercent;
		output.state.startPerturbationPercent = settings.monteCarloStartPerturbationPercent;
		output.state.seed = settings.monteCarloSeed;
		for (std::string_view name : model->parameters)
			output.result.parameterNames.emplace_back(name);
		output.result.freeParameterCount = static_cast<int>(std::count(freeMask.begin(), freeMask.end(), true));
		auto attachCurvesToResult = [&output]() {
			output.result.voltage = output.voltage;
			output.result.idealCurrent = output.idealCurrent;
			output.result.noisyCurrent = output.noisyCurrent;
		};

		const FitData source = BuildFitData(characteristic, prefit, nullptr, settings.monteCarloNoisePercent);
		output.voltage = source.voltage;
		output.idealCurrent = EvaluateCurve(*model, output.voltage, *characteristic.info.temperatureK, fitValues);
		const double factor = std::clamp(settings.monteCarloNoisePercent, 0.0, 100.0) / 100.0;
		output.sigma.resize(output.idealCurrent.size());
		for (std::size_t index = 0; index < output.idealCurrent.size(); ++index)
			output.sigma[index] = std::max(std::abs(output.idealCurrent[index] * factor), 1.0e-30);

		std::string gpuMessage;
		if (settings.preferGpu && JFMGpuAvailable(&gpuMessage))
		{
			GpuMonteCarloRequest request;
			request.model_type = model->gpuType;
			request.voltages = output.voltage;
			request.true_current = output.idealCurrent;
			request.n_samples = settings.monteCarloSamples;
			request.noise_pct = settings.monteCarloNoisePercent;
			request.noise_seed = settings.monteCarloSeed;
			request.start_perturbation_pct = settings.monteCarloStartPerturbationPercent;
			request.layout.n_total = static_cast<int>(ParameterCount);
			request.layout.T = *characteristic.info.temperatureK;
			for (std::size_t index = 0; index < ParameterCount; ++index)
				request.layout.fixed_values[index] = freeMask[index] ? prefitValues[index] : fitValues[index];
			for (std::size_t index = 0; index < ParameterCount; ++index)
			{
				if (!freeMask[index])
					continue;
				const int freeIndex = request.layout.n_free++;
				const double value = request.layout.fixed_values[index];
				const JFMParameterSolverSetting setting = ParameterSettingIncludingValue(settings, index, value);
				request.layout.free_to_total[freeIndex] = static_cast<int>(index);
				request.layout.min_bounds[freeIndex] = setting.minimum;
				request.layout.max_bounds[freeIndex] = setting.maximum;
			}
			request.layout.dof = std::max(1, static_cast<int>(output.voltage.size()) - request.layout.n_free);
			request.nm_config.max_iter = settings.monteCarloMaxIterations;
			request.nm_config.reduced_chi2_tol = monteCarloSolverSettings.fitReducedChiSquaredTolerance;
			if (settings.autoCalibrateGpuSimplexIterations && settings.simplexCalibrationRuns > 0)
			{
				GpuMonteCarloRequest calibration = request;
				calibration.n_samples = settings.simplexCalibrationRuns;
				GpuMonteCarloOutput calibrationOutput = gpuRunMonteCarlo(calibration);
				if (calibrationOutput.success && !calibrationOutput.results.empty())
				{
					int maxIterations = 0;
					for (const GpuMcResult &sample : calibrationOutput.results)
						maxIterations = std::max(maxIterations, sample.iterations);
					request.nm_config.max_iter = std::max(1, maxIterations + settings.simplexCalibrationExtraIterations);
				}
				else if (!calibrationOutput.message.empty())
					output.state.warnings.push_back("GPU simplex calibration failed: " + calibrationOutput.message);
			}
			GpuMonteCarloOutput gpu = gpuRunMonteCarlo(request);
			if (!gpu.success)
				output.state.warnings.push_back(gpu.message + "; CPU fallback used.");
			else
			{
				output.state.backend = "cuda";
				output.state.totalMilliseconds = gpu.total_ms;
				output.noisyCurrent = std::move(gpu.noisy_current);
				output.result.samples.reserve(gpu.results.size());
				for (const GpuMcResult &gpuSample : gpu.results)
				{
					JFMMonteCarloSample sample;
					int freeIndex = 0;
					for (std::string_view name : model->parameters)
					{
						const std::size_t index = IndexOf(name);
						sample.parameters.push_back(freeMask[index] ? gpuSample.best_free_params[freeIndex++] : fitValues[index]);
					}
					sample.chiSquared = gpuSample.chi2_min;
					sample.reducedChiSquared = gpuSample.reduced_chi2_min;
					sample.iterations = gpuSample.iterations;
					sample.converged = gpuSample.converged != 0;
					output.state.acceptedCount += sample.converged ? 1 : 0;
					output.result.samples.push_back(std::move(sample));
				}
				output.state.status = "completed";
				attachCurvesToResult();
				return output;
			}
		}
		else if (settings.preferGpu && !gpuMessage.empty())
		{
			output.state.warnings.push_back(gpuMessage + "; CPU fallback used.");
		}

		output.state.backend = "cpu";
		const auto started = std::chrono::steady_clock::now();
		std::mt19937 rng(settings.monteCarloSeed);
		std::normal_distribution<double> normal(0.0, 1.0);
		std::uniform_real_distribution<double> perturbation(-1.0, 1.0);
		output.noisyCurrent.reserve(static_cast<std::size_t>(settings.monteCarloSamples) * output.idealCurrent.size());
		output.result.samples.reserve(settings.monteCarloSamples);
		for (int run = 0; run < settings.monteCarloSamples; ++run)
		{
			FitData noisyData;
			noisyData.voltage = output.voltage;
			noisyData.sigma = output.sigma;
			noisyData.current.resize(output.idealCurrent.size());
			for (std::size_t point = 0; point < output.idealCurrent.size(); ++point)
			{
				const double noiseAmplitude = output.idealCurrent[point] * factor;
				noisyData.current[point] = output.idealCurrent[point] + normal(rng) * noiseAmplitude;
				output.noisyCurrent.push_back(noisyData.current[point]);
			}
			ParameterArray start = prefitValues;
			const double perturbationFactor = settings.monteCarloStartPerturbationPercent / 100.0;
			for (std::size_t index = 0; index < ParameterCount; ++index)
			{
				if (!freeMask[index])
					continue;
				const JFMParameterSolverSetting setting = ParameterSettingIncludingValue(settings, index, start[index]);
				const double scale = std::abs(start[index]) > 1.0e-300
					? std::abs(start[index])
					: std::max((setting.maximum - setting.minimum) * 1.0e-12, 1.0e-15);
				start[index] = std::clamp(start[index] + perturbation(rng) * perturbationFactor * scale, setting.minimum, setting.maximum);
			}
			const SolverOutput solved = Solve(*model, noisyData, *characteristic.info.temperatureK, start, fitValues, freeMask, monteCarloSolverSettings, settings.monteCarloMaxIterations);
			const double finalChiSquared = FinalMonteCarloChiSquared(*model, output.voltage, output.idealCurrent, *characteristic.info.temperatureK, solved.parameters, settings.monteCarloNoisePercent);
			JFMMonteCarloSample sample;
			for (std::string_view name : model->parameters)
				sample.parameters.push_back(solved.parameters[IndexOf(name)]);
			sample.chiSquared = finalChiSquared;
			sample.reducedChiSquared = finalChiSquared;
			sample.iterations = solved.result.iterations;
			sample.converged = std::isfinite(finalChiSquared);
			output.state.acceptedCount += sample.converged ? 1 : 0;
			output.result.samples.push_back(std::move(sample));
		}
		output.state.totalMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		output.state.status = "completed";
		attachCurvesToResult();
		return output;
	}
} // namespace SST::Prototypes::JFM
