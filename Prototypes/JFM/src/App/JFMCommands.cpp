#include "Core/dspch.hpp"

#include "App/JFMCommands.hpp"

#include "Core/Platform/NativeFileDialog.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <set>
#include <system_error>
#include <tuple>

namespace SST::Prototypes::JFM
{

	const CommandID NewProjectCommand{"jfm.project.new"};
	const CommandID OpenProjectCommand{"jfm.project.open"};
	const CommandID SaveProjectCommand{"jfm.project.save"};
	const CommandID SaveProjectAsCommand{"jfm.project.save_as"};
	const CommandID OpenProjectBundleCommand{JFMOpenProjectBundleCommandId};
	const CommandID SaveProjectBundleAsCommand{JFMSaveProjectBundleAsCommandId};
	const CommandID OpenNewProjectWindowCommand{"jfm.project.open_new_window"};
	const CommandID ImportFilesCommand{"jfm.import.files"};
	const CommandID ImportDirectoryCommand{"jfm.import.directory"};
	const CommandID ImportDirectoriesCommand{"jfm.import.directories"};
	const CommandID ImportDirectoriesRecursiveCommand{"jfm.import.directories_recursive"};
	const CommandID ExportRawTsvCommand{"jfm.export.raw_tsv"};
	const CommandID ExportSeriesRawTsvCommand{"jfm.export.series_raw_tsv"};
	const CommandID ExportGroupRawTsvCommand{"jfm.export.group_raw_tsv"};
	const CommandID ExportArrheniusTsvCommand{JFMExportArrheniusTsvCommandId};
	const CommandID ExportCharacteristicsFitTsvCommand{JFMExportCharacteristicsFitTsvCommandId};

	constexpr NativeFileDialogFilter JFMProjectFilter{"JFM Project", "jfm.yaml,yaml"};
	constexpr NativeFileDialogFilter JFMProjectBundleFilter{"JFM Project Bundle", "jfm"};
	constexpr NativeFileDialogFilter JFMDataFilter{"JFM Data", "dat,iv,txt,csv"};
	constexpr NativeFileDialogFilter TsvFilter{"TSV", "tsv"};
	StructuredError makeCommandError(std::string message, std::string details);

	class LambdaCommand final : public ICommand
	{
	public:
		using Handler = std::function<Result<void>(CommandContext &)>;

		LambdaCommand(std::string description, Handler handler)
			: m_Description(std::move(description)), m_Handler(std::move(handler))
		{
		}

		Result<void> Execute(CommandContext &context) override
		{
			return m_Handler(context);
		}

		std::string Description() const override
		{
			return m_Description;
		}

	private:
		std::string m_Description;
		Handler m_Handler;
	};

	Unique<ICommand> CreateLambdaCommand(const std::string &description, LambdaCommand::Handler handler, CommandContext &)
	{
		return CreateUnique<LambdaCommand>(description, std::move(handler));
	}

	Unique<ICommand> CreateProjectEditCommand(const Ref<JFMWorkspaceStore> &workspace, CommandContext &context)
	{
		JFMProjectEditRequest request;
		if (JFMProjectEditRequest *value = context.TryGet<JFMProjectEditRequest>("edit"))
			request = std::move(*value);
		return CreateJFMProjectEditCommand(workspace, std::move(request));
	}

	void queueSave(EventBus &eventBus, const Ref<JFMWorkspaceStore> &workspace, ProjectHandle project, const Path &path);
	void queueBundleSave(EventBus &eventBus, const Ref<JFMWorkspaceStore> &workspace, ProjectHandle project, const Path &path);

	StructuredError makeCommandError(std::string message, std::string details)
	{
		return StructuredError{
			ErrorCategory::Runtime,
			Severity::Error,
			std::move(message),
			std::move(details),
			"Retry the command or check the selected path.",
			"JFMCommands",
			"jfm.command"};
	}

	Path ensureProjectExtension(Path path)
	{
		std::string text = path.Utf8();
		if (text.size() >= 9 && text.substr(text.size() - 9) == ".jfm.yaml")
			return path;
		auto result = path.Native();
		result += ".jfm.yaml";
		return Path::FromResolved(std::move(result));
	}

	Path ensureProjectBundleExtension(Path path)
	{
		std::string text = path.Utf8();
		if (text.size() >= 4 && text.substr(text.size() - 4) == ".jfm")
			return path;
		auto result = path.Native();
		result += ".jfm";
		return Path::FromResolved(std::move(result));
	}

	Path ensureExtension(Path path, const char *extension)
	{
		const std::string text = path.Utf8();
		const std::string suffix = extension;
		if (text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix)
			return path;
		auto result = path.Native();
		result += suffix;
		return Path::FromResolved(std::move(result));
	}

	bool isDirectory(const Path &path)
	{
		std::error_code error;
		return std::filesystem::is_directory(path.Native(), error);
	}

	bool isProjectBundlePath(const Path &path)
	{
		std::string extension = path.extension().Utf8();
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return extension == ".jfm";
	}

	std::string safeFileName(std::string name)
	{
		if (name.empty())
			return "item";
		for (char &character : name)
		{
			if (character == '<' || character == '>' || character == ':' || character == '"' || character == '/' || character == '\\' || character == '|' || character == '?' || character == '*')
				character = '_';
		}
		return name;
	}

	std::string copyName(const std::string &name)
	{
		return name.empty() ? "Copy" : name + " Copy";
	}

	std::string replaceAll(std::string text, const std::string &from, const std::string &to)
	{
		std::size_t position = 0;
		while ((position = text.find(from, position)) != std::string::npos)
		{
			text.replace(position, from.size(), to);
			position += to.size();
		}
		return text;
	}

	std::string expandShortNamePattern(std::string pattern, const JFMCharacteristicInfo &info)
	{
		std::ostringstream temperature;
		if (info.temperatureK)
			temperature << *info.temperatureK;
		pattern = replaceAll(std::move(pattern), "{name}", info.name);
		pattern = replaceAll(std::move(pattern), "{id}", std::to_string(info.id));
		pattern = replaceAll(std::move(pattern), "{stem}", info.sourcePath.Empty() ? info.name : Path::FromResolved(info.sourcePath.Native().stem()).Utf8());
		pattern = replaceAll(std::move(pattern), "{folder}", info.sourcePath.Empty() ? std::string{} : info.sourcePath.parent_path().filename().Utf8());
		pattern = replaceAll(std::move(pattern), "{parent_folder}", info.sourcePath.Empty() ? std::string{} : info.sourcePath.parent_path().parent_path().filename().Utf8());
		pattern = replaceAll(std::move(pattern), "{T}", temperature.str());
		pattern = replaceAll(std::move(pattern), "{temperature_K}", temperature.str());
		for (const auto &[key, value] : info.metadata)
			pattern = replaceAll(std::move(pattern), "{" + key + "}", value);
		return pattern;
	}

	std::optional<Path> chooseProjectOpenPath(const Ref<JFMUserSettingsStore> &settings)
	{
		return OpenFileDialog(JFMProjectFilter, settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
	}

	std::optional<Path> chooseProjectBundleOpenPath(const Ref<JFMUserSettingsStore> &settings)
	{
		std::vector<Path> paths = PickFoldersDialog(false, settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
		if (paths.empty())
			return std::nullopt;
		return paths.front();
	}

	std::optional<Path> chooseProjectSavePath(const Ref<JFMUserSettingsStore> &settings)
	{
		std::optional<Path> path = SaveFileDialog(JFMProjectFilter, "project.jfm.yaml", settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
		if (!path)
			return std::nullopt;
		return ensureProjectExtension(*path);
	}

	std::optional<Path> chooseProjectBundleSavePath(const Ref<JFMUserSettingsStore> &settings, const std::string &projectName)
	{
		const std::string defaultName = safeFileName(projectName.empty() ? "project" : projectName) + ".jfm";
		std::optional<Path> path = SaveFileDialog(JFMProjectBundleFilter, defaultName.c_str(), settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
		if (!path)
			return std::nullopt;
		return ensureProjectBundleExtension(*path);
	}

	std::optional<Path> chooseRawTsvSavePath(const std::string &defaultName)
	{
		std::optional<Path> path = SaveFileDialog(TsvFilter, defaultName.c_str());
		if (!path)
			return std::nullopt;
		return ensureExtension(*path, ".tsv");
	}

	std::optional<Path> chooseOutputDirectory(const Ref<JFMUserSettingsStore> &settings)
	{
		std::vector<Path> paths = PickFoldersDialog(false, settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
		if (paths.empty())
			return std::nullopt;
		return paths.front();
	}

	std::vector<Path> chooseImportFiles(const Ref<JFMUserSettingsStore> &settings)
	{
		return OpenFilesDialog(JFMDataFilter, settings != nullptr ? settings->GetDefaultImportDirectory() : Path{});
	}

	std::vector<Path> chooseImportDirectories(const Ref<JFMUserSettingsStore> &settings, bool multiple)
	{
		return PickFoldersDialog(multiple, settings != nullptr ? settings->GetDefaultImportDirectory() : Path{});
	}

	void rememberImportDirectory(const Ref<JFMUserSettingsStore> &settings, const std::vector<Path> &paths)
	{
		if (settings == nullptr || paths.empty())
			return;
		const Path &first = paths.front();
		settings->SetDefaultImportDirectory(isDirectory(first) ? first : first.parent_path());
	}

	std::vector<JFMExportItem> makeExportItems(const std::vector<JFMCharacteristicRecord> &characteristics, Path prefix = {})
	{
		std::vector<JFMExportItem> items;
		items.reserve(characteristics.size());
		for (const JFMCharacteristicRecord &characteristic : characteristics)
		{
			Path relative = prefix / Path(safeFileName(characteristic.info.name) + ".tsv");
			items.push_back(JFMExportItem{relative, characteristic});
		}
		return items;
	}

	std::optional<JFMSeries> findSeries(const JFMProject &project, const std::string &seriesId)
	{
		const auto it = std::find_if(project.series.begin(), project.series.end(), [&seriesId](const JFMSeries &series) { return series.id == seriesId; });
		if (it == project.series.end())
			return std::nullopt;
		return *it;
	}

	std::optional<JFMGroup> findGroup(const JFMProject &project, const std::string &groupId)
	{
		const auto it = std::find_if(project.groups.begin(), project.groups.end(), [&groupId](const JFMGroup &group) { return group.id == groupId; });
		if (it == project.groups.end())
			return std::nullopt;
		return *it;
	}

	void collectGroupIds(const JFMProject &project, const std::string &groupId, std::set<std::string> &groupIds)
	{
		groupIds.insert(groupId);
		for (const JFMGroup &group : project.groups)
		{
			if (group.parentId == groupId)
				collectGroupIds(project, group.id, groupIds);
		}
	}

	Path groupPath(const JFMProject &project, std::string groupId)
	{
		std::vector<std::string> names;
		while (!groupId.empty())
		{
			auto group = findGroup(project, groupId);
			if (!group)
				break;
			names.push_back(safeFileName(group->name));
			groupId = group->parentId;
		}
		Path path;
		for (auto it = names.rbegin(); it != names.rend(); ++it)
			path = path / Path(*it);
		return path;
	}

	ProjectHandle projectFromContext(const Ref<JFMWorkspaceStore> &workspace, CommandContext &context)
	{
		if (ProjectHandle *project = context.TryGet<ProjectHandle>("project"))
			return *project;
		return workspace->GetActiveProjectHandle();
	}

	JFMTreeItemRef itemFromContext(const Ref<JFMWorkspaceStore> &workspace, CommandContext &context, const char *key = "item")
	{
		if (JFMTreeItemRef *item = context.TryGet<JFMTreeItemRef>(key))
			return *item;
		return workspace->GetFocusedTreeItem();
	}

	std::string contextString(CommandContext &context, const char *key, std::string fallback = {})
	{
		if (std::string *value = context.TryGet<std::string>(key))
			return *value;
		return fallback;
	}

	JFMProjectEditRequest makeEditRequest(
		const Ref<JFMWorkspaceStore> &workspace,
		CommandContext &context,
		std::string description,
		JFMProjectEditRequest::Apply apply)
	{
		return {projectFromContext(workspace, context), std::move(description), std::move(apply)};
	}

	Result<void> cloneTreeItem(
		JFMWorkspaceStore &workspace,
		ProjectHandle targetProject,
		const JFMTreeItemRef &source,
		const JFMTreeItemRef &target,
		bool extractToGroup)
	{
		if (!source.IsValid())
			return {};
		const JFMProject sourceProject = workspace.GetProjectSnapshot(source.project);
		const JFMProject targetSnapshot = workspace.GetProjectSnapshot(targetProject);
		const bool sameProject = source.project == targetProject;

		std::string targetGroupId;
		std::string targetSeriesId;
		if (target.kind == JFMTreeItemKind::Group)
			targetGroupId = target.id;
		else if (target.kind == JFMTreeItemKind::Series)
		{
			targetSeriesId = target.id;
			if (const auto series = findSeries(targetSnapshot, target.id))
				targetGroupId = series->groupId;
		}
		else if (target.kind == JFMTreeItemKind::Characteristic && !target.id.empty())
		{
			const CharacteristicId id = static_cast<CharacteristicId>(std::stoull(target.id));
			for (const JFMSeries &series : targetSnapshot.series)
			{
				if (std::find(series.characteristicIds.begin(), series.characteristicIds.end(), id) != series.characteristicIds.end())
				{
					targetSeriesId = series.id;
					targetGroupId = series.groupId;
					break;
				}
			}
		}

		if (extractToGroup)
			targetGroupId = workspace.AddGroup(targetProject, "Pasted", targetGroupId).id;

		const auto cloneCharacteristics = [&](const std::vector<CharacteristicId> &ids, const std::string &seriesId) {
			std::vector<JFMCharacteristicRecord> clones;
			clones.reserve(ids.size());
			for (CharacteristicId id : ids)
			{
				auto characteristic = workspace.FindCharacteristicRecord({source.project, id});
				if (!characteristic)
					continue;
				characteristic->info.id = InvalidCharacteristicId;
				characteristic->info.name = copyName(characteristic->info.name);
				clones.emplace_back(std::move(*characteristic));
			}
			if (!clones.empty())
				(void)workspace.AddCharacteristicRecords(targetProject, std::move(clones), seriesId);
		};

		std::function<void(const JFMSeries &, const std::string &)> cloneSeries;
		cloneSeries = [&](const JFMSeries &sourceSeries, const std::string &groupId) {
			const JFMSeries series = workspace.AddSeries(targetProject, copyName(sourceSeries.name), groupId);
			if (const auto shortName = sourceSeries.metadata.find("short_name"); shortName != sourceSeries.metadata.end())
				(void)workspace.SetSeriesShortName(targetProject, series.id, copyName(shortName->second));
			cloneCharacteristics(sourceSeries.characteristicIds, series.id);
		};

		if (source.kind == JFMTreeItemKind::Characteristic)
		{
			if (targetSeriesId.empty())
				targetSeriesId = workspace.AddSeries(targetProject, "Pasted Characteristics", targetGroupId).id;
			cloneCharacteristics(source.characteristicIds, targetSeriesId);
			return {};
		}
		if (source.kind == JFMTreeItemKind::Series)
		{
			if (const auto sourceSeries = findSeries(sourceProject, source.id))
				cloneSeries(*sourceSeries, targetGroupId.empty() && sameProject ? sourceSeries->groupId : targetGroupId);
			return {};
		}
		if (source.kind != JFMTreeItemKind::Group)
			return {};

		const auto copyGroup = [&](const auto &self, const std::string &sourceGroupId, const std::string &parentId) -> void {
			const auto group = findGroup(sourceProject, sourceGroupId);
			if (!group)
				return;
			const JFMGroup created = workspace.AddGroup(targetProject, copyName(group->name), parentId);
			if (const auto shortName = group->metadata.find("short_name"); shortName != group->metadata.end())
				(void)workspace.SetGroupShortName(targetProject, created.id, copyName(shortName->second));
			for (const JFMSeries &series : sourceProject.series)
			{
				if (series.groupId == sourceGroupId)
					cloneSeries(series, created.id);
			}
			for (const JFMGroup &child : sourceProject.groups)
			{
				if (child.parentId == sourceGroupId)
					self(self, child.id, created.id);
			}
		};
		const auto sourceGroup = findGroup(sourceProject, source.id);
		if (sourceGroup)
			copyGroup(copyGroup, source.id, targetGroupId.empty() && sameProject ? sourceGroup->parentId : targetGroupId);
		return {};
	}

	void queueBatchExport(EventBus &eventBus, const Path &directory, std::vector<JFMExportItem> items, ProjectHandle project)
	{
		if (!items.empty())
			eventBus.Queue(ExportRawTsvBatchRequested{directory, std::move(items), project});
	}

	std::vector<JFMCharacteristicRecord> collectSeriesRecords(const Ref<JFMWorkspaceStore> &workspace, ProjectHandle project, const std::string &seriesId)
	{
		return seriesId.empty() ? std::vector<JFMCharacteristicRecord>{} : workspace->GetSeriesCharacteristicRecords(project, seriesId);
	}

	std::vector<JFMCharacteristicRecord> collectGroupRecords(const Ref<JFMWorkspaceStore> &workspace, ProjectHandle projectHandle, const JFMProject &project, const std::string &groupId)
	{
		std::set<std::string> groupIds;
		collectGroupIds(project, groupId, groupIds);
		std::vector<JFMCharacteristicRecord> records;
		for (const JFMSeries &series : project.series)
		{
			if (groupIds.find(series.groupId) == groupIds.end())
				continue;
			std::vector<JFMCharacteristicRecord> seriesRecords = workspace->GetSeriesCharacteristicRecords(projectHandle, series.id);
			records.insert(records.end(), seriesRecords.begin(), seriesRecords.end());
		}
		return records;
	}

	std::optional<std::tuple<std::string, std::vector<JFMCharacteristicRecord>>> recordsFromSeriesOrGroupContext(
		const Ref<JFMWorkspaceStore> &workspace,
		CommandContext &context)
	{
		const ProjectHandle projectHandle = workspace->GetActiveProjectHandle();
		const JFMProject project = workspace->GetProjectSnapshot(projectHandle);
		if (std::string *seriesId = context.TryGet<std::string>("series_id"))
		{
			auto series = findSeries(project, *seriesId);
			if (!series)
				return std::nullopt;
			return std::tuple{series->name, collectSeriesRecords(workspace, projectHandle, *seriesId)};
		}
		if (std::string *groupId = context.TryGet<std::string>("group_id"))
		{
			auto group = findGroup(project, *groupId);
			if (!group)
				return std::nullopt;
			return std::tuple{group->name, collectGroupRecords(workspace, projectHandle, project, *groupId)};
		}
		const std::string seriesId = workspace->GetSelectedSeriesId();
		if (auto series = findSeries(project, seriesId))
			return std::tuple{series->name, collectSeriesRecords(workspace, projectHandle, seriesId)};
		return std::nullopt;
	}

	Result<void> handleNewProject(Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		(void)workspace->CreateProject("Untitled JFM Project");
		if (settings != nullptr)
			workspace->EnsureDefaultScripts(settings->GetDefaultScriptsDirectory());
		return Result<void>{};
	}

	Result<void> handleOpenProject(Ref<EventBus> eventBus, Ref<JFMUserSettingsStore> settings, CommandContext &context)
	{
		try
		{
			std::optional<Path> path;
			if (Path *contextPath = context.TryGet<Path>("path"))
				path = *contextPath;
			else
				path = chooseProjectOpenPath(settings);
			if (path)
			{
				if (isProjectBundlePath(*path))
					eventBus->Queue(ProjectBundleLoadRequested{*path});
				else
					eventBus->Queue(ProjectLoadRequested{*path});
			}
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Open project failed.", error.what())};
		}
	}

	Result<void> handleOpenProjectBundle(Ref<EventBus> eventBus, Ref<JFMUserSettingsStore> settings, CommandContext &context)
	{
		try
		{
			std::optional<Path> path;
			if (Path *contextPath = context.TryGet<Path>("path"))
				path = *contextPath;
			else
				path = chooseProjectBundleOpenPath(settings);
			if (path)
				eventBus->Queue(ProjectBundleLoadRequested{*path});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Open project bundle failed.", error.what())};
		}
	}

	Result<void> handleSaveProject(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		try
		{
			const ProjectHandle project = workspace->GetActiveProjectHandle();
			std::optional<Path> path = workspace->GetProjectPath(project);
			if (!path)
				path = chooseProjectSavePath(settings);
			if (path)
			{
				if (isProjectBundlePath(*path))
					queueBundleSave(*eventBus, workspace, project, *path);
				else
					queueSave(*eventBus, workspace, project, *path);
			}
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Save project failed.", error.what())};
		}
	}

	Result<void> handleSaveProjectAs(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		try
		{
			const ProjectHandle project = workspace->GetActiveProjectHandle();
			std::optional<Path> path = chooseProjectSavePath(settings);
			if (path)
				queueSave(*eventBus, workspace, project, *path);
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Save project as failed.", error.what())};
		}
	}

	Result<void> handleSaveProjectBundleAs(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		try
		{
			const ProjectHandle project = workspace->GetActiveProjectHandle();
			std::optional<Path> path = chooseProjectBundleSavePath(settings, workspace->GetProjectSnapshot(project).name);
			if (path)
				queueBundleSave(*eventBus, workspace, project, *path);
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Save project bundle failed.", error.what())};
		}
	}

	Result<void> handleOpenNewProjectWindow(CommandContext &)
	{
		auto executable = Platform::CurrentExecutablePath();
		if (!executable)
			return Result<void>{makeCommandError("Open new project window failed.", executable.Error().technicalDetails)};
		auto launched = Platform::LaunchDetached(Platform::ProcessRunOptions{*executable, {}, executable->parent_path()});
		if (!launched)
			return Result<void>{makeCommandError("Open new project window failed.", launched.Error().technicalDetails)};
		return {};
	}

	Result<void> handleImportFiles(Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, Ref<JFMImportDialogState> dialog, CommandContext &context)
	{
		try
		{
			std::vector<Path> paths;
			if (std::vector<Path> *contextPaths = context.TryGet<std::vector<Path>>("paths"))
				paths = *contextPaths;
			else
				paths = chooseImportFiles(settings);
			rememberImportDirectory(settings, paths);
			const ProjectHandle project = context.TryGet<ProjectHandle>("project") ? *context.TryGet<ProjectHandle>("project") : workspace->GetActiveProjectHandle();
			std::string targetSeriesId;
			if (std::string *contextSeriesId = context.TryGet<std::string>("target_series_id"))
				targetSeriesId = *contextSeriesId;
			const std::string targetGroupId = context.TryGet<std::string>("target_group_id") ? *context.TryGet<std::string>("target_group_id") : std::string{};
			const JFMImportPlacement placement = targetSeriesId.empty() ? JFMImportPlacement::NewSeries : JFMImportPlacement::ExistingSeries;
			if (!paths.empty() && dialog != nullptr)
				dialog->Open(JFMImportDialogRequest{paths, targetSeriesId, false, project, targetGroupId, placement});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Import files failed.", error.what())};
		}
	}

	Result<void> handleImportDirectories(Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, Ref<JFMImportDialogState> dialog, bool multiple, bool recursive, CommandContext &context)
	{
		try
		{
			std::vector<Path> paths;
			if (std::vector<Path> *contextPaths = context.TryGet<std::vector<Path>>("paths"))
				paths = *contextPaths;
			else
				paths = chooseImportDirectories(settings, multiple);
			if (paths.empty())
				return Result<void>{};
			rememberImportDirectory(settings, paths);
			const ProjectHandle project = context.TryGet<ProjectHandle>("project") ? *context.TryGet<ProjectHandle>("project") : workspace->GetActiveProjectHandle();
			std::string targetSeriesId;
			if (std::string *contextSeriesId = context.TryGet<std::string>("target_series_id"))
				targetSeriesId = *contextSeriesId;
			const std::string targetGroupId = context.TryGet<std::string>("target_group_id") ? *context.TryGet<std::string>("target_group_id") : std::string{};
			const JFMImportPlacement placement = targetSeriesId.empty()
				? (recursive ? JFMImportPlacement::PreserveFolderTree : JFMImportPlacement::NewSeries)
				: JFMImportPlacement::ExistingSeries;
			if (dialog != nullptr)
				dialog->Open(JFMImportDialogRequest{paths, targetSeriesId, recursive, project, targetGroupId, placement});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Import directories failed.", error.what())};
		}
	}

	Result<void> handleExportRawTsv(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		try
		{
			const ProjectHandle project = workspace->GetActiveProjectHandle();
			std::vector<JFMCharacteristicRecord> selected = workspace->GetSelectedCharacteristicRecords();
			if (selected.empty())
			{
				workspace->SetStatusMessage("Raw TSV export failed: no characteristic selected.");
				return Result<void>{};
			}
			if (selected.size() > 1)
			{
				std::optional<Path> directory = chooseOutputDirectory(settings);
				if (directory)
					queueBatchExport(*eventBus, *directory, makeExportItems(selected), project);
				return Result<void>{};
			}
			const std::string defaultName = selected.front().info.name.empty() ? "raw.tsv" : selected.front().info.name + ".tsv";
			std::optional<Path> path = chooseRawTsvSavePath(defaultName);
			if (path)
				eventBus->Queue(ExportRawTsvRequested{*path, selected.front(), project});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Raw TSV export failed.", error.what())};
		}
	}

	Result<void> handleExportSeriesRawTsv(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &context)
	{
		try
		{
			std::string seriesId = workspace->GetSelectedSeriesId();
			if (std::string *contextSeriesId = context.TryGet<std::string>("series_id"))
				seriesId = *contextSeriesId;

			const ProjectHandle projectHandle = workspace->GetActiveProjectHandle();
			const JFMProject project = workspace->GetProjectSnapshot(projectHandle);
			auto series = findSeries(project, seriesId);
			if (!series)
				return Result<void>{};

			std::optional<Path> directory = chooseOutputDirectory(settings);
			if (!directory)
				return Result<void>{};

			queueBatchExport(*eventBus, *directory, makeExportItems(workspace->GetSeriesCharacteristicRecords(projectHandle, seriesId), Path(safeFileName(series->name))), projectHandle);
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Series TSV export failed.", error.what())};
		}
	}

	Result<void> handleExportGroupRawTsv(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &context)
	{
		try
		{
			std::string groupId = workspace->GetSelectedGroupId();
			if (std::string *contextGroupId = context.TryGet<std::string>("group_id"))
				groupId = *contextGroupId;
			if (groupId.empty())
				return Result<void>{};

			const ProjectHandle projectHandle = workspace->GetActiveProjectHandle();
			const JFMProject project = workspace->GetProjectSnapshot(projectHandle);
			std::set<std::string> groupIds;
			collectGroupIds(project, groupId, groupIds);

			std::optional<Path> directory = chooseOutputDirectory(settings);
			if (!directory)
				return Result<void>{};

			std::vector<JFMExportItem> items;
			for (const JFMSeries &series : project.series)
			{
				if (groupIds.find(series.groupId) == groupIds.end())
					continue;
				std::vector<JFMExportItem> seriesItems = makeExportItems(
					workspace->GetSeriesCharacteristicRecords(projectHandle, series.id),
					groupPath(project, series.groupId) / Path(safeFileName(series.name)));
				items.insert(items.end(), seriesItems.begin(), seriesItems.end());
			}
			queueBatchExport(*eventBus, *directory, std::move(items), projectHandle);
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Group TSV export failed.", error.what())};
		}
	}

	Result<void> handleExportArrheniusTsv(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		try
		{
			auto source = recordsFromSeriesOrGroupContext(workspace, context);
			if (!source || std::get<1>(*source).empty())
				return Result<void>{};
			const std::string defaultName = safeFileName(std::get<0>(*source)) + "_arrhenius.tsv";
			std::optional<Path> path = chooseRawTsvSavePath(defaultName);
			if (path)
				eventBus->Queue(ExportArrheniusTsvRequested{*path, std::move(std::get<1>(*source)), workspace->GetActiveProjectHandle()});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Arrhenius TSV export failed.", error.what())};
		}
	}

	Result<void> handleExportCharacteristicsFitTsv(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		try
		{
			auto source = recordsFromSeriesOrGroupContext(workspace, context);
			if (!source || std::get<1>(*source).empty())
				return Result<void>{};
			const std::string defaultName = safeFileName(std::get<0>(*source)) + "_characteristics_fit.tsv";
			std::optional<Path> path = chooseRawTsvSavePath(defaultName);
			if (path)
				eventBus->Queue(ExportCharacteristicsFitTsvRequested{*path, std::move(std::get<1>(*source)), workspace->GetActiveProjectHandle()});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Characteristics + fit TSV export failed.", error.what())};
		}
	}

	void queueSave(EventBus &eventBus, const Ref<JFMWorkspaceStore> &workspace, ProjectHandle project, const Path &path)
	{
		eventBus.Queue(ProjectSaveRequested{
			path,
			workspace->GetProjectSnapshot(project),
			project,
			InvalidJFMOperationId,
			workspace->GetProjectRevision(project)});
	}

	void queueBundleSave(EventBus &eventBus, const Ref<JFMWorkspaceStore> &workspace, ProjectHandle project, const Path &path)
	{
		auto snapshot = workspace->CaptureProjectBundle(project);
		if (!snapshot)
		{
			workspace->SetStatusMessage(project, "Project bundle save failed: active project is unavailable.");
			return;
		}
		eventBus.Queue(ProjectBundleSaveRequested{
			path,
			std::move(*snapshot),
			project,
			InvalidJFMOperationId,
			workspace->GetProjectRevision(project)});
	}

	void registerCommand(Ref<CommandRegistry> registry, CommandMeta meta, LambdaCommand::Handler handler)
	{
		std::string description = meta.description;
		auto result = registry->Register(
			std::move(meta),
			std::bind(CreateLambdaCommand, description, std::move(handler), std::placeholders::_1));
		if (!result)
			DS_LOG_WARN("JFM command registration failed: {}", result.Error().technicalDetails);
	}

	using EditRequestBuilder = std::function<JFMProjectEditRequest(CommandContext &)>;

	void registerEditCommand(Ref<CommandRegistry> registry, CommandMeta meta, Ref<JFMWorkspaceStore> workspace, EditRequestBuilder builder)
	{
		auto result = registry->Register(
			std::move(meta),
			[workspace = std::move(workspace), builder = std::move(builder)](CommandContext &context) mutable {
				return CreateJFMProjectEditCommand(workspace, builder(context));
			});
		if (!result)
			DS_LOG_WARN("JFM edit command registration failed: {}", result.Error().technicalDetails);
	}

	void registerBinding(Ref<KeymapResolver> resolver, KeyBinding binding)
	{
		auto result = resolver->RegisterBinding(std::move(binding));
		if (!result)
			DS_LOG_WARN("JFM keybinding registration conflict: {}", result.Error().technicalDetails);
	}
	

	void RegisterJFMCommands(JFMCommandRegistrationContext context)
	{
		auto registry = context.commandRegistry.lock();
		auto resolver = context.keymapResolver.lock();
		Ref<EventBus> eventBus = std::move(context.eventBus);
		Ref<JFMWorkspaceStore> workspace = std::move(context.workspace);
		Ref<JFMUserSettingsStore> settings = std::move(context.settings);
		Ref<JFMImportDialogState> importDialog = std::move(context.importDialog);
		if (registry == nullptr || resolver == nullptr || eventBus == nullptr || workspace == nullptr || settings == nullptr || importDialog == nullptr)
			throw std::runtime_error("JFM startup failed: command registry, keymap resolver, event bus, workspace, settings, or import dialog is unavailable.");

		auto editRegistration = registry->Register(
			CommandMeta{
				CommandID{JFMApplyProjectEditCommandId},
				"Apply JFM Project Edit",
				"JFM",
				"Apply an undoable mutation to one JFM project.",
				{},
				CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			std::bind(CreateProjectEditCommand, workspace, std::placeholders::_1));
		if (!editRegistration)
			throw std::runtime_error("JFM startup failed: project edit command could not be registered.");

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMAddGroupCommandId}, "Add Group", "JFM", "Add a group to the project tree.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				const std::string parentId = contextString(commandContext, "parent_group_id");
				const std::string name = contextString(commandContext, "name", "Group");
				return makeEditRequest(workspace, commandContext, "Add group", [parentId, name](JFMWorkspaceStore &store, ProjectHandle project) {
					(void)store.AddGroup(project, name, parentId);
					return Result<void>{};
				});
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMAddSeriesCommandId}, "Add Series", "JFM", "Add a series to the project tree.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				const std::string groupId = contextString(commandContext, "group_id");
				const std::string name = contextString(commandContext, "name", "Series");
				return makeEditRequest(workspace, commandContext, "Add series", [groupId, name](JFMWorkspaceStore &store, ProjectHandle project) {
					(void)store.AddSeries(project, name, groupId);
					return Result<void>{};
				});
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMDeleteTreeItemCommandId}, "Delete Project Tree Item", "JFM", "Delete the focused project tree item.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				JFMTreeItemRef item = itemFromContext(workspace, commandContext);
				const ProjectHandle project = item.project == InvalidProjectHandle ? projectFromContext(workspace, commandContext) : item.project;
				return JFMProjectEditRequest{project, "Delete project item", [item = std::move(item)](JFMWorkspaceStore &store, ProjectHandle handle) {
					if (item.kind == JFMTreeItemKind::Group)
						(void)store.DeleteGroup(handle, item.id);
					else if (item.kind == JFMTreeItemKind::Series)
						(void)store.DeleteSeries(handle, item.id);
					else if (item.kind == JFMTreeItemKind::Characteristic)
					{
						std::vector<CharacteristicId> ids = item.characteristicIds;
						if (ids.empty() && !item.id.empty())
							ids.emplace_back(static_cast<CharacteristicId>(std::stoull(item.id)));
						for (CharacteristicId id : ids)
							(void)store.DeleteCharacteristic(handle, id);
					}
					return Result<void>{};
				}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMMoveCharacteristicsCommandId}, "Move Characteristics", "JFM", "Move characteristics to another series.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				const std::vector<CharacteristicId> ids = commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids")
					? *commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids")
					: workspace->GetSelectedCharacteristicIds();
				const std::string seriesId = contextString(commandContext, "target_series_id");
				return makeEditRequest(workspace, commandContext, "Move characteristics", [ids, seriesId](JFMWorkspaceStore &store, ProjectHandle project) {
					for (CharacteristicId id : ids)
						(void)store.MoveCharacteristic(project, id, seriesId);
					return Result<void>{};
				});
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMRenameTreeItemCommandId}, "Rename Project Tree Item", "JFM", "Rename the focused project tree item.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				JFMTreeItemRef item = itemFromContext(workspace, commandContext);
				const std::string name = contextString(commandContext, "name");
				const ProjectHandle project = item.project == InvalidProjectHandle ? projectFromContext(workspace, commandContext) : item.project;
				return JFMProjectEditRequest{project, "Rename project item", [item = std::move(item), name](JFMWorkspaceStore &store, ProjectHandle handle) {
					if (item.kind == JFMTreeItemKind::Project)
						(void)store.RenameProject(handle, name);
					else if (item.kind == JFMTreeItemKind::Group)
						(void)store.RenameGroup(handle, item.id, name);
					else if (item.kind == JFMTreeItemKind::Series)
						(void)store.RenameSeries(handle, item.id, name);
					else if (item.kind == JFMTreeItemKind::Characteristic && !item.id.empty())
						(void)store.RenameCharacteristic(handle, static_cast<CharacteristicId>(std::stoull(item.id)), name);
					return Result<void>{};
				}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMSetShortNameCommandId}, "Set Short Name", "JFM", "Set short names for project tree items.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				JFMTreeItemRef item = itemFromContext(workspace, commandContext);
				const std::string value = contextString(commandContext, "short_name");
				const ProjectHandle project = item.project == InvalidProjectHandle ? projectFromContext(workspace, commandContext) : item.project;
				return JFMProjectEditRequest{project, "Set short name", [item = std::move(item), value](JFMWorkspaceStore &store, ProjectHandle handle) {
					if (item.kind == JFMTreeItemKind::Group)
						(void)store.SetGroupShortName(handle, item.id, value);
					else if (item.kind == JFMTreeItemKind::Series)
						(void)store.SetSeriesShortName(handle, item.id, value);
					else if (item.kind == JFMTreeItemKind::Characteristic)
					{
						for (CharacteristicId id : item.characteristicIds)
						{
							auto record = store.FindCharacteristicRecord({handle, id});
							if (record)
								(void)store.SetCharacteristicShortName(handle, id, expandShortNamePattern(value, record->info));
						}
					}
					return Result<void>{};
				}};
			});

		registerCommand(
			registry,
			CommandMeta{CommandID{JFMCopyTreeItemCommandId}, "Copy Project Tree Item", "JFM", "Copy the focused project tree item.", {}, CommandFlags::None},
			[workspace](CommandContext &commandContext) {
				JFMTreeItemRef item = itemFromContext(workspace, commandContext);
				if (!item.IsValid())
					return Result<void>{};
				if (item.kind == JFMTreeItemKind::Characteristic && item.characteristicIds.empty())
					item.characteristicIds = workspace->GetSelectedCharacteristicIds();
				workspace->SetTreeClipboard(std::move(item));
				return Result<void>{};
			});

		registerCommand(
			registry,
			CommandMeta{CommandID{JFMRequestRenameTreeItemCommandId}, "Rename Project Tree Item...", "JFM", "Open the rename editor for the focused project tree item.", {}, CommandFlags::None},
			[workspace, eventBus](CommandContext &commandContext) {
				JFMTreeItemRef item = itemFromContext(workspace, commandContext);
				if (item.IsValid())
					eventBus->Queue(ProjectTreeRenameRequested{std::move(item)});
				return Result<void>{};
			});

		const auto registerPasteRequest = [&](const char *id, const char *title, bool duplicate) {
			registerCommand(
				registry,
				CommandMeta{CommandID{id}, title, "JFM", "Open project tree copy options.", {}, CommandFlags::None},
				[workspace, eventBus, duplicate](CommandContext &commandContext) {
					if (!duplicate && !workspace->GetTreeClipboard())
						return Result<void>{};
					JFMTreeItemRef item = itemFromContext(workspace, commandContext);
					if (item.IsValid())
						eventBus->Queue(ProjectTreePasteRequested{std::move(item), duplicate});
					return Result<void>{};
				});
		};
		registerPasteRequest(JFMRequestPasteTreeItemCommandId, "Paste Project Tree Item...", false);
		registerPasteRequest(JFMRequestDuplicateTreeItemCommandId, "Duplicate Project Tree Item...", true);

		const auto registerPaste = [&](const char *id, const char *title, bool duplicate) {
			registerEditCommand(
				registry,
				CommandMeta{CommandID{id}, title, "JFM", "Clone project tree items without copying raw arrays.", {}, CommandFlags::Undoable},
				workspace,
				[workspace, duplicate](CommandContext &commandContext) {
					JFMTreeItemRef source = duplicate ? itemFromContext(workspace, commandContext) : workspace->GetTreeClipboard().value_or(JFMTreeItemRef{});
					JFMTreeItemRef target = itemFromContext(workspace, commandContext, "target");
					const bool extract = commandContext.TryGet<bool>("extract_to_group") != nullptr && *commandContext.TryGet<bool>("extract_to_group");
					const ProjectHandle project = target.project == InvalidProjectHandle ? projectFromContext(workspace, commandContext) : target.project;
					return JFMProjectEditRequest{project, duplicate ? "Duplicate project item" : "Paste project item", [source = std::move(source), target = std::move(target), extract](JFMWorkspaceStore &store, ProjectHandle handle) {
						return cloneTreeItem(store, handle, source, target, extract);
					}};
				});
		};
		registerPaste(JFMPasteTreeItemCommandId, "Paste Project Tree Item", false);
		registerPaste(JFMDuplicateTreeItemCommandId, "Duplicate Project Tree Item", true);

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMAddScriptCommandId}, "Add Import Script", "JFM", "Register an import script.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				const Path path = commandContext.TryGet<Path>("path") ? *commandContext.TryGet<Path>("path") : Path{};
				return makeEditRequest(workspace, commandContext, "Add script", [path](JFMWorkspaceStore &store, ProjectHandle project) {
					if (!path.Empty())
						(void)store.AddScript(project, path);
					return Result<void>{};
				});
			});

		for (const auto &[id, title, remove] : std::array{
				std::tuple{JFMReloadScriptCommandId, "Reload Import Script", false},
				std::tuple{JFMRemoveScriptCommandId, "Remove Import Script", true}})
		{
			registerEditCommand(
				registry,
				CommandMeta{CommandID{id}, title, "JFM", "Update the project script registry.", {}, CommandFlags::Undoable},
				workspace,
				[workspace, remove](CommandContext &commandContext) {
					const std::string scriptId = contextString(commandContext, "script_id");
					return makeEditRequest(workspace, commandContext, remove ? "Remove script" : "Reload script", [scriptId, remove](JFMWorkspaceStore &store, ProjectHandle project) {
						if (remove)
							(void)store.RemoveScript(project, scriptId);
						else
							(void)store.ReloadScript(project, scriptId);
						return Result<void>{};
					});
				});
		}

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMSetPlotColorCommandId}, "Set Plot Color", "JFM", "Set plot color for selected characteristics.", {}, CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			workspace,
			[workspace](CommandContext &commandContext) {
				const ProjectHandle project = projectFromContext(workspace, commandContext);
				std::vector<CharacteristicId> ids = workspace->GetSelectedCharacteristicIds();
				if (auto *value = commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids"))
					ids = *value;
				const std::string color = contextString(commandContext, "plot_color");
				return JFMProjectEditRequest{
					project,
					"Set plot color",
					[ids, color](JFMWorkspaceStore &store, ProjectHandle handle) {
						(void)store.SetCharacteristicPlotColor(handle, ids, color);
						return Result<void>{};
					}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMSetCharacteristicMetadataCommandId}, "Set Characteristic Metadata", "JFM", "Set metadata for selected characteristics.", {}, CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			workspace,
			[workspace](CommandContext &commandContext) {
				const ProjectHandle project = projectFromContext(workspace, commandContext);
				std::vector<CharacteristicId> ids = workspace->GetSelectedCharacteristicIds();
				if (auto *value = commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids"))
					ids = *value;
				const std::string key = contextString(commandContext, "metadata_key");
				const std::string value = contextString(commandContext, "metadata_value");
				return JFMProjectEditRequest{
					project,
					"Set characteristic metadata",
					[ids, key, value](JFMWorkspaceStore &store, ProjectHandle handle) {
						(void)store.SetCharacteristicMetadata(handle, ids, key, value);
						return Result<void>{};
					}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMUpsertGroupingCommandId}, "Create or Update Grouping", "JFM", "Create or update a metadata grouping.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				JFMGroupingDefinition grouping = commandContext.TryGet<JFMGroupingDefinition>("grouping") ? *commandContext.TryGet<JFMGroupingDefinition>("grouping") : JFMGroupingDefinition{};
				const bool create = commandContext.TryGet<bool>("create") == nullptr || *commandContext.TryGet<bool>("create");
				return makeEditRequest(workspace, commandContext, create ? "Create grouping" : "Update grouping", [grouping = std::move(grouping), create](JFMWorkspaceStore &store, ProjectHandle project) mutable {
					if (create)
						(void)store.AddGrouping(project, std::move(grouping));
					else
						(void)store.UpdateGrouping(project, std::move(grouping));
					return Result<void>{};
				});
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMDeleteGroupingCommandId}, "Delete Grouping", "JFM", "Delete a metadata grouping.", {}, CommandFlags::Undoable},
			workspace,
			[workspace](CommandContext &commandContext) {
				const std::string groupingId = contextString(commandContext, "grouping_id");
				return makeEditRequest(workspace, commandContext, "Delete grouping", [groupingId](JFMWorkspaceStore &store, ProjectHandle project) {
					(void)store.DeleteGrouping(project, groupingId);
					return Result<void>{};
				});
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMSetPrefitModelCommandId}, "Set Prefit Model", "JFM", "Set the prefit model for selected characteristics.", {}, CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			workspace,
			[workspace, eventBus](CommandContext &commandContext) {
				const ProjectHandle project = projectFromContext(workspace, commandContext);
				std::vector<CharacteristicId> ids = workspace->GetSelectedCharacteristicIds();
				if (auto *value = commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids"))
					ids = *value;
				const std::string modelId = contextString(commandContext, "model_id", "SD-RsRsh");
				return JFMProjectEditRequest{
					project,
					"Change prefit model",
					[ids, modelId](JFMWorkspaceStore &store, ProjectHandle handle) {
						(void)store.SetPrefitModel(handle, ids, modelId);
						return Result<void>{};
					},
					[eventBus, project, ids]() {
						if (eventBus != nullptr && !ids.empty())
							eventBus->Queue(PrefitRequested{project, ids});
					}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMSetFitRangeCommandId}, "Set Fit Range", "JFM", "Set a manual fit range for one characteristic.", {}, CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			workspace,
			[workspace, eventBus](CommandContext &commandContext) {
				const ProjectHandle project = projectFromContext(workspace, commandContext);
				std::vector<CharacteristicId> ids = workspace->GetSelectedCharacteristicIds();
				if (auto *value = commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids"))
					ids = *value;
				else if (auto *idValue = commandContext.TryGet<CharacteristicId>("characteristic_id"))
					ids = {*idValue};
				if (ids.empty())
					ids = {workspace->GetSelectedCharacteristicId()};
				ids.erase(std::remove(ids.begin(), ids.end(), InvalidCharacteristicId), ids.end());
				std::sort(ids.begin(), ids.end());
				ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
				const double lower = commandContext.TryGet<double>("lower_voltage") ? *commandContext.TryGet<double>("lower_voltage") : 0.0;
				const double upper = commandContext.TryGet<double>("upper_voltage") ? *commandContext.TryGet<double>("upper_voltage") : 0.0;
				return JFMProjectEditRequest{
					project,
					"Change fit range",
					[ids, lower, upper](JFMWorkspaceStore &store, ProjectHandle handle) {
						for (CharacteristicId id : ids)
							(void)store.SetManualFitRange(handle, id, lower, upper);
						return Result<void>{};
					},
					[eventBus, project, ids]() {
						if (eventBus != nullptr && !ids.empty())
							eventBus->Queue(PrefitRequested{project, ids});
					}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMResetFitRangeCommandId}, "Reset Fit Range", "JFM", "Restore automatic range detection.", {}, CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			workspace,
			[workspace, eventBus](CommandContext &commandContext) {
				const ProjectHandle project = projectFromContext(workspace, commandContext);
				std::vector<CharacteristicId> ids = workspace->GetSelectedCharacteristicIds();
				if (auto *value = commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids"))
					ids = *value;
				else if (auto *idValue = commandContext.TryGet<CharacteristicId>("characteristic_id"))
					ids = {*idValue};
				if (ids.empty())
					ids = {workspace->GetSelectedCharacteristicId()};
				ids.erase(std::remove(ids.begin(), ids.end(), InvalidCharacteristicId), ids.end());
				std::sort(ids.begin(), ids.end());
				ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
				return JFMProjectEditRequest{
					project,
					"Reset fit range",
					[ids](JFMWorkspaceStore &store, ProjectHandle handle) {
						for (CharacteristicId id : ids)
							(void)store.ResetFitRange(handle, id);
						return Result<void>{};
					},
					[eventBus, project, ids]() {
						if (eventBus != nullptr && !ids.empty())
							eventBus->Queue(PrefitRequested{project, ids});
					}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMAutoDetectPrefitCommandId}, "Auto-detect Prefit Model", "JFM", "Automatically pick the branch, model, and fit range for selected characteristics.", {}, CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			workspace,
			[workspace, eventBus](CommandContext &commandContext) {
				const ProjectHandle project = projectFromContext(workspace, commandContext);
				std::vector<CharacteristicId> ids = workspace->GetSelectedCharacteristicIds();
				if (auto *value = commandContext.TryGet<std::vector<CharacteristicId>>("characteristic_ids"))
					ids = *value;
				else if (auto *idValue = commandContext.TryGet<CharacteristicId>("characteristic_id"))
					ids = {*idValue};
				ids.erase(std::remove(ids.begin(), ids.end(), InvalidCharacteristicId), ids.end());
				std::sort(ids.begin(), ids.end());
				ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
				return JFMProjectEditRequest{
					project,
					"Auto-detect prefit model",
					[ids](JFMWorkspaceStore &store, ProjectHandle handle) {
						(void)store.SetAutoModelSelection(handle, ids, true);
						return Result<void>{};
					},
					[eventBus, project, ids]() {
						if (eventBus != nullptr && !ids.empty())
							eventBus->Queue(PrefitRequested{project, ids});
					}};
			});

		registerEditCommand(
			registry,
			CommandMeta{CommandID{JFMImportApplyCommandId}, "Apply Imported Data", "JFM", "Apply one completed import as an undoable project edit.", {}, CommandFlags::Undoable | CommandFlags::HiddenFromPalette},
			workspace,
			[workspace, eventBus](CommandContext &commandContext) {
				std::vector<ImportCompleted> batches;
				if (auto *value = commandContext.TryGet<std::vector<ImportCompleted>>("batches"))
					batches = std::move(*value);
				const std::string targetGroupId = contextString(commandContext, "target_group_id");
				const bool plotAfterImport = commandContext.TryGet<bool>("plot_after_import") != nullptr && *commandContext.TryGet<bool>("plot_after_import");
				const ProjectHandle project = projectFromContext(workspace, commandContext);
				auto importedIds = CreateRef<std::vector<CharacteristicId>>();
				JFMProjectEditRequest request{
					project,
					"Import JFM data",
					[batches = std::move(batches), targetGroupId, plotAfterImport, eventBus, importedIds](JFMWorkspaceStore &store, ProjectHandle targetProject) mutable {
						std::string selectedSeries;
						for (ImportCompleted &batch : batches)
						{
							if (batch.characteristics.empty())
								continue;
							std::string seriesId = batch.targetSeriesId;
							if (seriesId.empty())
							{
								const std::string groupId = batch.groupPath.empty()
									? targetGroupId
									: store.FindOrCreateGroupPath(targetProject, batch.groupPath, targetGroupId);
								seriesId = store.AddSeries(targetProject, batch.seriesName.empty() ? "Series" : batch.seriesName, groupId).id;
							}
							std::vector<CharacteristicId> added = store.AddCharacteristicRecords(targetProject, std::move(batch.characteristics), seriesId);
							selectedSeries = seriesId;
							importedIds->insert(importedIds->end(), added.begin(), added.end());
							if (plotAfterImport && eventBus != nullptr && !added.empty())
							{
								const std::string title = batch.seriesName.empty() ? "Series" : batch.seriesName;
								eventBus->Queue(PlotRequested{targetProject, title, {{"import", title, std::move(added), {}, {}}}});
							}
						}
						if (!importedIds->empty())
							store.SetSelectedCharacteristics(targetProject, *importedIds, importedIds->back(), selectedSeries);
						store.SetStatusMessage(targetProject, "Import complete.");
						return Result<void>{};
					},
					[eventBus, project, importedIds]() {
						if (eventBus != nullptr && !importedIds->empty())
							eventBus->Queue(FitRequested{project, *importedIds});
					}};
				return request;
			});

		registerCommand(
			registry,
			CommandMeta{NewProjectCommand, "New Project", "JFM", "Create a new JFM project.", {}, CommandFlags::None},
			std::bind(handleNewProject, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{OpenProjectCommand, "Open Project...", "JFM", "Open a JFM project file.", {}, CommandFlags::AsyncCapable},
			std::bind(handleOpenProject, eventBus, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{OpenProjectBundleCommand, "Open Project Bundle...", "JFM", "Open a full JFM project bundle.", {}, CommandFlags::AsyncCapable},
			std::bind(handleOpenProjectBundle, eventBus, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{SaveProjectCommand, "Save Project", "JFM", "Save the current JFM project.", {}, CommandFlags::AsyncCapable},
			std::bind(handleSaveProject, eventBus, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{SaveProjectAsCommand, "Save Project As...", "JFM", "Save the current JFM project to a new file.", {}, CommandFlags::AsyncCapable},
			std::bind(handleSaveProjectAs, eventBus, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{SaveProjectBundleAsCommand, "Save Project Bundle As...", "JFM", "Save the full JFM project bundle.", {}, CommandFlags::AsyncCapable},
			std::bind(handleSaveProjectBundleAs, eventBus, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{OpenNewProjectWindowCommand, "Open New Project Window", "JFM", "Open another JFM application instance.", {}, CommandFlags::AsyncCapable},
			std::bind(handleOpenNewProjectWindow, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ImportFilesCommand, "Import Files", "JFM", "Import JFM data files through Python.", {}, CommandFlags::AsyncCapable},
			std::bind(handleImportFiles, workspace, settings, importDialog, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ImportDirectoryCommand, "Import Folder", "JFM", "Import one folder of JFM data files through Python.", {}, CommandFlags::AsyncCapable},
			std::bind(handleImportDirectories, workspace, settings, importDialog, false, false, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ImportDirectoriesCommand, "Import Folders", "JFM", "Import multiple folders of JFM data files through Python.", {}, CommandFlags::AsyncCapable},
			std::bind(handleImportDirectories, workspace, settings, importDialog, true, false, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ImportDirectoriesRecursiveCommand, "Import Folders Recursively", "JFM", "Import nested folders of JFM data files through Python.", {}, CommandFlags::AsyncCapable},
			std::bind(handleImportDirectories, workspace, settings, importDialog, true, true, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ExportRawTsvCommand, "Export Raw TSV", "JFM", "Export selected raw data as TSV.", {}, CommandFlags::AsyncCapable},
			std::bind(handleExportRawTsv, eventBus, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ExportSeriesRawTsvCommand, "Export Series Raw TSV", "JFM", "Export a series to TSV files.", {}, CommandFlags::AsyncCapable},
			std::bind(handleExportSeriesRawTsv, eventBus, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ExportGroupRawTsvCommand, "Export Group Raw TSV", "JFM", "Export a group to TSV files.", {}, CommandFlags::AsyncCapable},
			std::bind(handleExportGroupRawTsv, eventBus, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ExportArrheniusTsvCommand, "Export Arrhenius TSV", "JFM", "Export fitted parameters versus temperature.", {}, CommandFlags::AsyncCapable},
			std::bind(handleExportArrheniusTsv, eventBus, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ExportCharacteristicsFitTsvCommand, "Export Characteristics + Fit TSV", "JFM", "Export measured characteristics and fitted curves in one TSV.", {}, CommandFlags::AsyncCapable},
			std::bind(handleExportCharacteristicsFitTsv, eventBus, workspace, std::placeholders::_1));

		registerBinding(resolver, {"jfm.new_project", KeyChord{KeyCode::N, KeyModifiers::Ctrl}, NewProjectCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.open_project", KeyChord{KeyCode::O, KeyModifiers::Ctrl}, OpenProjectCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.save_project", KeyChord{KeyCode::S, KeyModifiers::Ctrl}, SaveProjectCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.save_project_as", KeyChord{KeyCode::S, KeyModifiers::Ctrl | KeyModifiers::Shift}, SaveProjectAsCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.redo_alt", KeyChord{KeyCode::Z, KeyModifiers::Ctrl | KeyModifiers::Shift}, CommandID{"edit.redo"}, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.import_files", KeyChord{KeyCode::I, KeyModifiers::Ctrl | KeyModifiers::Shift}, ImportFilesCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.import_directory", KeyChord{KeyCode::D, KeyModifiers::Ctrl | KeyModifiers::Shift}, ImportDirectoryCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.import_directories", KeyChord{KeyCode::D, KeyModifiers::Ctrl | KeyModifiers::Alt | KeyModifiers::Shift}, ImportDirectoriesCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.import_directories_recursive", KeyChord{KeyCode::R, KeyModifiers::Ctrl | KeyModifiers::Alt | KeyModifiers::Shift}, ImportDirectoriesRecursiveCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.export_raw_tsv", KeyChord{KeyCode::E, KeyModifiers::Ctrl | KeyModifiers::Shift}, ExportRawTsvCommand, {}, KeymapLayer::Project, true});
		const ContextExpr projectTreeFocused{"jfm.project_tree.focused"};
		registerBinding(resolver, {"jfm.tree.copy", KeyChord{KeyCode::C, KeyModifiers::Ctrl}, CommandID{JFMCopyTreeItemCommandId}, projectTreeFocused, KeymapLayer::WindowLocal, true});
		registerBinding(resolver, {"jfm.tree.paste", KeyChord{KeyCode::V, KeyModifiers::Ctrl}, CommandID{JFMRequestPasteTreeItemCommandId}, projectTreeFocused, KeymapLayer::WindowLocal, true});
		registerBinding(resolver, {"jfm.tree.duplicate", KeyChord{KeyCode::D, KeyModifiers::Ctrl}, CommandID{JFMRequestDuplicateTreeItemCommandId}, projectTreeFocused, KeymapLayer::WindowLocal, true});
		registerBinding(resolver, {"jfm.tree.delete", KeyChord{KeyCode::Delete, KeyModifiers::None}, CommandID{JFMDeleteTreeItemCommandId}, projectTreeFocused, KeymapLayer::WindowLocal, true});
		registerBinding(resolver, {"jfm.tree.rename", KeyChord{KeyCode::F2, KeyModifiers::None}, CommandID{JFMRequestRenameTreeItemCommandId}, projectTreeFocused, KeymapLayer::WindowLocal, true});
	}
} // namespace SST::Prototypes::JFM
