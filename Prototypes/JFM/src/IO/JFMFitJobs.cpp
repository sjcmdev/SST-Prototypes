#include "Core/dspch.hpp"
#include "Core/Diagnostics/OperationOutcome.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/Logging/Logger.hpp"
#include <thread>
#include <algorithm>
#include <cmath>
#include <limits>

#include "IO/JFMFitJobs.hpp"
#include "IO/JFMMonteCarloStorage.hpp"

#include "Analysis/JFMFitEngine.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/JobSystem/ParallelFor.hpp"
#include "Events/JFMEvents.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>

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

        Path MonteCarloDirectory(const JFMMonteCarloWorkItem& item) {
            static std::atomic_uint64_t sequence{0};
            const auto stamp=std::chrono::system_clock::now().time_since_epoch().count();
            return item.outputRoot / std::filesystem::path("monte-carlo")
                / std::filesystem::path("project-"+std::to_string(item.ref.project))
                / std::filesystem::path("characteristic-"+std::to_string(item.ref.characteristic))
                / std::filesystem::path("run-"+std::to_string(stamp)+"-"+std::to_string(++sequence));
        }

	} // namespace

    JFMMonteCarloSaveJob::JFMMonteCarloSaveJob(Ref<EventBus> bus, JFMCharacteristicRef ref, std::uint64_t generation,
        Path directory, Ref<const JFMMonteCarloResult> result, bool tsv, double noisePercent)
        : m_Bus(std::move(bus)), m_Ref(ref), m_Generation(generation), m_Directory(std::move(directory)),
          m_Result(std::move(result)), m_Tsv(tsv), m_NoisePercent(noisePercent) {}

    JFMMonteCarloSaveJob::~JFMMonteCarloSaveJob() {
        if (!m_Finished) Finish("cancelled", "Sample save cancelled before starting; the result remains available in the project.");
    }
    void JFMMonteCarloSaveJob::Finish(std::string status, std::string error) {
        m_Finished=true;
        if (!m_Bus) return;
        MonteCarloSamplesSaved event;
        event.ref=m_Ref; event.generation=m_Generation; event.path=m_Directory;
        event.status=std::move(status); event.error=std::move(error);
        // Explicit exports report through Core progress; they cannot replace primary storage metadata.
        if (!m_Tsv) m_Bus->Queue(std::move(event));
    }
    void JFMMonteCarloSaveJob::Execute(JobContext& context) {
        const auto started=std::chrono::steady_clock::now();
        context.SetStage(m_Tsv ? "Exporting MC samples" : "Saving MC samples");
        try {
            context.ThrowIfCancellationRequested();
            context.SetProgress(0,1);
            if (!m_Result) throw std::runtime_error("MC samples are unavailable.");
            Result<void> saved;
            if (m_Tsv) saved=ExportJFMMonteCarloTsv(m_Directory,*m_Result,m_NoisePercent,[&] { context.ThrowIfCancellationRequested(); });
            else {
                std::size_t checkpoints=0;
                const auto expected=std::max<std::size_t>(1,(m_Result->samples.size()+255)/256+14);
                saved=SaveJFMMonteCarloHdf5(m_Directory/Path("samples.h5"),*m_Result,[&] {
                    context.ThrowIfCancellationRequested();
                    context.SetProgress(static_cast<float>(std::min(++checkpoints,expected-1)),static_cast<float>(expected));
                });
            }
            if (!saved) throw OperationException(saved.Error());
            context.SetProgress(1,1); Finish("saved");
            DS_LOG_INFO("JFM MC storage N={} save_ms={:.3f} path={}",m_Result->samples.size(),
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),m_Directory.Utf8());
        } catch (const std::exception& e) {
            const bool cancelled=context.IsCancellationRequested();
            Finish(cancelled ? "cancelled" : "failed",std::string(e.what())+" Result retained; save the project or retry export.");
            if (cancelled) throw JobCancelledException(e.what());
            throw;
        }
    }

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
        std::atomic_size_t completed{0};
        std::mutex errorMutex;
        std::optional<StructuredError> firstError;
		try
		{
			while (m_Queue != nullptr)
			{
				auto items = m_Queue->TakeNext();
				if (!items)
					break;
				const auto fitOne = [&](std::size_t index)
				{
					context.ThrowIfCancellationRequested();
					const JFMFitWorkItem &item = (*items)[index];
					context.SetMessage(item.characteristic.info.name);
					if (m_Store == nullptr || !m_Store->IsFitGenerationCurrent(item.ref, item.generation))
						return;
					auto result = RunJFMSimplexFit(item.characteristic, item.settings, item.useTunedStart);
                    if (result) {
                        const bool accepted=result->state.accepted;
                        if (m_EventBus) m_EventBus->Queue(FitCompleted{item.ref,item.generation,std::move(result->state),std::move(result->curve)});
                        if (accepted) { ++completed; return; }
                        std::lock_guard guard(errorMutex);
                        if (!firstError) firstError=MakeOperationError(ErrorCategory::Numerical,"Fit produced an unaccepted solution.","JFM");
                    } else {
                        if (m_EventBus) m_EventBus->Queue(FitFailed{item.ref,item.generation,result.Error().technicalDetails});
                        std::lock_guard guard(errorMutex);
                        if (!firstError) firstError=result.Error();
                    }
				};
                const bool gpu = std::any_of(items->begin(), items->end(), [](const auto& item) { return item.settings.preferGpu; }) && JFMGpuAvailable();
                if (gpu) for (std::size_t index = 0; index < items->size(); ++index) fitOne(index);
                else (void)ParallelFor(context, items->size(), fitOne);
                context.ThrowIfCancellationRequested();
				context.SetProgress(static_cast<float>(items->size()), static_cast<float>(items->size()));
			}
            if (firstError) {
                if (completed>0) { auto error=MakeOperationError(ErrorCategory::Partial,firstError->technicalDetails,"JFM"); throw OperationException(std::move(error)); }
                throw OperationException(std::move(*firstError));
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

	JFMMonteCarloJob::~JFMMonteCarloJob()
	{
		// Core can cancel before Execute; release that queue and its running workspace state too.
		if (!m_Executed && m_Queue != nullptr)
			while (auto items = m_Queue->TakeNext())
				for (const auto &item : *items)
					if (m_Store != nullptr)
						(void)m_Store->FailMonteCarlo(item.ref, item.generation, "Cancelled before Monte Carlo started.", true);
	}

	void JFMMonteCarloJob::Execute(JobContext &context)
	{
		m_Executed = true;
		context.SetStage("JFM Monte Carlo");
		std::optional<StructuredError> firstError;
        std::size_t completed=0;
		std::optional<std::vector<JFMMonteCarloWorkItem>> items;
		std::size_t index = 0;
		try
		{
			while (m_Queue != nullptr)
			{
				items = m_Queue->TakeNext();
				if (!items)
					break;
				for (index = 0; index < items->size(); ++index)
				{
					context.ThrowIfCancellationRequested();
					const JFMMonteCarloWorkItem &item = (*items)[index];
					context.SetProgress(static_cast<float>(index), static_cast<float>(items->size()));
					context.SetMessage(item.characteristic.info.name);
					if (m_Store == nullptr || !m_Store->IsMonteCarloGenerationCurrent(item.ref, item.generation))
						continue;
                    JobContext sampleContext(
                        [&](float done, float total) {
                            context.SetProgress(static_cast<float>(index) + (total > 0 ? done/total : 0.f), static_cast<float>(items->size()));
                        }, {}, {},
                        [&](JobLogLevel level, const std::string& message) {
                            switch (level) {
                            case JobLogLevel::Debug: context.LogDebug(message); break;
                            case JobLogLevel::Info: context.LogInfo(message); break;
                            case JobLogLevel::Warning: context.LogWarning(message); break;
                            case JobLogLevel::Error: context.LogError(message); break;
                            }
                        },
                        [&] { return context.IsCancellationRequested(); },
                        [&](const Ref<IJob>& job, JobPriority priority) { return context.SubmitJob(job, priority); },
                        [&](JobId id) { return context.WaitForJob(id); }, context.GetWorkerCount(), context.GetStopToken());
					const auto started = std::chrono::steady_clock::now();
					auto result = RunJFMMonteCarlo(item.characteristic, item.prefit, item.fit, item.settings, &sampleContext);
					const auto solved = std::chrono::steady_clock::now();
                    if (!result || result->state.acceptedCount == 0)
                    {
						const std::string error = !result ? result.Error().technicalDetails : "No accepted Monte Carlo samples: non-finite objective or model domain.";
						if (!firstError) firstError = !result ? result.Error() : MakeOperationError(ErrorCategory::Numerical,error,"JFM");
                        if (m_EventBus != nullptr)
                            m_EventBus->Queue(MonteCarloFailed{item.ref, item.generation, error});
						continue;
					}
                    result->state.totalMilliseconds=std::chrono::duration<double,std::milli>(solved-started).count();
                    const auto runDirectory=MonteCarloDirectory(item);
                    result->state.sampleStorageId=runDirectory.filename().Utf8();
                    result->state.sampleSaveStatus=item.settings.saveSamples ? "pending" : "not_requested";
                    result->state.outputPath=item.settings.saveSamples ? runDirectory : Path{};
                    auto samples=CreateRef<const JFMMonteCarloResult>(std::move(result->result));
                    MonteCarloCompleted published;
                    published.ref=item.ref; published.generation=item.generation;
                    published.state=std::move(result->state); published.result=samples;
                    const auto directory=published.state.outputPath;
                    const auto backend=published.state.backend;
                    if (m_EventBus) m_EventBus->Queue(std::move(published));
                    DS_LOG_INFO("JFM MC [{}] backend={} N={} workers={} solve_ms={:.3f} publish_ms={:.3f}",
                        item.characteristic.info.name, backend, item.settings.monteCarloSamples, context.GetWorkerCount(),
                        std::chrono::duration<double,std::milli>(solved-started).count(),
                        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-solved).count());
                    ++completed;
                    if (item.settings.saveSamples) {
                        auto save=CreateRef<JFMMonteCarloSaveJob>(m_EventBus,item.ref,item.generation,directory,samples);
                        if (!context.SubmitJob(save,JobPriority::Low)) {
                            if (m_EventBus) m_EventBus->Queue(JobSubmitRequested{save,JobPriority::Low,"jfm.mc.save"});
                            else throw std::runtime_error("MC sample save could not be submitted; retain the result and save the project.");
                        }
                    }
				}
				context.SetProgress(static_cast<float>(items->size()), static_cast<float>(items->size()));
			}
		}
		catch (const std::exception &error)
		{
			const bool cancelled = context.IsCancellationRequested();
			context.SetMessage(error.what());
			do
			{
				if (items && m_EventBus != nullptr)
					for (; index < items->size(); ++index)
						m_EventBus->Queue(MonteCarloFailed{(*items)[index].ref, (*items)[index].generation, error.what(), cancelled});
				index = 0;
				items = m_Queue != nullptr ? m_Queue->TakeNext() : std::nullopt;
			} while (items);
			throw;
		}
		catch (...)
		{
			if (m_Queue != nullptr) m_Queue->Reset();
			throw;
		}
		if (firstError) {
            if (completed>0) firstError=MakeOperationError(ErrorCategory::Partial,OperationErrorSummary(*firstError),"JFM");
            context.SetMessage(OperationErrorSummary(*firstError));
            throw OperationException(std::move(*firstError));
        }
	}
} // namespace SST::Prototypes::JFM
