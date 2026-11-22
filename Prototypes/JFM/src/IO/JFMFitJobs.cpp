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
			const std::size_t points = computation.result.voltage.size();
			for (std::size_t sample = 0; sample < savedSamples; ++sample)
			{
                const auto curve=JFMMonteCarloNoisyCurve(computation.result,sample);
				for (std::size_t point = 0; point < points; ++point)
				{
					if (point >= curve.size())
						break;
					const double ideal = computation.result.idealCurrent[point];
					const double noisy = curve[point];
					noise << sample << '\t' << computation.result.voltage[point] << '\t' << ideal << '\t' << noisy << '\t' << noisy - ideal << '\t' << computation.result.currentSigma[point] << '\n';
				}
			}

			std::ofstream parameters(directory.Join("parameters.tsv").Native());
			if (!parameters)
				return StructuredError{ErrorCategory::IO, Severity::Error, "Monte Carlo parameters could not be saved.", "Cannot open parameters.tsv.", "Check the projects directory permissions.", "JFMMonteCarloJob", "jfm.mc.parameters"};
			parameters << std::setprecision(17) << "sample";
			for (const std::string &name : computation.result.parameterNames)
				parameters << '\t' << name;
			parameters << "\tchi_squared\treduced_chi_squared\titerations\tconverged\taccepted\tsolver_converged\tstop_reason\tconfidence_available\tfit_method\tflat_directions\tlm_status\tobjective_chi_squared\tlinear_points\tlog_points\n";
			for (std::size_t index = 0; index < savedSamples; ++index)
			{
				const JFMMonteCarloSample &sample = computation.result.samples[index];
				parameters << index;
				for (double value : sample.parameters)
					parameters << '\t' << value;
				parameters << '\t' << sample.chiSquared << '\t' << sample.reducedChiSquared << '\t' << sample.iterations << '\t' << (sample.solverConverged ? 1 : 0) << '\t' << (sample.accepted ? 1 : 0) << '\t' << (sample.solverConverged ? 1 : 0) << '\t' << sample.stopReason << '\t' << (computation.result.confidenceAvailable ? 1 : 0) << '\t' << sample.fitMethod << '\t' << (sample.flatDirections ? 1 : 0) << '\t' << sample.lmStatus << '\t' << sample.objectiveChiSquared << '\t' << sample.linearPoints << '\t' << sample.logPoints << '\n';
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

