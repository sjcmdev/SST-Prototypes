#include "Core/dspch.hpp"

#include "IO/JFMIORegistration.hpp"

#include "App/JFMCommands.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Notifications/NotificationEvents.hpp"
#include "Core/Utils/Assert.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/IOLayer.hpp"
#include "IO/JFMExportRawTsvJob.hpp"
#include "IO/JFMFitJobs.hpp"
#include "IO/JFMImportJob.hpp"
#include "IO/JFMImportPreviewJob.hpp"
#include "IO/JFMProjectJobs.hpp"
#include "IO/JFMPrefitJob.hpp"

#include <algorithm>
#include <functional>

namespace SST::Prototypes::JFM
{
	constexpr const char *JFMIOJobSource = "jfm.io";

	struct JFMIOState
	{
		Ref<EventBus> eventBus;
		Ref<JFMWorkspaceStore> store;
		Ref<Notifier> notifier;
		Ref<JFMUserSettingsStore> settings;
		WeakRef<CommandRegistry> commandRegistry;
		Path pythonRoot;
		std::vector<std::string> importWarnings;
		std::vector<ImportCompleted> importBatches;
		ProjectHandle importProject = InvalidProjectHandle;
		JFMOperationId importOperation = InvalidJFMOperationId;
		JFMOperationId nextOperation = 1;
		std::string importTargetGroupId;
		Ref<JFMPrefitWorkQueue> prefitQueue = CreateRef<JFMPrefitWorkQueue>();
		Ref<JFMFitWorkQueue> fitQueue = CreateRef<JFMFitWorkQueue>();
		Ref<JFMMonteCarloWorkQueue> monteCarloQueue = CreateRef<JFMMonteCarloWorkQueue>();
		std::vector<JFMCharacteristicRef> pendingFits;
		std::vector<JFMCharacteristicRef> pendingMonteCarlo;
		bool importRunning = false;
		bool plotAfterImport = false;
		bool deferImportApply = false;
	};

	bool TakePending(std::vector<JFMCharacteristicRef> &pending, JFMCharacteristicRef ref)
	{
		const auto found = std::find(pending.begin(), pending.end(), ref);
		if (found == pending.end())
			return false;
		pending.erase(found);
		return true;
	}

	void AddPending(std::vector<JFMCharacteristicRef> &pending, JFMCharacteristicRef ref)
	{
		if (std::find(pending.begin(), pending.end(), ref) == pending.end())
			pending.push_back(ref);
	}

	void NotifyFailure(
		const Ref<JFMIOState> &state,
		NotificationCategory category,
		std::string title,
		std::string message)
	{
		if (state->eventBus == nullptr)
			return;

		Notification notification;
		notification.severity = NotificationSeverity::Error;
		notification.category = category;
		notification.title = std::move(title);
		notification.message = std::move(message);
		notification.source = "JFM IO";
		notification.timeoutMs = 12000;
		state->eventBus->Queue(NotificationRequestedEvent{std::move(notification)});
	}

	void HandleImportRequested(const Ref<JFMIOState> &state, const ImportRequested &event)
	{
		if (event.paths.empty())
			return;

		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->importRunning)
		{
			NotifyFailure(state, NotificationCategory::Import, "JFM import not started", "Another JFM import is already running.");
			DS_LOG_WARN("JFM import request [{}] rejected while operation [{}] is running", operation, state->importOperation);
			return;
		}

		state->importRunning = true;
		state->importProject = project;
		state->importOperation = operation;
		state->importTargetGroupId = event.targetGroupId;
		state->plotAfterImport = event.plotAfterImport;
		state->deferImportApply = event.deferApply;
		state->importWarnings.clear();
		state->importBatches.clear();
		if (state->store != nullptr)
		{
			state->store->SetImportRunning(project, true);
			state->store->SetStatusMessage(project, "Import queued.");
		}
		auto job = CreateRef<JFMImportJob>(
			state->eventBus,
			event.paths,
			event.recursive,
			event.placement,
			event.targetSeriesId,
			state->pythonRoot,
			event.options,
			project,
			operation);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
	}

	void HandleImportPreviewRequested(const Ref<JFMIOState> &state, const ImportPreviewRequested &event)
	{
		auto job = CreateRef<JFMImportPreviewJob>(
			state->eventBus,
			event.generation,
			event.paths,
			event.recursive,
			state->pythonRoot,
			event.options);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Low, JFMIOJobSource});
	}

	void HandleImportProgress(const Ref<JFMIOState> &state, const ImportProgress &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, "Import: " + event.message);
	}

	void HandleImportCompleted(const Ref<JFMIOState> &state, const ImportCompleted &event)
	{
		if (event.operation != state->importOperation || event.project != state->importProject)
			return;

		state->importWarnings.insert(state->importWarnings.end(), event.warnings.begin(), event.warnings.end());
		state->importBatches.push_back(event);
		if (!event.finalBatch)
			return;

		const ProjectHandle project = state->importProject;
		if (state->store != nullptr)
		{
			state->store->SetLastImportWarnings(project, state->importWarnings);
			state->store->SetImportRunning(project, false);
		}

		const bool hasCharacteristics = std::any_of(state->importBatches.begin(), state->importBatches.end(), [](const ImportCompleted &batch) {
			return !batch.characteristics.empty();
		});
		if (hasCharacteristics && !state->deferImportApply)
		{
			auto registry = state->commandRegistry.lock();
			if (registry != nullptr)
			{
				CommandContext context(ContextID{"jfm.import.complete"});
				context.SetSource("JFM import completion");
				context.Set("project", project);
				context.Set("batches", std::move(state->importBatches));
				context.Set("target_group_id", state->importTargetGroupId);
				context.Set("plot_after_import", state->plotAfterImport);
				auto result = registry->Execute(CommandID{JFMImportApplyCommandId}, std::move(context));
				if (!result)
					NotifyFailure(state, NotificationCategory::Import, "JFM import could not be applied", result.Error().technicalDetails);
			}
			else
			{
				NotifyFailure(state, NotificationCategory::Import, "JFM import could not be applied", "Command runtime is unavailable.");
			}
		}
		else if (hasCharacteristics && state->store != nullptr)
		{
			state->store->SetStatusMessage(project, "Import ready for data range.");
		}

		state->importRunning = false;
		state->plotAfterImport = false;
		state->deferImportApply = false;
		state->importWarnings.clear();
		state->importBatches.clear();
		state->importProject = InvalidProjectHandle;
		state->importOperation = InvalidJFMOperationId;
		state->importTargetGroupId.clear();
	}

	void HandleImportFailed(const Ref<JFMIOState> &state, const ImportFailed &event)
	{
		if (state->importRunning
			&& event.operation != InvalidJFMOperationId
			&& (event.operation != state->importOperation || event.project != state->importProject))
			return;
		state->importRunning = false;
		state->plotAfterImport = false;
		state->deferImportApply = false;
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->importProject : event.project;
		if (state->store != nullptr && project != InvalidProjectHandle)
		{
			state->store->SetImportRunning(project, false);
			state->store->SetStatusMessage(project, "Import failed: " + event.error);
			state->store->SetLastImportWarnings(project, {event.error});
		}
		state->importWarnings.clear();
		state->importBatches.clear();
		state->importProject = InvalidProjectHandle;
		state->importOperation = InvalidJFMOperationId;
		state->importTargetGroupId.clear();
		NotifyFailure(state, NotificationCategory::Import, "JFM import failed", event.error);
		DS_LOG_WARN("JFM import failed: {}", event.error);
	}

	void RegisterImportHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ImportRequested>(std::bind_front(&HandleImportRequested, state));
		ioLayer.RegisterHandler<ImportPreviewRequested>(std::bind_front(&HandleImportPreviewRequested, state));
		ioLayer.RegisterHandler<ImportProgress>(std::bind_front(&HandleImportProgress, state));
		ioLayer.RegisterHandler<ImportCompleted>(std::bind_front(&HandleImportCompleted, state));
		ioLayer.RegisterHandler<ImportFailed>(std::bind_front(&HandleImportFailed, state));
	}

	void HandlePrefitRequested(const Ref<JFMIOState> &state, const PrefitRequested &event)
	{
		if (state->store == nullptr || event.project == InvalidProjectHandle || event.ids.empty())
			return;
		std::vector<CharacteristicId> ids = event.ids;
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		std::vector<JFMPrefitWorkItem> items;
		items.reserve(ids.size());
		for (CharacteristicId id : ids)
		{
			const JFMCharacteristicRef ref{event.project, id};
			const std::uint64_t generation = state->store->BeginPrefit(ref);
			if (generation == 0)
				continue;
			auto characteristic = state->store->FindCharacteristicRecord(ref);
			if (characteristic)
				items.push_back(JFMPrefitWorkItem{ref, generation, std::move(*characteristic)});
		}
		if (items.empty())
			return;
		if (state->prefitQueue == nullptr || !state->prefitQueue->Enqueue(std::move(items)))
			return;
		auto job = CreateRef<JFMPrefitJob>(state->eventBus, state->store, state->pythonRoot, state->prefitQueue);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, "jfm.prefit"});
	}

	void HandlePrefitCompleted(const Ref<JFMIOState> &state, const PrefitCompleted &event)
	{
		if (state->store != nullptr
			&& state->store->ApplyPrefitResult(event.ref, event.generation, event.state, event.curve)
			&& TakePending(state->pendingFits, event.ref))
			state->eventBus->Queue(FitRequested{event.ref.project, {event.ref.characteristic}});
	}

	void HandleFitRequested(const Ref<JFMIOState> &state, const FitRequested &event)
	{
		if (state->store == nullptr || event.project == InvalidProjectHandle || event.ids.empty())
			return;
		std::vector<CharacteristicId> ids = event.ids;
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		std::vector<CharacteristicId> prefitIds;
		std::vector<JFMFitWorkItem> items;
		const JFMFitMonteCarloSettings settings = state->settings != nullptr
			? state->settings->GetSnapshot().fitMonteCarlo
			: JFMFitMonteCarloSettings{};
		for (CharacteristicId id : ids)
		{
			const JFMCharacteristicRef ref{event.project, id};
			const auto prefit = state->store->GetPrefitState(ref);
			if (!prefit || prefit->status != "completed")
			{
				AddPending(state->pendingFits, ref);
				if (!prefit || prefit->status != "running")
					prefitIds.push_back(id);
				continue;
			}
			const std::uint64_t generation = state->store->BeginFit(ref);
			if (generation == 0)
				continue;
			auto characteristic = state->store->FindCharacteristicRecord(ref);
			if (characteristic)
				items.push_back(JFMFitWorkItem{ref, generation, std::move(*characteristic), settings, event.useTunedStart});
		}
		if (!prefitIds.empty())
			state->eventBus->Queue(PrefitRequested{event.project, std::move(prefitIds)});
		if (items.empty() || state->fitQueue == nullptr || !state->fitQueue->Enqueue(std::move(items)))
			return;
		state->eventBus->Queue(JobSubmitRequested{CreateRef<JFMFitJob>(state->eventBus, state->store, state->fitQueue), JobPriority::High, "jfm.fit"});
	}

	void HandleFitCompleted(const Ref<JFMIOState> &state, const FitCompleted &event)
	{
		if (state->store != nullptr
			&& state->store->ApplyFitResult(event.ref, event.generation, event.state, event.curve)
			&& TakePending(state->pendingMonteCarlo, event.ref))
			state->eventBus->Queue(MonteCarloRequested{event.ref.project, {event.ref.characteristic}});
	}

	void HandleFitFailed(const Ref<JFMIOState> &state, const FitFailed &event)
	{
		if (state->store != nullptr && state->store->FailFit(event.ref, event.generation, event.error))
			DS_LOG_WARN("JFM fit failed [{}:{}]: {}", event.ref.project, event.ref.characteristic, event.error);
	}

	void HandleMonteCarloRequested(const Ref<JFMIOState> &state, const MonteCarloRequested &event)
	{
		if (state->store == nullptr || event.project == InvalidProjectHandle || event.ids.empty())
			return;
		std::vector<CharacteristicId> ids = event.ids;
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		std::vector<CharacteristicId> fitIds;
		std::vector<JFMMonteCarloWorkItem> items;
		const JFMFitMonteCarloSettings settings = state->settings != nullptr
			? state->settings->GetSnapshot().fitMonteCarlo
			: JFMFitMonteCarloSettings{};
		Path outputRoot = state->settings != nullptr ? state->settings->GetDefaultProjectsDirectory() : Path{};
		if (outputRoot.Empty())
			outputRoot = Path{FileSystem::CurrentPath()};
		for (CharacteristicId id : ids)
		{
			const JFMCharacteristicRef ref{event.project, id};
			const auto prefit = state->store->GetPrefitState(ref);
			const auto fit = state->store->GetFitState(ref);
			if (!prefit || prefit->status != "completed" || !fit || fit->status != "completed" || prefit->modelId != fit->modelId)
			{
				AddPending(state->pendingMonteCarlo, ref);
				if (!fit || fit->status != "running")
					fitIds.push_back(id);
				continue;
			}
			const std::uint64_t generation = state->store->BeginMonteCarlo(ref);
			if (generation == 0)
				continue;
			auto characteristic = state->store->FindCharacteristicRecord(ref);
			if (characteristic)
				items.push_back(JFMMonteCarloWorkItem{ref, generation, std::move(*characteristic), *prefit, *fit, settings, outputRoot});
		}
		if (!fitIds.empty())
			state->eventBus->Queue(FitRequested{event.project, std::move(fitIds)});
		if (items.empty() || state->monteCarloQueue == nullptr || !state->monteCarloQueue->Enqueue(std::move(items)))
			return;
		state->eventBus->Queue(JobSubmitRequested{CreateRef<JFMMonteCarloJob>(state->eventBus, state->store, state->monteCarloQueue), JobPriority::Normal, "jfm.monte_carlo"});
	}

	void HandleMonteCarloCompleted(const Ref<JFMIOState> &state, const MonteCarloCompleted &event)
	{
		if (state->store != nullptr)
			(void)state->store->ApplyMonteCarloResult(event.ref, event.generation, event.state, event.result);
	}

	void HandleMonteCarloFailed(const Ref<JFMIOState> &state, const MonteCarloFailed &event)
	{
		if (state->store != nullptr && state->store->FailMonteCarlo(event.ref, event.generation, event.error))
			DS_LOG_WARN("JFM Monte Carlo failed [{}:{}]: {}", event.ref.project, event.ref.characteristic, event.error);
	}

	void HandlePrefitFailed(const Ref<JFMIOState> &state, const PrefitFailed &event)
	{
		if (state->store != nullptr && state->store->FailPrefit(event.ref, event.generation, event.error))
			DS_LOG_WARN("JFM prefit failed [{}:{}]: {}", event.ref.project, event.ref.characteristic, event.error);
	}

	void RegisterPrefitHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<PrefitRequested>(std::bind_front(&HandlePrefitRequested, state));
		ioLayer.RegisterHandler<PrefitCompleted>(std::bind_front(&HandlePrefitCompleted, state));
		ioLayer.RegisterHandler<PrefitFailed>(std::bind_front(&HandlePrefitFailed, state));
		ioLayer.RegisterHandler<FitRequested>(std::bind_front(&HandleFitRequested, state));
		ioLayer.RegisterHandler<FitCompleted>(std::bind_front(&HandleFitCompleted, state));
		ioLayer.RegisterHandler<FitFailed>(std::bind_front(&HandleFitFailed, state));
		ioLayer.RegisterHandler<MonteCarloRequested>(std::bind_front(&HandleMonteCarloRequested, state));
		ioLayer.RegisterHandler<MonteCarloCompleted>(std::bind_front(&HandleMonteCarloCompleted, state));
		ioLayer.RegisterHandler<MonteCarloFailed>(std::bind_front(&HandleMonteCarloFailed, state));
	}

	void HandleExportRawTsvRequested(const Ref<JFMIOState> &state, const ExportRawTsvRequested &event)
	{
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->store != nullptr)
			state->store->SetStatusMessage(project, "Raw TSV export queued: " + event.path.Utf8());

		auto job = CreateRef<JFMExportRawTsvJob>(state->eventBus, event.path, event.characteristic, project, operation);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
	}

	void HandleExportRawTsvCompleted(const Ref<JFMIOState> &state, const ExportRawTsvCompleted &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, "Raw TSV exported: " + event.path.Utf8());
	}

	void HandleExportRawTsvFailed(const Ref<JFMIOState> &state, const ExportRawTsvFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, "Raw TSV export failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Export, "JFM export failed", event.error);
		DS_LOG_WARN("JFM raw TSV export failed [{}]: {}", event.path.Utf8(), event.error);
	}

	void HandleExportRawTsvBatchRequested(const Ref<JFMIOState> &state, const ExportRawTsvBatchRequested &event)
	{
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->store != nullptr)
			state->store->SetStatusMessage(project, "Raw TSV batch export queued: " + event.outputDirectory.Utf8());

		auto job = CreateRef<JFMExportRawTsvBatchJob>(state->eventBus, event.outputDirectory, event.items, project, operation);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
	}

	void HandleExportRawTsvBatchCompleted(const Ref<JFMIOState> &state, const ExportRawTsvBatchCompleted &event)
	{
		if (state->store != nullptr)
		{
			state->store->SetLastImportWarnings(event.project, event.warnings);
			state->store->SetStatusMessage(event.project, "Raw TSV batch exported: " + std::to_string(event.count) + " file(s).");
		}
	}

	void HandleExportRawTsvBatchFailed(const Ref<JFMIOState> &state, const ExportRawTsvBatchFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, "Raw TSV batch export failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Export, "JFM export failed", event.error);
		DS_LOG_WARN("JFM raw TSV batch export failed [{}]: {}", event.outputDirectory.Utf8(), event.error);
	}

	void HandleExportArrheniusTsvRequested(const Ref<JFMIOState> &state, const ExportArrheniusTsvRequested &event)
	{
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->store != nullptr)
			state->store->SetStatusMessage(project, "Arrhenius TSV export queued: " + event.path.Utf8());
		state->eventBus->Queue(JobSubmitRequested{CreateRef<JFMExportArrheniusTsvJob>(state->eventBus, event.path, event.characteristics, project, operation), JobPriority::Normal, JFMIOJobSource});
	}

	void HandleExportCharacteristicsFitTsvRequested(const Ref<JFMIOState> &state, const ExportCharacteristicsFitTsvRequested &event)
	{
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->store != nullptr)
			state->store->SetStatusMessage(project, "Characteristics + fit TSV export queued: " + event.path.Utf8());
		state->eventBus->Queue(JobSubmitRequested{CreateRef<JFMExportCharacteristicsFitTsvJob>(state->eventBus, event.path, event.characteristics, project, operation), JobPriority::Normal, JFMIOJobSource});
	}

	void HandleExportAnalysisTsvCompleted(const Ref<JFMIOState> &state, const ExportAnalysisTsvCompleted &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, event.kind + " exported: " + event.path.Utf8());
	}

	void HandleExportAnalysisTsvFailed(const Ref<JFMIOState> &state, const ExportAnalysisTsvFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, event.kind + " export failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Export, "JFM export failed", event.error);
		DS_LOG_WARN("JFM analysis TSV export failed [{}]: {}", event.path.Utf8(), event.error);
	}

	void RegisterExportHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ExportRawTsvRequested>(std::bind_front(&HandleExportRawTsvRequested, state));
		ioLayer.RegisterHandler<ExportRawTsvCompleted>(std::bind_front(&HandleExportRawTsvCompleted, state));
		ioLayer.RegisterHandler<ExportRawTsvFailed>(std::bind_front(&HandleExportRawTsvFailed, state));
		ioLayer.RegisterHandler<ExportRawTsvBatchRequested>(std::bind_front(&HandleExportRawTsvBatchRequested, state));
		ioLayer.RegisterHandler<ExportRawTsvBatchCompleted>(std::bind_front(&HandleExportRawTsvBatchCompleted, state));
		ioLayer.RegisterHandler<ExportRawTsvBatchFailed>(std::bind_front(&HandleExportRawTsvBatchFailed, state));
		ioLayer.RegisterHandler<ExportArrheniusTsvRequested>(std::bind_front(&HandleExportArrheniusTsvRequested, state));
		ioLayer.RegisterHandler<ExportCharacteristicsFitTsvRequested>(std::bind_front(&HandleExportCharacteristicsFitTsvRequested, state));
		ioLayer.RegisterHandler<ExportAnalysisTsvCompleted>(std::bind_front(&HandleExportAnalysisTsvCompleted, state));
		ioLayer.RegisterHandler<ExportAnalysisTsvFailed>(std::bind_front(&HandleExportAnalysisTsvFailed, state));
	}

	void HandleProjectSaveRequested(const Ref<JFMIOState> &state, const ProjectSaveRequested &event)
	{
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->store != nullptr)
			state->store->SetStatusMessage(project, "Saving project: " + event.path.Utf8());

		auto job = CreateRef<SaveProjectJob>(state->eventBus, event.path, event.snapshot, project, operation, event.revision);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, JFMIOJobSource});
	}

	void HandleProjectBundleSaveRequested(const Ref<JFMIOState> &state, const ProjectBundleSaveRequested &event)
	{
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->store != nullptr)
			state->store->SetStatusMessage(project, "Saving project bundle: " + event.path.Utf8());

		auto job = CreateRef<SaveProjectBundleJob>(state->eventBus, event.path, event.snapshot, project, operation, event.revision);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, JFMIOJobSource});
	}

	void HandleProjectSaveCompleted(const Ref<JFMIOState> &state, const ProjectSaveCompleted &event)
	{
		if (state->store == nullptr)
			return;

		state->store->MarkProjectSaved(event.project, event.path, event.revision);
		state->store->SetStatusMessage(event.project, "Project saved: " + event.path.Utf8());
		if (state->settings != nullptr)
			state->settings->AddRecentProject(event.path);
	}

	void HandleProjectSaveFailed(const Ref<JFMIOState> &state, const ProjectSaveFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, "Project save failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Project, "JFM save failed", event.error);
		DS_LOG_WARN("JFM project save failed [{}]: {}", event.path.Utf8(), event.error);
	}

	void HandleProjectBundleSaveCompleted(const Ref<JFMIOState> &state, const ProjectBundleSaveCompleted &event)
	{
		if (state->store == nullptr)
			return;
		state->store->MarkProjectSaved(event.project, event.path, event.revision);
		const std::string suffix = event.warnings.empty() ? std::string{} : " with warnings";
		state->store->SetStatusMessage(event.project, "Project bundle saved" + suffix + ": " + event.path.Utf8());
		if (state->settings != nullptr)
			state->settings->AddRecentProject(event.path);
	}

	void HandleProjectBundleSaveFailed(const Ref<JFMIOState> &state, const ProjectBundleSaveFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, "Project bundle save failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Project, "JFM bundle save failed", event.error);
		DS_LOG_WARN("JFM project bundle save failed [{}]: {}", event.path.Utf8(), event.error);
	}

	void RegisterProjectSaveHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ProjectSaveRequested>(std::bind_front(&HandleProjectSaveRequested, state));
		ioLayer.RegisterHandler<ProjectSaveCompleted>(std::bind_front(&HandleProjectSaveCompleted, state));
		ioLayer.RegisterHandler<ProjectSaveFailed>(std::bind_front(&HandleProjectSaveFailed, state));
		ioLayer.RegisterHandler<ProjectBundleSaveRequested>(std::bind_front(&HandleProjectBundleSaveRequested, state));
		ioLayer.RegisterHandler<ProjectBundleSaveCompleted>(std::bind_front(&HandleProjectBundleSaveCompleted, state));
		ioLayer.RegisterHandler<ProjectBundleSaveFailed>(std::bind_front(&HandleProjectBundleSaveFailed, state));
	}

	void HandleProjectLoadRequested(const Ref<JFMIOState> &state, const ProjectLoadRequested &event)
	{
		if (state->store != nullptr)
		{
			if (const auto existing = state->store->FindProjectByPath(event.path))
			{
				(void)state->store->SetActiveProject(*existing);
				state->store->SetStatusMessage(*existing, "Project already open.");
				return;
			}
			state->store->SetStatusMessage("Loading project: " + event.path.Utf8());
		}

		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		auto job = CreateRef<LoadProjectJob>(state->eventBus, event.path, operation);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, JFMIOJobSource});
	}

	void HandleProjectBundleLoadRequested(const Ref<JFMIOState> &state, const ProjectBundleLoadRequested &event)
	{
		if (state->store != nullptr)
		{
			if (const auto existing = state->store->FindProjectByPath(event.path))
			{
				(void)state->store->SetActiveProject(*existing);
				state->store->SetStatusMessage(*existing, "Project bundle already open.");
				return;
			}
			state->store->SetStatusMessage("Loading project bundle: " + event.path.Utf8());
		}

		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		auto job = CreateRef<LoadProjectBundleJob>(state->eventBus, event.path, operation);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, JFMIOJobSource});
	}

	void HandleProjectLoadCompleted(const Ref<JFMIOState> &state, const ProjectLoadCompleted &event)
	{
		ProjectHandle project = InvalidProjectHandle;
		if (state->store != nullptr)
		{
			project = state->store->AttachProject(event.project, event.path);
			if (state->settings != nullptr)
				state->store->EnsureDefaultScripts(state->settings->GetDefaultScriptsDirectory());
			state->store->SetStatusMessage(project, "Project loaded; raw data materialization queued.");
		}
		if (state->settings != nullptr)
			state->settings->AddRecentProject(event.path);

		auto job = CreateRef<MaterializeProjectDataJob>(state->eventBus, event.project, state->pythonRoot, project, event.operation);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
	}

	void HandleProjectBundleLoadCompleted(const Ref<JFMIOState> &state, const ProjectBundleLoadCompleted &event)
	{
		ProjectHandle project = InvalidProjectHandle;
		if (state->store != nullptr)
		{
			JFMProjectBundleSnapshot snapshot = event.snapshot;
			project = state->store->AttachProjectBundle(std::move(snapshot), event.path);
			if (state->settings != nullptr)
				state->store->EnsureDefaultScripts(state->settings->GetDefaultScriptsDirectory());
			state->store->SetLastImportWarnings(project, event.warnings);
			const std::string suffix = event.warnings.empty() ? std::string{} : " with warnings";
			state->store->SetStatusMessage(project, "Project bundle loaded" + suffix + ".");
		}
		if (state->settings != nullptr)
			state->settings->AddRecentProject(event.path);
	}

	void HandleProjectLoadFailed(const Ref<JFMIOState> &state, const ProjectLoadFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Project load failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Project, "JFM load failed", event.error);
		DS_LOG_WARN("JFM project load failed [{}]: {}", event.path.Utf8(), event.error);
	}

	void HandleProjectBundleLoadFailed(const Ref<JFMIOState> &state, const ProjectBundleLoadFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Project bundle load failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Project, "JFM bundle load failed", event.error);
		DS_LOG_WARN("JFM project bundle load failed [{}]: {}", event.path.Utf8(), event.error);
	}

	void HandleProjectDataMaterializationCompleted(const Ref<JFMIOState> &state, const ProjectDataMaterializationCompleted &event)
	{
		if (state->store == nullptr)
			return;

		const std::size_t updated = state->store->UpdateCharacteristicRecords(event.project, event.characteristics);
		state->store->SetLastImportWarnings(event.project, event.warnings);
		if (event.warnings.empty())
			state->store->SetStatusMessage(event.project, "Project raw data materialized: " + std::to_string(updated) + " characteristic(s).");
		else
			state->store->SetStatusMessage(event.project, "Project raw data materialized with warnings: " + std::to_string(updated) + " characteristic(s).");
		std::vector<CharacteristicId> ids;
		ids.reserve(event.characteristics.size());
		for (const JFMCharacteristicRecord &characteristic : event.characteristics)
			ids.push_back(characteristic.info.id);
        if (!ids.empty())
            state->eventBus->Queue(PlotRequested{event.project, {}, {{"selection", "Selection", {ids.front()}, {}, {}}}});
	}

	void RegisterProjectLoadHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ProjectLoadRequested>(std::bind_front(&HandleProjectLoadRequested, state));
		ioLayer.RegisterHandler<ProjectLoadCompleted>(std::bind_front(&HandleProjectLoadCompleted, state));
		ioLayer.RegisterHandler<ProjectLoadFailed>(std::bind_front(&HandleProjectLoadFailed, state));
		ioLayer.RegisterHandler<ProjectBundleLoadRequested>(std::bind_front(&HandleProjectBundleLoadRequested, state));
		ioLayer.RegisterHandler<ProjectBundleLoadCompleted>(std::bind_front(&HandleProjectBundleLoadCompleted, state));
		ioLayer.RegisterHandler<ProjectBundleLoadFailed>(std::bind_front(&HandleProjectBundleLoadFailed, state));
		ioLayer.RegisterHandler<ProjectDataMaterializationCompleted>(std::bind_front(&HandleProjectDataMaterializationCompleted, state));
	}

	void RegisterJFMIOHandlers(IOLayer &ioLayer, JFMIORegistrationContext context)
	{
		DS_ASSERT(context.eventBus != nullptr, "JFM IO registration requires EventBus");
		DS_ASSERT(context.store != nullptr, "JFM IO registration requires workspace");

		auto state = CreateRef<JFMIOState>();
		state->eventBus = std::move(context.eventBus);
		state->store = std::move(context.store);
		state->notifier = std::move(context.notifier);
		state->settings = std::move(context.settings);
		state->commandRegistry = std::move(context.commandRegistry);
		state->pythonRoot = std::move(context.pythonRoot);

		RegisterImportHandlers(ioLayer, state);
		RegisterPrefitHandlers(ioLayer, state);
		RegisterExportHandlers(ioLayer, state);
		RegisterProjectSaveHandlers(ioLayer, state);
		RegisterProjectLoadHandlers(ioLayer, state);
	}
} // namespace SST::Prototypes::JFM
