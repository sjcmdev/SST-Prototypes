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
	

	void RegisterJFMIOHandlers(IOLayer &ioLayer, JFMIORegistrationContext context)
	{
		DS_ASSERT(context.eventBus != nullptr, "JFM IO registration requires EventBus");

		auto state = CreateRef<JFMIOState>();
		state->eventBus = std::move(context.eventBus);
		state->store = std::move(context.store);
		state->notifier = std::move(context.notifier);
		state->pythonRoot = std::move(context.pythonRoot);

		ioLayer.RegisterHandler<ImportRequested>([state](const ImportRequested &event) {
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
		});

		ioLayer.RegisterHandler<ImportProgress>([state](const ImportProgress &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Import: " + event.message);
		});

		ioLayer.RegisterHandler<ImportCompleted>([state](const ImportCompleted &event) {
			state->importRunning = false;
			if (state->store != nullptr)
			{
				if (!state->targetSeriesId.empty())
					state->store->SelectSeries(state->targetSeriesId);
				state->store->SetLastImportWarnings(event.warnings);
				state->store->AddImportedCharacteristics(event.characteristics);
			}
			state->targetSeriesId.clear();
		});

		ioLayer.RegisterHandler<ImportFailed>([state](const ImportFailed &event) {
			state->importRunning = false;
			if (state->store != nullptr)
			{
				state->store->SetImportRunning(false);
				state->store->SetStatusMessage("Import failed: " + event.error);
			}
			state->targetSeriesId.clear();
			DS_LOG_WARN("JFM import failed: {}", event.error);
		});

		ioLayer.RegisterHandler<ExportRawTsvRequested>([state](const ExportRawTsvRequested &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Raw TSV export queued: " + event.path.String());

			auto job = CreateRef<JFMExportRawTsvJob>(state->eventBus, event.path, event.characteristic);
			state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, "JFMIO"});
		});

		ioLayer.RegisterHandler<ExportRawTsvCompleted>([state](const ExportRawTsvCompleted &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Raw TSV exported: " + event.path.String());
		});

		ioLayer.RegisterHandler<ExportRawTsvFailed>([state](const ExportRawTsvFailed &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Raw TSV export failed: " + event.error);
			DS_LOG_WARN("JFM raw TSV export failed [{}]: {}", event.path.String(), event.error);
		});

		ioLayer.RegisterHandler<ProjectSaveRequested>([state](const ProjectSaveRequested &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Saving project: " + event.path.String());

			auto job = CreateRef<SaveProjectJob>(state->eventBus, event.path, event.snapshot);
			state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, "JFMIO"});
		});

		ioLayer.RegisterHandler<ProjectSaveCompleted>([state](const ProjectSaveCompleted &event) {
			if (state->store == nullptr)
				return;

			state->store->SetProjectPath(event.path);
			state->store->SetStatusMessage("Project saved: " + event.path.String());
		});

		ioLayer.RegisterHandler<ProjectSaveFailed>([state](const ProjectSaveFailed &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Project save failed: " + event.error);
			DS_LOG_WARN("JFM project save failed [{}]: {}", event.path.String(), event.error);
		});

		ioLayer.RegisterHandler<ProjectLoadRequested>([state](const ProjectLoadRequested &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Loading project: " + event.path.String());

			auto job = CreateRef<LoadProjectJob>(state->eventBus, event.path);
			state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, "JFMIO"});
		});

		ioLayer.RegisterHandler<ProjectLoadCompleted>([state](const ProjectLoadCompleted &event) {
			if (state->store != nullptr)
				state->store->ReplaceProject(event.project, event.path);
		});

		ioLayer.RegisterHandler<ProjectLoadFailed>([state](const ProjectLoadFailed &event) {
			if (state->store != nullptr)
				state->store->SetStatusMessage("Project load failed: " + event.error);
			DS_LOG_WARN("JFM project load failed [{}]: {}", event.path.String(), event.error);
		});
	}
} // namespace SST::Prototypes::JFM
