#include "Core/dspch.hpp"

#include "IO/JFMIORegistration.hpp"

#include "Core/JobSystem/JobEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Assert.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/IOLayer.hpp"
#include "IO/JFMExportRawTsvJob.hpp"
#include "IO/JFMImportJob.hpp"
#include "IO/JFMProjectJobs.hpp"

#include <functional>

namespace SST::Prototypes::JFM
{

	struct JFMIOState
	{
		Ref<EventBus> eventBus;
		Ref<JFMWorkspaceStore> store;
		Ref<Notifier> notifier;
		Path pythonRoot;
		std::string targetSeriesId;
		bool importRunning = false;
	};

	void HandleImportRequested(const Ref<JFMIOState> &state, const ImportRequested &event)
	{
		if (event.paths.empty())
			return;

		if (state->importRunning)
		{
			state->eventBus->Queue(ImportFailed{"Another JFM import is already running."});
			return;
		}

		state->importRunning = true;
		if (state->store != nullptr)
		{
			state->store->SetImportRunning(true);
			state->store->SetStatusMessage("Import queued.");
		}
		state->targetSeriesId = event.targetSeriesId;

		auto job = CreateRef<JFMImportJob>(state->eventBus, event.paths, state->pythonRoot);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, "JFMIO"});
	}

	void HandleImportProgress(const Ref<JFMIOState> &state, const ImportProgress &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Import: " + event.message);
	}

	void HandleImportCompleted(const Ref<JFMIOState> &state, const ImportCompleted &event)
	{
		state->importRunning = false;
		if (state->store != nullptr)
		{
			if (!state->targetSeriesId.empty())
				state->store->SelectSeries(state->targetSeriesId);
			state->store->SetLastImportWarnings(event.warnings);
			state->store->AddImportedCharacteristics(event.characteristics);
		}
		state->targetSeriesId.clear();
	}

	void HandleImportFailed(const Ref<JFMIOState> &state, const ImportFailed &event)
	{
		state->importRunning = false;
		if (state->store != nullptr)
		{
			state->store->SetImportRunning(false);
			state->store->SetStatusMessage("Import failed: " + event.error);
		}
		state->targetSeriesId.clear();
		DS_LOG_WARN("JFM import failed: {}", event.error);
	}

	void RegisterImportHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ImportRequested>(std::bind_front(&HandleImportRequested, state));
		ioLayer.RegisterHandler<ImportProgress>(std::bind_front(&HandleImportProgress, state));
		ioLayer.RegisterHandler<ImportCompleted>(std::bind_front(&HandleImportCompleted, state));
		ioLayer.RegisterHandler<ImportFailed>(std::bind_front(&HandleImportFailed, state));
	}

	void HandleExportRawTsvRequested(const Ref<JFMIOState> &state, const ExportRawTsvRequested &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Raw TSV export queued: " + event.path.String());

		auto job = CreateRef<JFMExportRawTsvJob>(state->eventBus, event.path, event.characteristic);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, "JFMIO"});
	}

	void HandleExportRawTsvCompleted(const Ref<JFMIOState> &state, const ExportRawTsvCompleted &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Raw TSV exported: " + event.path.String());
	}

	void HandleExportRawTsvFailed(const Ref<JFMIOState> &state, const ExportRawTsvFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Raw TSV export failed: " + event.error);
		DS_LOG_WARN("JFM raw TSV export failed [{}]: {}", event.path.String(), event.error);
	}

	void RegisterExportHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ExportRawTsvRequested>(std::bind_front(&HandleExportRawTsvRequested, state));
		ioLayer.RegisterHandler<ExportRawTsvCompleted>(std::bind_front(&HandleExportRawTsvCompleted, state));
		ioLayer.RegisterHandler<ExportRawTsvFailed>(std::bind_front(&HandleExportRawTsvFailed, state));
	}

	void HandleProjectSaveRequested(const Ref<JFMIOState> &state, const ProjectSaveRequested &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Saving project: " + event.path.String());

		auto job = CreateRef<SaveProjectJob>(state->eventBus, event.path, event.snapshot);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, "JFMIO"});
	}

	void HandleProjectSaveCompleted(const Ref<JFMIOState> &state, const ProjectSaveCompleted &event)
	{
		if (state->store == nullptr)
			return;

		state->store->SetProjectPath(event.path);
		state->store->SetStatusMessage("Project saved: " + event.path.String());
	}

	void HandleProjectSaveFailed(const Ref<JFMIOState> &state, const ProjectSaveFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Project save failed: " + event.error);
		DS_LOG_WARN("JFM project save failed [{}]: {}", event.path.String(), event.error);
	}

	void RegisterProjectSaveHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ProjectSaveRequested>(std::bind_front(&HandleProjectSaveRequested, state));
		ioLayer.RegisterHandler<ProjectSaveCompleted>(std::bind_front(&HandleProjectSaveCompleted, state));
		ioLayer.RegisterHandler<ProjectSaveFailed>(std::bind_front(&HandleProjectSaveFailed, state));
	}

	void HandleProjectLoadRequested(const Ref<JFMIOState> &state, const ProjectLoadRequested &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Loading project: " + event.path.String());

		auto job = CreateRef<LoadProjectJob>(state->eventBus, event.path);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, "JFMIO"});
	}

	void HandleProjectLoadCompleted(const Ref<JFMIOState> &state, const ProjectLoadCompleted &event)
	{
		if (state->store != nullptr)
			state->store->ReplaceProject(event.project, event.path);
	}

	void HandleProjectLoadFailed(const Ref<JFMIOState> &state, const ProjectLoadFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Project load failed: " + event.error);
		DS_LOG_WARN("JFM project load failed [{}]: {}", event.path.String(), event.error);
	}

	void RegisterProjectLoadHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ProjectLoadRequested>(std::bind_front(&HandleProjectLoadRequested, state));
		ioLayer.RegisterHandler<ProjectLoadCompleted>(std::bind_front(&HandleProjectLoadCompleted, state));
		ioLayer.RegisterHandler<ProjectLoadFailed>(std::bind_front(&HandleProjectLoadFailed, state));
	}

	void RegisterJFMIOHandlers(IOLayer &ioLayer, JFMIORegistrationContext context)
	{
		DS_ASSERT(context.eventBus != nullptr, "JFM IO registration requires EventBus");

		auto state = CreateRef<JFMIOState>();
		state->eventBus = std::move(context.eventBus);
		state->store = std::move(context.store);
		state->notifier = std::move(context.notifier);
		state->pythonRoot = std::move(context.pythonRoot);

		RegisterImportHandlers(ioLayer, state);
		RegisterExportHandlers(ioLayer, state);
		RegisterProjectSaveHandlers(ioLayer, state);
		RegisterProjectLoadHandlers(ioLayer, state);
	}
} // namespace SST::Prototypes::JFM
