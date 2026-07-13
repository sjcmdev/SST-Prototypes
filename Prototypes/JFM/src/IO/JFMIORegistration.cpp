#include "Core/dspch.hpp"

#include "IO/JFMIORegistration.hpp"

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
		Path pythonRoot;
		std::vector<std::string> importWarnings;
		bool importRunning = false;
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

	void AddDirectoryImportGroups(const Path &directory, bool recursive, std::vector<JFMImportGroup> &groups)
	{
		for (auto &[folder, paths] : DataFilesByFolder(directory, recursive))
		{
			const Path folderPath = Path::FromResolved(folder);
			groups.push_back(JFMImportGroup{
				std::move(paths),
				{},
				ParentGroupPathForFolder(directory, folderPath),
				FolderName(folderPath)});
		}
	}

	std::vector<JFMImportGroup> BuildImportGroups(const ImportRequested &event)
	{
		std::vector<JFMImportGroup> groups;
		std::vector<Path> filePaths;
		for (const Path &path : event.paths)
		{
			if (IsDirectory(path))
				AddDirectoryImportGroups(path, event.recursive, groups);
			else if (IsRegularFile(path) || !FileSystem::Exists(path.Native()))
				filePaths.push_back(path);
		}

		if (!filePaths.empty())
			groups.push_back(JFMImportGroup{std::move(filePaths), event.targetSeriesId});
		return groups;
	}

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
		state->importWarnings.clear();
		if (state->store != nullptr)
		{
			state->store->SetImportRunning(true);
			state->store->SetStatusMessage("Import queued.");
		}
		std::vector<JFMImportGroup> groups = BuildImportGroups(event);
		if (groups.empty())
		{
			state->importRunning = false;
			if (state->store != nullptr)
			{
				state->store->SetImportRunning(false);
				state->store->SetStatusMessage("No JFM data files found for import.");
			}
			return;
		}

		auto job = CreateRef<JFMImportJob>(state->eventBus, std::move(groups), state->pythonRoot);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::Normal, JFMIOJobSource});
	}

	void HandleImportProgress(const Ref<JFMIOState> &state, const ImportProgress &event)
	{
		if (state->store != nullptr)
			state->store->SetStatusMessage("Import: " + event.message);
	}

	void HandleImportCompleted(const Ref<JFMIOState> &state, const ImportCompleted &event)
	{
		if (state->store != nullptr)
		{
			state->importWarnings.insert(state->importWarnings.end(), event.warnings.begin(), event.warnings.end());
			state->store->SetLastImportWarnings(state->importWarnings);
			std::string targetSeriesId = event.targetSeriesId;
			if (!event.characteristics.empty() && targetSeriesId.empty())
			{
				const std::string groupId = event.groupPath.empty() ? std::string{} : state->store->FindOrCreateGroupPath(event.groupPath);
				const JFMSeries series = state->store->AddSeries(event.seriesName.empty() ? "Imported Data" : event.seriesName, groupId);
				targetSeriesId = series.id;
			}
			if (!targetSeriesId.empty())
				state->store->SelectSeries(targetSeriesId);
			if (!event.characteristics.empty())
				state->store->AddCharacteristics(event.characteristics, targetSeriesId);
			if (event.finalBatch)
				state->store->SetImportRunning(false);
		}
		if (event.finalBatch)
		{
			state->importRunning = false;
			state->importWarnings.clear();
		}
	}

	void HandleImportFailed(const Ref<JFMIOState> &state, const ImportFailed &event)
	{
		state->importRunning = false;
		if (state->store != nullptr)
		{
			state->store->SetImportRunning(false);
			state->store->SetStatusMessage("Import failed: " + event.error);
			state->store->SetLastImportWarnings({event.error});
		}
		state->importWarnings.clear();
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
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, JFMIOJobSource});
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
			state->store->SetStatusMessage("Loading project: " + event.path.String());

		auto job = CreateRef<LoadProjectJob>(state->eventBus, event.path);
		state->eventBus->Queue(JobSubmitRequested{job, JobPriority::High, JFMIOJobSource});
	}

	void HandleProjectLoadCompleted(const Ref<JFMIOState> &state, const ProjectLoadCompleted &event)
	{
		if (state->store != nullptr)
		{
			state->store->ReplaceProject(event.project, event.path);
			state->store->SetStatusMessage("Project loaded; raw data materialization queued.");
		}

		auto job = CreateRef<MaterializeProjectDataJob>(state->eventBus, event.project, state->pythonRoot);
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

		const std::size_t updated = state->store->UpdateCharacteristicData(event.characteristics);
		state->store->SetLastImportWarnings(event.warnings);
		if (event.warnings.empty())
			state->store->SetStatusMessage("Project raw data materialized: " + std::to_string(updated) + " characteristic(s).");
		else
			state->store->SetStatusMessage("Project raw data materialized with warnings: " + std::to_string(updated) + " characteristic(s).");
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
		state->pythonRoot = std::move(context.pythonRoot);

		RegisterImportHandlers(ioLayer, state);
		RegisterExportHandlers(ioLayer, state);
		RegisterProjectSaveHandlers(ioLayer, state);
		RegisterProjectLoadHandlers(ioLayer, state);
	}
} // namespace SST::Prototypes::JFM
