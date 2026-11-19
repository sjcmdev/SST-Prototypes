#include "Core/dspch.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include <thread>
#include <algorithm>
#include <cmath>
#include <limits>

#include "IO/JFMFitJobs.hpp"

#include "Analysis/JFMFitEngine.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace SST::Prototypes::JFM
{
	namespace
	{
		template <typename Item>
		bool EnqueueItems(std::mutex &mutex, std::vector<Item> &pending, bool &scheduled, std::vector<Item> items)
		{
			std::scoped_lock lock(mutex);
			for (Item &item : items)
			{
				const auto existing = std::find_if(pending.begin(), pending.end(), [&item](const Item &candidate) { return candidate.ref == item.ref; });
				if (existing == pending.end())
					pending.push_back(std::move(item));
				else
					*existing = std::move(item);
			}
			if (scheduled || pending.empty())
				return false;
			scheduled = true;
			return true;
		}

		template <typename Item>
		std::optional<std::vector<Item>> TakeItems(std::mutex &mutex, std::vector<Item> &pending, bool &scheduled)
		{
			std::scoped_lock lock(mutex);
			if (pending.empty())
			{
				scheduled = false;
				return std::nullopt;
			}
			std::vector<Item> result = std::move(pending);
			pending.clear();
			return result;
		}

		template <typename Item>
		void ResetItems(std::mutex &mutex, std::vector<Item> &pending, bool &scheduled)
		{
			std::scoped_lock lock(mutex);
			pending.clear();
			scheduled = false;
		}

		Result<Path> PersistMonteCarlo(const JFMMonteCarloWorkItem &item, const JFMMonteCarloComputation &computation)
		{
			const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			Path directory = item.outputRoot
				/ std::filesystem::path("monte-carlo")
				/ std::filesystem::path("project-" + std::to_string(item.ref.project))
				/ std::filesystem::path("characteristic-" + std::to_string(item.ref.characteristic))
				/ std::filesystem::path("run-" + std::to_string(stamp) + "-seed-" + std::to_string(item.settings.monteCarloSeed));
			std::error_code error;
			(void)FileSystem::CreateDirectories(directory.Native(), error);
			if (error)
				return StructuredError{ErrorCategory::IO, Severity::Error, "Monte Carlo results could not be saved.", error.message(), "Check the projects directory permissions.", "JFMMonteCarloJob", "jfm.mc.write"};
			if (!item.settings.saveSamples)
				return directory;

			const std::size_t savedSamples = item.settings.savedSampleLimit > 0
				? std::min(computation.result.samples.size(), static_cast<std::size_t>(item.settings.savedSampleLimit))
				: computation.result.samples.size();

			std::ofstream noise(directory.Join("noise.tsv").Native());
			if (!noise)
				return StructuredError{ErrorCategory::IO, Severity::Error, "Monte Carlo noise could not be saved.", "Cannot open noise.tsv.", "Check the projects directory permissions.", "JFMMonteCarloJob", "jfm.mc.noise"};
			noise << std::setprecision(17) << "sample\tvoltage\tideal_current\tnoisy_current\tnoise\tsigma\n";
			const std::size_t points = computation.voltage.size();
			for (std::size_t sample = 0; sample < savedSamples; ++sample)
			{
				for (std::size_t point = 0; point < points; ++point)
				{
					const std::size_t offset = sample * points + point;
					if (offset >= computation.noisyCurrent.size())
						break;
					const double ideal = computation.idealCurrent[point];
					const double noisy = computation.noisyCurrent[offset];
					noise << sample << '\t' << computation.voltage[point] << '\t' << ideal << '\t' << noisy << '\t' << noisy - ideal << '\t' << computation.sigma[point] << '\n';
				}
			}

			std::ofstream parameters(directory.Join("parameters.tsv").Native());
			if (!parameters)
				return StructuredError{ErrorCategory::IO, Severity::Error, "Monte Carlo parameters could not be saved.", "Cannot open parameters.tsv.", "Check the projects directory permissions.", "JFMMonteCarloJob", "jfm.mc.parameters"};
			parameters << std::setprecision(17) << "sample";
			for (const std::string &name : computation.result.parameterNames)
				parameters << '\t' << name;
			parameters << "\tchi_squared\treduced_chi_squared\titerations\tconverged\taccepted\tsolver_converged\tstop_reason\tconfidence_available\tfit_method\tflat_directions\tlm_status\tobjective_chi_squared\n";
			for (std::size_t index = 0; index < savedSamples; ++index)
			{
				const JFMMonteCarloSample &sample = computation.result.samples[index];
				parameters << index;
				for (double value : sample.parameters)
					parameters << '\t' << value;
				parameters << '\t' << sample.chiSquared << '\t' << sample.reducedChiSquared << '\t' << sample.iterations << '\t' << (sample.solverConverged ? 1 : 0) << '\t' << (sample.accepted ? 1 : 0) << '\t' << (sample.solverConverged ? 1 : 0) << '\t' << sample.stopReason << '\t' << (computation.result.confidenceAvailable ? 1 : 0) << '\t' << sample.fitMethod << '\t' << (sample.flatDirections ? 1 : 0) << '\t' << sample.lmStatus << '\t' << sample.objectiveChiSquared << '\n';
			}
			if (!noise || !parameters)
				return StructuredError{ErrorCategory::IO, Severity::Error, "Monte Carlo results could not be saved.", "Writing the result files failed.", "Check available disk space.", "JFMMonteCarloJob", "jfm.mc.write"};
			return directory;
		}
	} // namespace

	bool JFMFitWorkQueue::Enqueue(std::vector<JFMFitWorkItem> items) { return EnqueueItems(m_Mutex, m_Pending, m_JobScheduled, std::move(items)); }
	std::optional<std::vector<JFMFitWorkItem>> JFMFitWorkQueue::TakeNext() { return TakeItems(m_Mutex, m_Pending, m_JobScheduled); }
	void JFMFitWorkQueue::Reset() { ResetItems(m_Mutex, m_Pending, m_JobScheduled); }

	JFMFitJob::JFMFitJob(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMFitWorkQueue> queue)
		: m_EventBus(std::move(eventBus)), m_Store(std::move(store)), m_Queue(std::move(queue))
	{
	}

	std::string JFMFitJob::GetName() const { return "Fit JFM Characteristics"; }
	std::string JFMFitJob::GetType() const { return "jfm.fit"; }

	void JFMFitJob::Execute(JobContext &context)
	{
		context.SetStage("JFM fit");
		try
		{
			while (m_Queue != nullptr)
			{
				auto items = m_Queue->TakeNext();
				if (!items)
					break;
				for (std::size_t index = 0; index < items->size(); ++index)
				{
					context.ThrowIfCancellationRequested();
					const JFMFitWorkItem &item = (*items)[index];
					context.SetProgress(static_cast<float>(index), static_cast<float>(items->size()));
					context.SetMessage(item.characteristic.info.name);
					if (m_Store == nullptr || !m_Store->IsFitGenerationCurrent(item.ref, item.generation))
						continue;
					auto result = RunJFMSimplexFit(item.characteristic, item.settings, item.useTunedStart);
					if (m_EventBus == nullptr)
						continue;
					if (result)
						m_EventBus->Queue(FitCompleted{item.ref, item.generation, std::move(result->state), std::move(result->curve)});
					else
						m_EventBus->Queue(FitFailed{item.ref, item.generation, result.Error().technicalDetails});
				}
				context.SetProgress(static_cast<float>(items->size()), static_cast<float>(items->size()));
			}
		}
		catch (...)
		{
			if (m_Queue != nullptr)
				m_Queue->Reset();
			throw;
		}
	}

	bool JFMMonteCarloWorkQueue::Enqueue(std::vector<JFMMonteCarloWorkItem> items) { return EnqueueItems(m_Mutex, m_Pending, m_JobScheduled, std::move(items)); }
	std::optional<std::vector<JFMMonteCarloWorkItem>> JFMMonteCarloWorkQueue::TakeNext() { return TakeItems(m_Mutex, m_Pending, m_JobScheduled); }
	void JFMMonteCarloWorkQueue::Reset() { ResetItems(m_Mutex, m_Pending, m_JobScheduled); }

	JFMMonteCarloJob::JFMMonteCarloJob(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMMonteCarloWorkQueue> queue)
		: m_EventBus(std::move(eventBus)), m_Store(std::move(store)), m_Queue(std::move(queue))
	{
	}

	std::string JFMMonteCarloJob::GetName() const { return "Monte Carlo JFM Fits"; }
	std::string JFMMonteCarloJob::GetType() const { return "jfm.monte_carlo"; }

	void JFMMonteCarloJob::Execute(JobContext &context)
	{
		context.SetStage("JFM Monte Carlo");
		try
		{
			while (m_Queue != nullptr)
			{
				auto items = m_Queue->TakeNext();
				if (!items)
					break;
				for (std::size_t index = 0; index < items->size(); ++index)
				{
					context.ThrowIfCancellationRequested();
					const JFMMonteCarloWorkItem &item = (*items)[index];
					context.SetProgress(static_cast<float>(index), static_cast<float>(items->size()));
					context.SetMessage(item.characteristic.info.name);
					if (m_Store == nullptr || !m_Store->IsMonteCarloGenerationCurrent(item.ref, item.generation))
						continue;
					auto result = RunJFMMonteCarlo(item.characteristic, item.prefit, item.fit, item.settings);
                    if (!result || result->state.acceptedCount == 0)
                    {
                        if (m_EventBus != nullptr)
                            m_EventBus->Queue(MonteCarloFailed{item.ref, item.generation, !result ? result.Error().technicalDetails : "No accepted Monte Carlo samples: non-finite objective or model domain."});
						continue;
					}
					auto outputPath = PersistMonteCarlo(item, *result);
					if (!outputPath)
					{
						if (m_EventBus != nullptr)
							m_EventBus->Queue(MonteCarloFailed{item.ref, item.generation, outputPath.Error().technicalDetails});
						continue;
					}
					result->state.outputPath = std::move(*outputPath);
					if (m_EventBus != nullptr)
						m_EventBus->Queue(MonteCarloCompleted{item.ref, item.generation, std::move(result->state), std::move(result->result)});
				}
				context.SetProgress(static_cast<float>(items->size()), static_cast<float>(items->size()));
			}
		}
		catch (...)
		{
			if (m_Queue != nullptr)
				m_Queue->Reset();
			throw;
		}
	}
} // namespace SST::Prototypes::JFM


namespace SST::Prototypes::JFM {
namespace {
WeakRef<CommandRegistry> fitCommands;
WeakRef<JobSystem> fitJobs;
Ref<JobSystem> FitJobs() {
    if (auto jobs=fitJobs.lock()) return jobs;
    static auto headless=CreateRef<JobSystem>(Ref<EventBus>{},1);
    return headless;
}
Ref<CommandRegistry> FitCommands() {
    if (auto commands=fitCommands.lock()) return commands;
    static auto headless=CreateRef<CommandRegistry>();
    return headless;
}
class SubmitFitCommand final : public ICommand {
    JobSystem &jobs;
    Ref<IJob> job;
    Ref<JobId> id;
public:
    SubmitFitCommand(JobSystem &j, Ref<IJob> work, Ref<JobId> result) : jobs(j), job(std::move(work)), id(std::move(result)) {}
    Result<void> Execute(CommandContext &) override { *id = jobs.Submit(job); return {}; }
    std::string Description() const override { return "Fit JFM characteristic"; }
};
}
void ConfigureJFMFitServices(WeakRef<CommandRegistry> commands, WeakRef<JobSystem> jobs) {
    fitCommands=std::move(commands); fitJobs=std::move(jobs);
}
nlohmann::json RunJFMFitRequest(const nlohmann::json &request) {
    using Json = nlohmann::json;
    try {
        const std::string method = request.value("method", "lm");
        if (method != "lm" && method != "nm") throw std::invalid_argument("method must be lm or nm");
        auto data = CreateRef<JFMCharacteristicData>();
        data->voltage = request.at("voltage_V").get<std::vector<double>>();
        data->current = request.at("current_A").get<std::vector<double>>();
        if (data->voltage.size() != data->current.size() || data->voltage.empty())
            throw std::invalid_argument("voltage_V and current_A require equal nonzero lengths");
        const double temperature = request.at("temperature_K");
        if (!std::isfinite(temperature) || temperature <= 0) throw std::invalid_argument("temperature_K must be positive");
        JFMCharacteristicRecord record;
        record.info.name = request.value("name", "Headless fit"); record.info.temperatureK = temperature; record.data = data;
        record.info.prefit = JFMPrefitState{};
        auto &pre = *record.info.prefit;
        pre.modelId = request.at("model_id"); pre.status = "completed";
        const auto &names = JFMModelParameterNames(pre.modelId);
        if (names.empty()) throw std::invalid_argument("Unknown model_id");
        for (auto name : names) {
            const auto key = std::string(name);
            double value = request.at("parameters").at(key);
            if (!std::isfinite(value)) throw std::invalid_argument("Non-finite parameter: "+key);
            pre.parameters[key] = {value, std::nullopt, "headless", {}};
        }
        auto limits = std::minmax_element(data->voltage.begin(), data->voltage.end());
        const auto range = request.value("fit_range_V", std::vector<double>{*limits.first, *limits.second});
        if (range.size() != 2) throw std::invalid_argument("fit_range_V requires two values");
        pre.fitRange = {0, data->voltage.size(), range[0], range[1], "headless"};
        JFMFitMonteCarloSettings settings;
        settings.fitMethod = method; settings.preferGpu = request.value("backend", "cpu") == "cuda";
        if (request.value("backend", "cpu") != "cpu" && request.value("backend", "cpu") != "cuda")
            throw std::invalid_argument("backend must be cpu or cuda");
        settings.fitMaxIterations = settings.monteCarloMaxIterations = request.value("max_iterations", 2500);
        if (settings.fitMaxIterations < 1) throw std::invalid_argument("max_iterations must be positive");
        settings.monteCarloSamples = request.value("samples", 0);
        if (settings.monteCarloSamples < 0) throw std::invalid_argument("samples must be nonnegative");
        settings.monteCarloNoisePercent = request.value("noise_percent", 1.0);
        settings.monteCarloStartPerturbationPercent = request.value("start_perturbation_percent", 5.0);
        const auto seed = request.value("seed", Json(42));
        if (!seed.is_number_integer() || seed.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("seed must be an integer in [0, 4294967295]");
        settings.monteCarloSeed = seed.get<std::uint32_t>();
        if (!std::isfinite(settings.monteCarloNoisePercent) || settings.monteCarloNoisePercent < 0 || settings.monteCarloNoisePercent > 100
            || !std::isfinite(settings.monteCarloStartPerturbationPercent) || settings.monteCarloStartPerturbationPercent < 0 || settings.monteCarloStartPerturbationPercent > 100)
            throw std::invalid_argument("noise_percent and start_perturbation_percent must be in [0, 100]");
        settings.autoCalibrateGpuSimplexIterations = false; settings.saveSamples = false;
        for (const auto &[name, bound] : request.value("bounds", Json::object()).items()) {
            if (std::find(names.begin(), names.end(), name) == names.end()) throw std::invalid_argument("Unknown bound parameter: "+name);
            const double lo = bound.at(0), hi = bound.at(1);
            if (!std::isfinite(lo) || !std::isfinite(hi) || lo >= hi) throw std::invalid_argument("Invalid bounds for "+name);
            settings.parameters[name] = {lo, hi, true};
        }
        const auto fixed = request.value("fixed_parameters", std::vector<std::string>{});
        for (const auto &name : fixed) {
            if (std::find(names.begin(), names.end(), name) == names.end()) throw std::invalid_argument("Unknown fixed parameter: "+name);
            if (!settings.parameters.contains(name)) {
                const double v = pre.parameters.at(name).value;
                settings.parameters[name] = {std::nextafter(v, -INFINITY), std::nextafter(v, INFINITY), false};
            } else settings.parameters[name].monteCarloFree = false;
        }
        auto store = CreateRef<JFMWorkspaceStore>(); const auto project = store->CreateProject("Headless fit");
        const auto ids = store->AddCharacteristicRecords(project, {record});
        const JFMCharacteristicRef ref{project, ids.at(0)};
        record = *store->FindCharacteristicRecord(ref);
        auto bus = CreateRef<EventBus>();
        Json result;
        auto failed = [&](const auto &event) { result = {{"ok", false}, {"error", {{"code", "jfm.fit.failed"}, {"message", event.error}}}}; };
        auto fitDone = bus->Subscribe<FitCompleted>([&](const FitCompleted &event) {
            Json parameters = Json::object();
            for (const auto &[name, estimate] : event.state.parameters) parameters[name] = estimate.value;
            result = {{"ok", true}, {"parameters", parameters}, {"chi_squared", event.state.chiSquared},
                {"reduced_chi_squared", event.state.reducedChiSquared}, {"iterations", event.state.iterations},
                {"fit_method", event.state.fitMethod}, {"accepted", event.state.accepted}, {"converged", event.state.converged},
                {"stop_reason", event.state.stopReason}, {"flat_directions", event.state.flatDirections},
                {"lm_status", event.state.lmStatus}, {"backend", event.state.backend}};
        });
        auto fitFailed = bus->Subscribe<FitFailed>(failed);
        auto mcDone = bus->Subscribe<MonteCarloCompleted>([&](const MonteCarloCompleted &event) {
            Json samples = Json::array();
            for (std::size_t i = 0; i < event.result.samples.size(); ++i) {
                const auto &s = event.result.samples[i];
                samples.push_back({{"sample_id", i}, {"parameters", s.parameters}, {"chi_squared", s.chiSquared},
                    {"objective_chi_squared", s.objectiveChiSquared}, {"accepted", s.accepted}, {"converged", s.solverConverged},
                    {"solver_converged", s.solverConverged}, {"stop_reason", s.stopReason}, {"fit_method", s.fitMethod},
                    {"lm_status", s.lmStatus}, {"flat_directions", s.flatDirections}, {"iterations", s.iterations}});
            }
            result = {{"ok", true}, {"samples", samples}, {"parameter_names", event.result.parameterNames},
                {"voltage_V", event.result.voltage}, {"ideal_current_A", event.result.idealCurrent},
                {"noisy_current_A", event.result.noisyCurrent}, {"total_ms", event.state.totalMilliseconds},
                {"accepted_count", event.state.acceptedCount}, {"converged_count", event.state.convergedCount},
                {"backend", event.state.backend}, {"warnings", event.state.warnings}};
        });
        auto mcFailed = bus->Subscribe<MonteCarloFailed>(failed);
        Ref<IJob> job;
        if (settings.monteCarloSamples == 0) {
            auto queue = CreateRef<JFMFitWorkQueue>();
            (void)queue->Enqueue({{ref, store->BeginFit(ref), record, settings, false}});
            job = CreateRef<JFMFitJob>(bus, store, queue);
        } else {
            JFMFitState fit; fit.modelId = pre.modelId; fit.status = "completed"; fit.parameters = pre.parameters;
            if (!store->ApplyFitResult(ref, store->BeginFit(ref), fit, {})) throw std::runtime_error("Cannot initialize Monte Carlo fit");
            auto queue = CreateRef<JFMMonteCarloWorkQueue>();
            const auto output = Path::FromResolved(std::filesystem::temp_directory_path()/"sst-jfm-headless");
            (void)queue->Enqueue({{ref, store->BeginMonteCarlo(ref), record, *record.info.prefit, fit, settings, output}});
            job = CreateRef<JFMMonteCarloJob>(bus, store, queue);
        }
        auto jobs=FitJobs(); auto commands=FitCommands(); auto id=CreateRef<JobId>(0);
        const CommandID command{"jfm.fit"};
        if (!commands->HasCommand(command)) {
            auto registered = commands->Register({command, "Fit JFM characteristic", "JFM", "Submit a prepared fit job.", {}, CommandFlags::HiddenFromPalette | CommandFlags::AsyncCapable},
                [](CommandContext &context) {
                    auto *jobs=context.TryGet<JobSystem *>("jobs"); auto *job=context.TryGet<Ref<IJob>>("job");
                    auto *id=context.TryGet<Ref<JobId>>("job_id");
                    if (!jobs || !*jobs || !job || !id) throw std::invalid_argument("Missing JFM fit context");
                    return CreateUnique<SubmitFitCommand>(**jobs,*job,*id);
                });
            if (!registered) throw std::runtime_error(registered.Error().technicalDetails);
        }
        CommandContext context; context.Set("jobs",jobs.get()); context.Set("job",job); context.Set("job_id",id);
        auto submitted = commands->Execute(command, std::move(context));
        if (!submitted || !*id) throw std::runtime_error("Fit submission failed");
        while (true) {
            const auto status = jobs->GetJob(*id);
            if (!status) throw std::runtime_error("Fit job disappeared");
            if (status->status == JobStatus::Completed) break;
            if (status->status == JobStatus::Failed || status->status == JobStatus::Cancelled)
                throw std::runtime_error(status->errorMessage);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bus->ProcessQueue();
        if (result.is_null()) throw std::runtime_error("Fit job produced no result");
        result["project_id"] = project; result["characteristic_id"] = ref.characteristic; result["job_id"] = *id;
        return result;
    } catch (const std::invalid_argument &e) {
        return {{"ok", false}, {"error", {{"code", "jfm.fit.validation"}, {"message", e.what()}}}};
    } catch (const Json::exception &e) {
        return {{"ok", false}, {"error", {{"code", "jfm.fit.validation"}, {"message", e.what()}}}};
    } catch (const std::exception &e) {
        return {{"ok", false}, {"error", {{"code", "jfm.fit.failed"}, {"message", e.what()}}}};
    }
}
}
