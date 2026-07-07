#include "Core/dspch.hpp"

#include "IO/JFMIORegistration.hpp"

#include "App/Application.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/IOLayer.hpp"
#include "IO/JFMExportRawTsvJob.hpp"
#include "IO/JFMImportJob.hpp"
#include "IO/JFMImportPaths.hpp"
#include "IO/JFMProjectJobs.hpp"

namespace SST::Prototypes::JFM
{
	namespace
	{
		struct JFMIOState
		{
			Ref<JFMWorkspaceStore> store;
			std::string targetSeriesId;
			bool importRunning = false;
		};
	}

	void RegisterJFMIOHandlers(IOLayer &ioLayer, Ref<JFMWorkspaceStore> store)
	{
		auto state = CreateRef<JFMIOState>();
		state->store = std::move(store);

		ioLayer.RegisterHandler<ImportRequested>([state](const ImportRequested &event) {
			if (event.paths.empty())
				return;

			if (state->importRunning)
			{
				Application::Get().GetEventBus().Queue(ImportFailed{"Another JFM import is already running."});
				return;
			}

			state->importRunning = true;
			if (state->store != nullptr)
			{
				state->store->SetImportRunning(true);
				state->store->SetStatusMessage("Import queued.");
			}
			state->targetSeriesId = event.targetSeriesId;

			const Path pythonRoot = ResolveJFMPythonRoot(Application::Get().GetConfig());
			auto job = CreateRef<JFMImportJob>(Application::Get().GetEventBusRef(), event.paths, pythonRoot);
			Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::Normal, "JFMIO"});
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

			auto job = CreateRef<JFMExportRawTsvJob>(Application::Get().GetEventBusRef(), event.path, event.characteristic);
			Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::Normal, "JFMIO"});
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

			auto job = CreateRef<SaveProjectJob>(Application::Get().GetEventBusRef(), event.path, event.snapshot);
			Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::High, "JFMIO"});
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

			auto job = CreateRef<LoadProjectJob>(Application::Get().GetEventBusRef(), event.path);
			Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::High, "JFMIO"});
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
