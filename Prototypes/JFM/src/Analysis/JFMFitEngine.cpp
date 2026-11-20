#include "Core/dspch.hpp"

#include "Analysis/JFMFitEngine.hpp"

#include "gpu/gpu_backend.hpp"
#include <sst/math/nelder_mead.hpp>
#include <sst/physics/iv.hpp>
#include "solver/objective.hpp"
#include "solver/fit_solver.hpp"

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

		constexpr std::array<std::string_view, ParameterCount> ParameterNames = {
			"I0", "A", "Rs", "Rsh", "alpha", "Rsh2", "I_ph"};

		struct ModelSpec
		{
			std::string_view id;
			std::vector<std::string_view> parameters;
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
				if (found != values.end())
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

		const std::array<ModelSpec, 10> Models = {{
			{"SD-0", {"I0", "A"}, GpuModelType::SD0},
			{"SD-Rs", {"I0", "A", "Rs"}, GpuModelType::SDRs},
			{"SD-Rsh", {"I0", "A", "Rsh"}, GpuModelType::SDRsh},
			{"SD-RsRsh", {"I0", "A", "Rs", "Rsh"}, GpuModelType::SDRsRsh},
			{"SD-0-I_ph", {"I0", "A", "I_ph"}, GpuModelType::SD0Iph},
			{"SD-Rs-I_ph", {"I0", "A", "Rs", "I_ph"}, GpuModelType::SDRsIph},
			{"SD-Rsh-I_ph", {"I0", "A", "Rsh", "I_ph"}, GpuModelType::SDRshIph},
			{"SD-RsRsh-I_ph", {"I0", "A", "Rs", "Rsh", "I_ph"}, GpuModelType::SDRsRshIph},
			{"SD-RsRsh-alpha-Rsh2", {"I0", "A", "Rs", "Rsh", "alpha", "Rsh2"}, GpuModelType::SDRsRshAlphaRsh2},
			{"SD-RsRsh-alpha-Rsh2-I_ph", {"I0", "A", "Rs", "Rsh", "alpha", "Rsh2", "I_ph"}, GpuModelType::SDRsRshAlphaRsh2Iph},
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
			const double current = lm::current(model.gpuType, voltage, parameters.data(), temperature, nullptr, true);
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
            GpuParamLayout objective;
		};

		FitData BuildFitData(const JFMCharacteristicRecord &characteristic, const JFMPrefitState &prefit, const JFMFitMonteCarloSettings &settings)
		{
			FitData result;
			const std::vector<double> &sourceCurrent = Current(characteristic);
			const std::size_t count = characteristic.data->voltage.size();
            if (count < 2 || sourceCurrent.size() != count) return result;
            const double lower = prefit.fitRange.lowerVoltage;
            const double upper = prefit.fitRange.upperVoltage;
            if (!std::isfinite(lower) || !std::isfinite(upper) || upper <= lower)
                return result;
            const double factor = settings.monteCarloNoisePercent / 100.0;
            auto &policy = result.objective;
            // Reverse-current points outside a dark fit must not change its regression weights.
            bool dark = true;
            for (std::size_t i=0;i<count;++i) {
                const double v=characteristic.data->voltage[i];
                if ((!std::isfinite(v) || (v>=lower-1e-12 && v<=upper+1e-12)) && !(sourceCurrent[i]>0)) { dark=false; break; }
            }
            policy.objective = settings.objectiveMode == "signed_sigma" ? JfmObjective::SignedSigma :
                settings.objectiveMode == "isc_shift" || (settings.objectiveMode == "auto" && !dark) ? JfmObjective::IscShift : JfmObjective::LogLegacy;
            policy.sigma_rel = factor; policy.switch_k = settings.objectiveSwitchK;
            if (policy.objective != JfmObjective::LogLegacy) {
                std::vector<double> magnitudes; magnitudes.reserve(sourceCurrent.size());
                for (double y : sourceCurrent) magnitudes.push_back(std::abs(y));
                const double maximum = *std::max_element(magnitudes.begin(),magnitudes.end());
                auto middle = magnitudes.begin()+magnitudes.size()/2;
                std::nth_element(magnitudes.begin(),middle,magnitudes.end());
                const double scale = std::max(*middle,maximum*1e-12);
                policy.sigma_abs = settings.sigmaAbsolute > 0 ? settings.sigmaAbsolute : scale*1e-4;
                if (policy.objective == JfmObjective::IscShift)
                    policy.shift = lm::iscShift(characteristic.data->voltage.data(), sourceCurrent.data(), static_cast<int>(count));
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                const double voltage = characteristic.data->voltage[index];
                if (std::isfinite(voltage) && (voltage < lower - 1e-12 || voltage > upper + 1e-12))
                    continue;
                result.voltage.push_back(voltage);
                result.current.push_back(sourceCurrent[index]);
                result.sigma.push_back(std::abs(sourceCurrent[index] * factor) + policy.sigma_abs);
            }
			return result;
		}

        std::string ValidateData(const FitData &data)
        {
            if (data.voltage.size() < 2)
                return "Invalid or empty fit range; choose increasing voltage bounds.";
            if (!std::isfinite(data.objective.sigma_rel) || data.objective.sigma_rel < 0 || data.objective.sigma_rel > 1 ||
                !std::isfinite(data.objective.sigma_abs) || data.objective.sigma_abs < 0)
                return "Invalid noise model: sigma must be finite and nonnegative.";
            if (!std::isfinite(data.objective.shift))
                return "Objective domain: isc_shift requires measured data bracketing V=0; use signed_sigma otherwise.";
            if (!(data.objective.switch_k > 0) || !std::isfinite(data.objective.switch_k))
                return "Objective switch k must be finite and positive.";
            for (std::size_t i = 0; i < data.voltage.size(); ++i) {
                if (!std::isfinite(data.voltage[i]) || !std::isfinite(data.current[i]))
                    return "Objective domain: non-finite voltage or current.";
                if (data.objective.objective != JfmObjective::LogLegacy && !(data.sigma[i]>0))
                    return "Cannot infer a nonzero instrument floor from these data; set a positive absolute sigma floor.";
                if (data.objective.objective == JfmObjective::LogLegacy && (!(data.current[i]>0) || data.current[i]==1))
                    return "Objective domain: log_legacy requires positive current different from 1.";
            }
            return {};
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

        GpuParamLayout Layout(const ModelSpec &model, double temperature, const ParameterArray &start,
            const ParameterArray &fixed, const FreeMask &freeMask, const JFMFitMonteCarloSettings &settings, int count)
        {
            GpuParamLayout layout;
            layout.n_total = ParameterCount; layout.T = temperature;
            for (std::size_t k = 0; k < ParameterCount; ++k) {
                const bool free = HasParameter(model, k) && freeMask[k];
                const double value = free ? start[k] : fixed[k];
                const auto bounds = ParameterSettingIncludingValue(settings, k, value);
                layout.fixed_values[k] = std::clamp(value, bounds.minimum, bounds.maximum);
                if (free) {
                    const int j = layout.n_free++;
                    layout.free_to_total[j] = static_cast<int>(k);
                    layout.min_bounds[j] = bounds.minimum; layout.max_bounds[j] = bounds.maximum;
                }
            }
            layout.dof = std::max(1, count-layout.n_free);
            return layout;
        }
        struct SolverOutput {
            ParameterArray parameters{};
            sst::math::FitResult result;
            lm::FitOutcome outcome;
            std::string backend = "cpu";
        };
        SolverOutput Solve(const ModelSpec &model, const FitData &data, double temperature,
            ParameterArray start, const ParameterArray &fixed, const FreeMask &freeMask,
            const JFMFitMonteCarloSettings &settings, int maxIterations, uint64_t seed = 0, int sample = 0, double perturb = 0)
        {
            auto layout = Layout(model, temperature, start, fixed, freeMask, settings, static_cast<int>(data.voltage.size()));
            layout.objective = data.objective.objective; layout.shift = data.objective.shift;
            layout.sigma_rel = data.objective.sigma_rel; layout.sigma_abs = data.objective.sigma_abs;
            layout.switch_k = data.objective.switch_k;
            layout.point_sigma = data.sigma.data();
            GpuNmConfig config; config.max_iter = std::max(1, maxIterations); config.reduced_chi2_tol = -1;
            SolverOutput out;
            if (settings.preferGpu && JFMGpuAvailable() && layout.n_free > 0) {
                GpuMonteCarloRequest request; request.model_type = model.gpuType; request.layout = layout;
                request.nm_config = config; request.use_lm = settings.fitMethod == "lm";
                request.voltages = data.voltage; request.true_current = data.current;
                request.generate_noise = false; request.sigma = data.sigma; request.n_samples = 1; request.noise_pct = 0; request.noise_seed = static_cast<unsigned int>(seed);
                request.start_perturbation_pct = perturb;
                auto gpu = gpuRunMonteCarlo(request);
                if (gpu.success && gpu.results.size() == 1) {
                    const auto &r = gpu.results.front(); out.backend = "cuda";
                    std::copy_n(r.best_free_params, layout.n_free, out.outcome.result.p);
                    out.outcome.result.chi = r.objective_chi2; out.outcome.result.status = r.stop_status;
                    out.outcome.result.iterations = r.iterations; out.outcome.lm_status = r.lm_status;
                    out.outcome.fallback = r.lm_fallback != 0; out.outcome.flat = r.flat_directions != 0;
                }
            }
            if (out.backend == "cpu") {
            out.outcome = lm::fitSample(model.gpuType, layout, config, data.voltage.data(), data.current.data(),
                static_cast<int>(data.voltage.size()), seed, sample, perturb, settings.fitMethod == "lm");
            }
            double full[8]; lm::full(layout, out.outcome.result.p, full);
            std::copy_n(full, ParameterCount, out.parameters.begin());
            out.result.chi2_min = out.outcome.result.chi;
            out.result.reduced_chi2_min = out.result.chi2_min / layout.dof;
            out.result.iterations = out.outcome.result.iterations;
            out.result.converged = out.outcome.result.status == 1 && !out.outcome.flat;
            out.result.stop_reason = lm::stopReason(out.outcome.result.status, out.outcome.flat);
            return out;
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

        JFMObjectiveSummary ObjectiveSummary(const FitData &data, const JFMCharacteristicRecord &record) {
            JFMObjectiveSummary out;
            out.mode = data.objective.objective == JfmObjective::LogLegacy ? "log_legacy" : data.objective.objective == JfmObjective::IscShift ? "isc_shift" : "signed_sigma";
            out.currentUnit = record.data->currentDensity.empty() ? "A" : "A/m2";
            const auto role = record.data->currentDensity.empty() ? "current" : "current_density";
            for (const auto &column : record.info.columns) if (column.role == role && !column.unit.empty()) out.currentUnit = column.unit;
            out.shift = data.objective.shift; out.sigmaAbsolute = data.objective.sigma_abs;
            out.sigmaRelative = data.objective.sigma_rel; out.switchK = data.objective.switch_k;
            for (std::size_t i=0;i<data.current.size();++i)
                if (out.mode == "signed_sigma" || (out.mode == "isc_shift" && data.current[i]+out.shift <= out.switchK*data.sigma[i])) ++out.linearPoints;
                else ++out.logPoints;
            return out;
        }
		double FinalMonteCarloChiSquared(
			const ModelSpec &model,
			const std::vector<double> &voltage,
			const std::vector<double> &trueCurrent,
			double temperature,
			const ParameterArray &parameters,
			double noisePercent, const FitData &data)
		{
			const double noise = std::clamp(noisePercent, 0.0, 100.0) / 100.0;
			double chiSquared = 0.0;
			const std::size_t count = std::min(voltage.size(), trueCurrent.size());
			for (std::size_t index = 0; index < count; ++index)
			{
				const double fitCurrent = Evaluate(model, voltage[index], temperature, parameters);
                if (data.objective.objective != JfmObjective::LogLegacy) {
                    if (data.sigma[index] == 0) continue;
                    const double r = lm::statisticalResidual(fitCurrent, trueCurrent[index], data.sigma[index], data.objective);
                    chiSquared += r*r;
                    continue;
                }
				if (!(trueCurrent[index] > 0.0) || !(fitCurrent > 0.0) || !std::isfinite(trueCurrent[index]) || !std::isfinite(fitCurrent))
					return std::numeric_limits<double>::infinity();
				const double residual = (lm::shared_log(fitCurrent) - lm::shared_log(trueCurrent[index])) / (noise == 0.0 ? 1.0 : noise);
				if (!std::isfinite(residual))
					return std::numeric_limits<double>::infinity();
				chiSquared += residual * residual;
			}
			return noise == 0.0 && data.objective.sigma_abs == 0.0 ? 0.0 : chiSquared;
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
			return MakeError("Completed prefit is required as the fit start point.");
		const ModelSpec *model = FindModel(characteristic.info.prefit->modelId);
		if (model == nullptr)
			return MakeError("Unsupported model: " + characteristic.info.prefit->modelId);

		const ParameterArray prefitStart = ParametersFrom(characteristic.info.prefit->parameters);
		ParameterArray tuned = prefitStart;
		ApplyTunedMetadata(tuned, characteristic.info.metadata);
		const ParameterArray start = useTunedStart ? tuned : prefitStart;
		const FitData data = BuildFitData(characteristic, *characteristic.info.prefit, settings);
		if (const auto error = ValidateData(data); !error.empty())
			return MakeError(error);
		if (data.voltage.size() < model->parameters.size() + 2)
			return MakeError("Fit range contains too few points.");
        for (double voltage : data.voltage)
            if (!std::isfinite(Evaluate(*model, voltage, *characteristic.info.temperatureK, start)))
                return MakeError("Model domain: invalid parameters or negative junction voltage with fractional alpha.");
		const SolverOutput solved = Solve(*model, data, *characteristic.info.temperatureK, start, tuned, MonteCarloParameters(*model, settings), settings, settings.fitMaxIterations);

        if (!std::isfinite(solved.result.chi2_min)) return MakeError("Objective or model domain is non-finite; no fit was accepted.");
		JFMFitComputation output;
		output.state.objective = ObjectiveSummary(data, characteristic);
		output.state.modelId = std::string(model->id);
		output.state.status = "completed";
		output.state.fitRange = characteristic.info.prefit->fitRange;
		output.state.chiSquared = solved.result.chi2_min;
		output.state.reducedChiSquared = solved.result.reduced_chi2_min;
		output.state.iterations = solved.result.iterations;
		output.state.converged = solved.result.converged;
		output.state.backend = solved.backend;
        output.state.accepted = true;
        output.state.flatDirections = solved.outcome.flat;
        output.state.fitMethod = settings.fitMethod == "nm" ? "NM" : solved.outcome.fallback ? "LM→NM" : "LM";
        output.state.stopReason = solved.result.stop_reason;
        output.state.lmStatus = solved.outcome.lm_status;
		if (!solved.result.converged)
			output.state.warnings.push_back("Fit stopped: " + solved.result.stop_reason);
		for (std::string_view name : model->parameters)
			output.state.parameters[std::string(name)] = JFMParameterEstimate{solved.parameters[IndexOf(name)], std::nullopt, "fit_" + solved.backend + "_" + settings.fitMethod, {}};
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
        monteCarloSolverSettings.preferGpu = false;
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
            if (output.state.acceptedCount == 0) {
                output.state.status = "failed";
                output.state.warnings.push_back("No accepted Monte Carlo samples: non-finite objective or model domain.");
            }
            if (!output.result.confidenceAvailable)
                output.state.warnings.push_back("Zero noise variance: chi_squared=0 is a sentinel; confidence classes are unavailable.");
            output.result.voltage = output.voltage;
			output.result.idealCurrent = output.idealCurrent;
            output.result.currentSigma = output.sigma;
			output.result.noisyCurrent = output.noisyCurrent;
		};

		const FitData source = BuildFitData(characteristic, prefit, settings);
        if (const auto error = ValidateData(source); !error.empty())
            return MakeError(error);
        if (settings.monteCarloSamples <= 0 || !std::isfinite(settings.monteCarloNoisePercent) || settings.monteCarloNoisePercent < 0.0)
            return MakeError("Invalid Monte Carlo sample count or noise percent.");
        output.state.objective = ObjectiveSummary(source, characteristic);
        output.result.confidenceAvailable = settings.monteCarloNoisePercent > 0.0 || source.objective.sigma_abs > 0;
        output.voltage = source.voltage;
		output.idealCurrent = EvaluateCurve(*model, output.voltage, *characteristic.info.temperatureK, fitValues);
        for (double current : output.idealCurrent)
            if (!std::isfinite(current) || (source.objective.objective == JfmObjective::LogLegacy && (!(current > 0.0) || current == 1.0)))
                return MakeError("Model domain: non-finite or non-positive current (possibly negative junction voltage with fractional alpha). Choose a positive fit range and valid parameters.");
        const double factor = std::clamp(settings.monteCarloNoisePercent, 0.0, 100.0) / 100.0;
		output.sigma.resize(output.idealCurrent.size());
		for (std::size_t index = 0; index < output.idealCurrent.size(); ++index)
			output.sigma[index] = std::abs(output.idealCurrent[index] * factor) + source.objective.sigma_abs;

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
			request.use_lm = settings.fitMethod == "lm";
            request.layout = Layout(*model, *characteristic.info.temperatureK, prefitValues, fitValues, freeMask, settings, static_cast<int>(output.voltage.size()));
            request.layout.objective = source.objective.objective; request.layout.shift = source.objective.shift;
            request.layout.sigma_rel = source.objective.sigma_rel; request.layout.sigma_abs = source.objective.sigma_abs;
            request.layout.switch_k = source.objective.switch_k; request.sigma = output.sigma;
			request.nm_config.max_iter = settings.monteCarloMaxIterations;
			request.nm_config.reduced_chi2_tol = monteCarloSolverSettings.fitReducedChiSquaredTolerance;
			if (!request.use_lm && settings.autoCalibrateGpuSimplexIterations && settings.simplexCalibrationRuns > 0)
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
                    sample.converged = sample.accepted = gpuSample.converged != 0;
                    sample.solverConverged = sample.accepted && gpuSample.solver_converged != 0;
                    sample.flatDirections = gpuSample.flat_directions != 0;
                    sample.fitMethod = settings.fitMethod == "nm" ? "NM" : gpuSample.lm_fallback ? "LM→NM" : "LM";
                    sample.lmStatus = gpuSample.lm_status;
                    sample.objectiveChiSquared = gpuSample.objective_chi2;
                    sample.linearPoints = gpuSample.linear_points; sample.logPoints = gpuSample.log_points;
                    sample.stopReason = !sample.accepted ? "non_finite" : lm::stopReason(gpuSample.stop_status, sample.flatDirections);
                    output.state.convergedCount += sample.solverConverged ? 1 : 0;
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
		
		output.noisyCurrent.reserve(static_cast<std::size_t>(settings.monteCarloSamples) * output.idealCurrent.size());
		output.result.samples.reserve(settings.monteCarloSamples);
		for (int run = 0; run < settings.monteCarloSamples; ++run)
		{
			FitData noisyData;
			noisyData.voltage = output.voltage;
			noisyData.sigma = output.sigma;
            noisyData.objective = source.objective;
			noisyData.current.resize(output.idealCurrent.size());
			for (std::size_t point = 0; point < output.idealCurrent.size(); ++point)
			{
				const double noiseAmplitude = source.objective.objective == JfmObjective::LogLegacy ? output.idealCurrent[point]*factor : output.sigma[point];
				noisyData.current[point] = output.idealCurrent[point] + lm::normal(settings.monteCarloSeed + static_cast<uint64_t>(run)*0xD1B54A32D192ED03ULL, static_cast<int>(point)) * noiseAmplitude;
				output.noisyCurrent.push_back(noisyData.current[point]);
			}
			ParameterArray start = prefitValues;
            const SolverOutput solved = Solve(*model, noisyData, *characteristic.info.temperatureK, start, fitValues, freeMask, monteCarloSolverSettings, settings.monteCarloMaxIterations, settings.monteCarloSeed, run, settings.monteCarloStartPerturbationPercent);
			const double finalChiSquared = FinalMonteCarloChiSquared(*model, output.voltage, output.idealCurrent, *characteristic.info.temperatureK, solved.parameters, settings.monteCarloNoisePercent, noisyData);
			JFMMonteCarloSample sample;
			for (std::string_view name : model->parameters)
				sample.parameters.push_back(solved.parameters[IndexOf(name)]);
			sample.chiSquared = finalChiSquared;
			sample.reducedChiSquared = source.objective.objective == JfmObjective::LogLegacy ? finalChiSquared : finalChiSquared / std::max(1, output.result.freeParameterCount);
			sample.iterations = solved.result.iterations;
            sample.converged = sample.accepted = std::isfinite(finalChiSquared) && std::isfinite(solved.result.chi2_min);
            sample.solverConverged = sample.accepted && solved.result.converged;
            sample.flatDirections = solved.outcome.flat;
            sample.fitMethod = settings.fitMethod == "nm" ? "NM" : solved.outcome.fallback ? "LM→NM" : "LM";
            sample.lmStatus = solved.outcome.lm_status;
            sample.objectiveChiSquared = solved.result.chi2_min;
            const auto branches = ObjectiveSummary(noisyData, characteristic);
            sample.linearPoints = branches.linearPoints; sample.logPoints = branches.logPoints;
            sample.stopReason = !sample.accepted ? "non_finite" : solved.result.stop_reason;
            output.state.convergedCount += sample.solverConverged ? 1 : 0;
			output.state.acceptedCount += sample.converged ? 1 : 0;
			output.result.samples.push_back(std::move(sample));
		}
		output.state.totalMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		output.state.status = "completed";
		attachCurvesToResult();
		return output;
	}
} // namespace SST::Prototypes::JFM
