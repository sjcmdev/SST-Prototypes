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
#include "IO/JFMImportJob.hpp"
#include "IO/JFMProjectJobs.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <system_error>

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
		bool importRunning = false;
		bool plotAfterImport = false;
	};

	bool IsDirectory(const Path &path)
	{
		std::error_code error;
		return std::filesystem::is_directory(path.Native(), error);
	}

	bool IsRegularFile(const Path &path)
	{
		std::error_code error;
		return std::filesystem::is_regular_file(path.Native(), error);
	}

	bool IsJFMDataFile(const Path &path)
	{
		std::string extension = path.extension().String();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".dat" || extension == ".iv" || extension == ".txt" || extension == ".csv";
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

	std::string FolderName(const Path &path)
	{
		std::string name = path.filename().String();
		return name.empty() ? path.String() : name;
	}

	std::vector<std::string> ParentGroupPathForFolder(const Path &root, const Path &folder)
	{
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(folder.Native(), root.Native(), error);
		if (error || relative.empty() || relative == ".")
			return {};

		const std::filesystem::path parent = relative.parent_path();
		if (parent.empty())
			return {FolderName(root)};

		std::vector<std::string> names{FolderName(root)};
		for (const std::filesystem::path &part : parent)
			names.push_back(part.string());
		return names;
	}

	std::vector<Path> DirectDataFilesInDirectory(const Path &directory)
	{
		std::vector<Path> paths;
		std::error_code error;
		for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native(), error))
		{
			if (error)
				break;
			const Path path = Path::FromResolved(entry.path());
			std::error_code fileError;
			if (entry.is_regular_file(fileError) && IsJFMDataFile(path))
				paths.push_back(path);
		}
		return paths;
	}

	std::map<std::string, std::vector<Path>> DataFilesByFolder(const Path &directory, bool recursive)
	{
		std::map<std::string, std::vector<Path>> filesByFolder;
		if (!recursive)
		{
			std::vector<Path> paths = DirectDataFilesInDirectory(directory);
			if (!paths.empty())
				filesByFolder[directory.String()] = std::move(paths);
			return filesByFolder;
		}

		std::error_code error;
		const auto options = std::filesystem::directory_options::skip_permission_denied;
		for (const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator(directory.Native(), options, error))
		{
			if (error)
				break;
			const Path path = Path::FromResolved(entry.path());
			std::error_code fileError;
			if (entry.is_regular_file(fileError) && IsJFMDataFile(path))
				filesByFolder[path.parent_path().String()].push_back(path);
		}
		return filesByFolder;
	}

	void AddDirectoryImportGroups(const Path &directory, bool recursive, const std::string &targetSeriesId, std::vector<JFMImportGroup> &groups)
	{
		for (auto &[folder, paths] : DataFilesByFolder(directory, recursive))
		{
			const Path folderPath = Path::FromResolved(folder);
			groups.push_back(JFMImportGroup{
				std::move(paths),
				targetSeriesId,
				targetSeriesId.empty() ? ParentGroupPathForFolder(directory, folderPath) : std::vector<std::string>{},
				targetSeriesId.empty() ? FolderName(folderPath) : std::string{}});
		}
	}

	std::vector<JFMImportGroup> BuildImportGroups(const ImportRequested &event)
	{
		std::vector<JFMImportGroup> groups;
		std::vector<Path> filePaths;
		const std::string targetSeriesId = event.placement == JFMImportPlacement::ExistingSeries ? event.targetSeriesId : std::string{};
		const bool recursive = event.recursive || event.placement == JFMImportPlacement::PreserveFolderTree;
		for (const Path &path : event.paths)
		{
			if (IsDirectory(path))
				AddDirectoryImportGroups(path, recursive, targetSeriesId, groups);
			else if (IsRegularFile(path) || !FileSystem::Exists(path.Native()))
				filePaths.push_back(path);
		}

		if (!filePaths.empty())
		{
			const std::string seriesName = FolderName(filePaths.front().parent_path());
			groups.push_back(JFMImportGroup{std::move(filePaths), targetSeriesId, {}, seriesName.empty() ? "Series" : seriesName});
		}
		return groups;
	}

	void HandleImportRequested(const Ref<JFMIOState> &state, const ImportRequested &event)
	{
		if (event.paths.empty())
			return;

		if (state->importRunning)
		{
			state->eventBus->Queue(ImportFailed{"Another JFM import is already running.", event.project, event.operation});
			return;
		}

		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		state->importRunning = true;
		state->importProject = project;
		state->importOperation = operation;
		state->importTargetGroupId = event.targetGroupId;
		state->plotAfterImport = event.plotAfterImport;
		state->importWarnings.clear();
		state->importBatches.clear();
		if (state->store != nullptr)
		{
			state->store->SetImportRunning(project, true);
			state->store->SetStatusMessage(project, "Import queued.");
		}
		std::vector<JFMImportGroup> groups = BuildImportGroups(event);
		if (groups.empty())
		{
			state->importRunning = false;
			state->plotAfterImport = false;
			if (state->store != nullptr)
			{
				state->store->SetImportRunning(project, false);
				state->store->SetStatusMessage(project, "No JFM data files found for import.");
			}
			return;
		}

		auto job = CreateRef<JFMImportJob>(state->eventBus, std::move(groups), state->pythonRoot, event.options, project, operation);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
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
		if (hasCharacteristics)
		{
			auto registry = state->commandRegistry.lock();
			if (registry != nullptr)
			{
				std::vector<ImportCompleted> batches = std::move(state->importBatches);
				const std::string targetGroupId = state->importTargetGroupId;
				const bool plotAfterImport = state->plotAfterImport;
				Ref<EventBus> eventBus = state->eventBus;
				JFMProjectEditRequest request;
				request.project = project;
				request.description = "Import JFM data";
				request.apply = [batches = std::move(batches), targetGroupId, plotAfterImport, eventBus](JFMWorkspaceStore &workspace, ProjectHandle targetProject) mutable -> Result<void> {
					for (ImportCompleted &batch : batches)
					{
						if (batch.characteristics.empty())
							continue;
						std::string seriesId = batch.targetSeriesId;
						if (seriesId.empty())
						{
							const std::string groupId = batch.groupPath.empty()
								? targetGroupId
								: workspace.FindOrCreateGroupPath(targetProject, batch.groupPath, targetGroupId);
							seriesId = workspace.AddSeries(targetProject, batch.seriesName.empty() ? "Series" : batch.seriesName, groupId).id;
						}
						std::vector<CharacteristicId> added = workspace.AddCharacteristics(targetProject, std::move(batch.characteristics), seriesId);
						if (plotAfterImport && eventBus != nullptr && !added.empty())
							eventBus->Queue(PlotCharacteristicsRequested{std::move(added), batch.seriesName.empty() ? "Series" : batch.seriesName, targetProject});
					}
					workspace.SetStatusMessage(targetProject, "Import complete.");
					return {};
				};
				CommandContext context(ContextID{"jfm.import.complete"});
				context.SetSource("JFM import completion");
				context.Set("edit", std::move(request));
				auto result = registry->Execute(CommandID{JFMApplyProjectEditCommandId}, std::move(context));
				if (!result)
					NotifyFailure(state, NotificationCategory::Import, "JFM import could not be applied", result.Error().technicalDetails);
			}
			else
			{
				NotifyFailure(state, NotificationCategory::Import, "JFM import could not be applied", "Command runtime is unavailable.");
			}
		}

		state->importRunning = false;
		state->plotAfterImport = false;
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
		ioLayer.RegisterHandler<ImportProgress>(std::bind_front(&HandleImportProgress, state));
		ioLayer.RegisterHandler<ImportCompleted>(std::bind_front(&HandleImportCompleted, state));
		ioLayer.RegisterHandler<ImportFailed>(std::bind_front(&HandleImportFailed, state));
	}

	void HandleExportRawTsvRequested(const Ref<JFMIOState> &state, const ExportRawTsvRequested &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Raw TSV export queued: " + event.path.String());

		auto job = CreateRef<JFMExportRawTsvJob>(state->eventBus, event.path, event.characteristic);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
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
		NotifyFailure(state, NotificationCategory::Export, "JFM export failed", event.error);
		DS_LOG_WARN("JFM raw TSV export failed [{}]: {}", event.path.String(), event.error);
	}

	void HandleExportRawTsvBatchRequested(const Ref<JFMIOState> &state, const ExportRawTsvBatchRequested &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Raw TSV batch export queued: " + event.outputDirectory.String());

		auto job = CreateRef<JFMExportRawTsvBatchJob>(state->eventBus, event.outputDirectory, event.items);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
	}

	void HandleExportRawTsvBatchCompleted(const Ref<JFMIOState> &state, const ExportRawTsvBatchCompleted &event)
	{
		if (state->store != nullptr)
		{
			state->store->SetLastImportWarnings(event.warnings);
			state->store->SetStatusMessage("Raw TSV batch exported: " + std::to_string(event.count) + " file(s).");
		}
	}

	void HandleExportRawTsvBatchFailed(const Ref<JFMIOState> &state, const ExportRawTsvBatchFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Raw TSV batch export failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Export, "JFM export failed", event.error);
		DS_LOG_WARN("JFM raw TSV batch export failed [{}]: {}", event.outputDirectory.String(), event.error);
	}

	void RegisterExportHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ExportRawTsvRequested>(std::bind_front(&HandleExportRawTsvRequested, state));
		ioLayer.RegisterHandler<ExportRawTsvCompleted>(std::bind_front(&HandleExportRawTsvCompleted, state));
		ioLayer.RegisterHandler<ExportRawTsvFailed>(std::bind_front(&HandleExportRawTsvFailed, state));
		ioLayer.RegisterHandler<ExportRawTsvBatchRequested>(std::bind_front(&HandleExportRawTsvBatchRequested, state));
		ioLayer.RegisterHandler<ExportRawTsvBatchCompleted>(std::bind_front(&HandleExportRawTsvBatchCompleted, state));
		ioLayer.RegisterHandler<ExportRawTsvBatchFailed>(std::bind_front(&HandleExportRawTsvBatchFailed, state));
	}

	void HandleProjectSaveRequested(const Ref<JFMIOState> &state, const ProjectSaveRequested &event)
	{
		const ProjectHandle project = event.project == InvalidProjectHandle ? state->store->GetActiveProjectHandle() : event.project;
		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		if (state->store != nullptr)
			state->store->SetStatusMessage(project, "Saving project: " + event.path.String());

		auto job = CreateRef<SaveProjectJob>(state->eventBus, event.path, event.snapshot, project, operation, event.revision);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, JFMIOJobSource});
	}

	void HandleProjectSaveCompleted(const Ref<JFMIOState> &state, const ProjectSaveCompleted &event)
	{
		if (state->store == nullptr)
			return;

		state->store->MarkProjectSaved(event.project, event.path, event.revision);
		state->store->SetStatusMessage(event.project, "Project saved: " + event.path.String());
		if (state->settings != nullptr)
			state->settings->AddRecentProject(event.path);
	}

	void HandleProjectSaveFailed(const Ref<JFMIOState> &state, const ProjectSaveFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage(event.project, "Project save failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Project, "JFM save failed", event.error);
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
		{
			if (const auto existing = state->store->FindProjectByPath(event.path))
			{
				(void)state->store->SetActiveProject(*existing);
				state->store->SetStatusMessage(*existing, "Project already open.");
				return;
			}
			state->store->SetStatusMessage("Loading project: " + event.path.String());
		}

		const JFMOperationId operation = event.operation == InvalidJFMOperationId ? state->nextOperation++ : event.operation;
		auto job = CreateRef<LoadProjectJob>(state->eventBus, event.path, operation);
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

	void HandleProjectLoadFailed(const Ref<JFMIOState> &state, const ProjectLoadFailed &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Project load failed: " + event.error);
		NotifyFailure(state, NotificationCategory::Project, "JFM load failed", event.error);
		DS_LOG_WARN("JFM project load failed [{}]: {}", event.path.String(), event.error);
	}

	void HandleProjectDataMaterializationCompleted(const Ref<JFMIOState> &state, const ProjectDataMaterializationCompleted &event)
	{
		if (state->store == nullptr)
			return;

		const std::size_t updated = state->store->UpdateCharacteristicData(event.project, event.characteristics);
		state->store->SetLastImportWarnings(event.project, event.warnings);
		if (event.warnings.empty())
			state->store->SetStatusMessage(event.project, "Project raw data materialized: " + std::to_string(updated) + " characteristic(s).");
		else
			state->store->SetStatusMessage(event.project, "Project raw data materialized with warnings: " + std::to_string(updated) + " characteristic(s).");
	}

	void RegisterProjectLoadHandlers(IOLayer &ioLayer, const Ref<JFMIOState> &state)
	{
		ioLayer.RegisterHandler<ProjectLoadRequested>(std::bind_front(&HandleProjectLoadRequested, state));
		ioLayer.RegisterHandler<ProjectLoadCompleted>(std::bind_front(&HandleProjectLoadCompleted, state));
		ioLayer.RegisterHandler<ProjectLoadFailed>(std::bind_front(&HandleProjectLoadFailed, state));
		ioLayer.RegisterHandler<ProjectDataMaterializationCompleted>(std::bind_front(&HandleProjectDataMaterializationCompleted, state));
	}

	void RegisterJFMIOHandlers(IOLayer &ioLayer, JFMIORegistrationContext context)
	{
		DS_ASSERT(context.eventBus != nullptr, "JFM IO registration requires EventBus");

		auto state = CreateRef<JFMIOState>();
		state->eventBus = std::move(context.eventBus);
		state->store = std::move(context.store);
		state->notifier = std::move(context.notifier);
		state->settings = std::move(context.settings);
		state->commandRegistry = std::move(context.commandRegistry);
		state->pythonRoot = std::move(context.pythonRoot);

		RegisterImportHandlers(ioLayer, state);
		RegisterExportHandlers(ioLayer, state);
		RegisterProjectSaveHandlers(ioLayer, state);
		RegisterProjectLoadHandlers(ioLayer, state);
	}
} // namespace SST::Prototypes::JFM
