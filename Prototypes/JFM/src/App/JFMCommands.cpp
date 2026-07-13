#include "Core/dspch.hpp"

#include "App/JFMCommands.hpp"

#include "App/NativeFileDialog.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

#if defined(DS_PLATFORM_WINDOWS)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace SST::Prototypes::JFM
{

	const CommandID NewProjectCommand{"jfm.project.new"};
	const CommandID OpenProjectCommand{"jfm.project.open"};
	const CommandID SaveProjectCommand{"jfm.project.save"};
	const CommandID SaveProjectAsCommand{"jfm.project.save_as"};
	const CommandID OpenNewProjectWindowCommand{"jfm.project.open_new_window"};
	const CommandID ImportFilesCommand{"jfm.import.files"};
	const CommandID ImportDirectoryCommand{"jfm.import.directory"};
	const CommandID ImportDirectoriesCommand{"jfm.import.directories"};
	const CommandID ImportDirectoriesRecursiveCommand{"jfm.import.directories_recursive"};
	const CommandID ExportRawTsvCommand{"jfm.export.raw_tsv"};
	const CommandID ExportSeriesRawTsvCommand{"jfm.export.series_raw_tsv"};
	const CommandID ExportGroupRawTsvCommand{"jfm.export.group_raw_tsv"};
	const CommandID AddSeriesCommand{"jfm.project_tree.add_series"};
	const CommandID DeleteSeriesCommand{"jfm.project_tree.delete_series"};
	const CommandID DeleteCharacteristicCommand{"jfm.project_tree.delete_characteristic"};
	const CommandID AddScriptCommand{"jfm.scripts.add"};
	const CommandID RemoveScriptCommand{"jfm.scripts.remove"};
	const CommandID ReloadScriptCommand{"jfm.scripts.reload"};

	constexpr NativeFileDialogFilter JFMProjectFilter{"JFM Project", "jfm.yaml,yaml"};
	constexpr NativeFileDialogFilter JFMDataFilter{"JFM Data", "dat,iv,txt,csv"};
	constexpr NativeFileDialogFilter TsvFilter{"TSV", "tsv"};
	constexpr NativeFileDialogFilter PythonScriptFilter{"Python", "py"};

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

	void queueSave(EventBus &eventBus, const Ref<JFMWorkspaceStore> &workspace, const Path &path);

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
		std::string text = path.String();
		if (text.size() >= 9 && text.substr(text.size() - 9) == ".jfm.yaml")
			return path;
		return Path::FromResolved(path.Native().string() + ".jfm.yaml");
	}

	Path ensureExtension(Path path, const char *extension)
	{
		const std::string text = path.String();
		const std::string suffix = extension;
		if (text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix)
			return path;
		return Path::FromResolved(path.Native().string() + suffix);
	}

	bool isDirectory(const Path &path)
	{
		std::error_code error;
		return std::filesystem::is_directory(path.Native(), error);
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

	std::optional<Path> chooseProjectOpenPath(const Ref<JFMUserSettingsStore> &settings)
	{
		return OpenFileDialog(JFMProjectFilter, settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
	}

	std::optional<Path> chooseProjectSavePath(const Ref<JFMUserSettingsStore> &settings)
	{
		std::optional<Path> path = SaveFileDialog(JFMProjectFilter, "project.jfm.yaml", settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
		if (!path)
			return std::nullopt;
		return ensureProjectExtension(*path);
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

	std::vector<JFMExportItem> makeExportItems(const std::vector<JFMCharacteristic> &characteristics, Path prefix = {})
	{
		std::vector<JFMExportItem> items;
		items.reserve(characteristics.size());
		for (const JFMCharacteristic &characteristic : characteristics)
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

	void queueBatchExport(EventBus &eventBus, const Path &directory, std::vector<JFMExportItem> items)
	{
		if (!items.empty())
			eventBus.Queue(ExportRawTsvBatchRequested{directory, std::move(items)});
	}

	Result<void> handleNewProject(Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		workspace->ResetProject("Untitled JFM Project");
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
				eventBus->Queue(ProjectLoadRequested{*path});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Open project failed.", error.what())};
		}
	}

	Result<void> handleSaveProject(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		try
		{
			std::optional<Path> path = workspace->GetProjectPath();
			if (!path)
				path = chooseProjectSavePath(settings);
			if (path)
				queueSave(*eventBus, workspace, *path);
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
			std::optional<Path> path = chooseProjectSavePath(settings);
			if (path)
				queueSave(*eventBus, workspace, *path);
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Save project as failed.", error.what())};
		}
	}

	Result<void> handleOpenNewProjectWindow(CommandContext &)
	{
#if defined(DS_PLATFORM_WINDOWS)
		wchar_t executable[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0)
			return Result<void>{makeCommandError("Open new project window failed.", "Could not resolve current executable path.")};

		std::wstring commandLine = L"\"";
		commandLine += executable;
		commandLine += L"\"";
		STARTUPINFOW startupInfo{};
		startupInfo.cb = sizeof(startupInfo);
		PROCESS_INFORMATION processInfo{};
		if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo))
			return Result<void>{makeCommandError("Open new project window failed.", "CreateProcessW failed with code " + std::to_string(GetLastError()))};
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		return Result<void>{};
#else
		return Result<void>{makeCommandError("Open new project window failed.", "Launching a detached JFM instance is only implemented on Windows.")};
#endif
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
			std::string targetSeriesId = workspace->GetSelectedSeriesId();
			if (std::string *contextSeriesId = context.TryGet<std::string>("target_series_id"))
				targetSeriesId = *contextSeriesId;
			if (!paths.empty() && dialog != nullptr)
				dialog->Open(JFMImportDialogRequest{paths, targetSeriesId, false});
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
			std::string targetSeriesId = workspace->GetSelectedSeriesId();
			if (std::string *contextSeriesId = context.TryGet<std::string>("target_series_id"))
				targetSeriesId = *contextSeriesId;
			if (dialog != nullptr)
				dialog->Open(JFMImportDialogRequest{paths, targetSeriesId, recursive});
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
			std::vector<JFMCharacteristic> selected = workspace->GetSelectedCharacteristics();
			if (selected.empty())
			{
				workspace->SetStatusMessage("Raw TSV export failed: no characteristic selected.");
				return Result<void>{};
			}
			if (selected.size() > 1)
			{
				std::optional<Path> directory = chooseOutputDirectory(settings);
				if (directory)
					queueBatchExport(*eventBus, *directory, makeExportItems(selected));
				return Result<void>{};
			}
			const std::string defaultName = selected.front().info.name.empty() ? "raw.tsv" : selected.front().info.name + ".tsv";
			std::optional<Path> path = chooseRawTsvSavePath(defaultName);
			if (path)
				eventBus->Queue(ExportRawTsvRequested{*path, selected.front()});
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

			const JFMProject project = workspace->GetProjectSnapshot();
			auto series = findSeries(project, seriesId);
			if (!series)
				return Result<void>{};

			std::optional<Path> directory = chooseOutputDirectory(settings);
			if (!directory)
				return Result<void>{};

			queueBatchExport(*eventBus, *directory, makeExportItems(workspace->GetSeriesCharacteristics(seriesId), Path(safeFileName(series->name))));
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
			std::string groupId;
			if (std::string *contextGroupId = context.TryGet<std::string>("group_id"))
				groupId = *contextGroupId;
			if (groupId.empty())
				return Result<void>{};

			const JFMProject project = workspace->GetProjectSnapshot();
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
					workspace->GetSeriesCharacteristics(series.id),
					groupPath(project, series.groupId) / Path(safeFileName(series.name)));
				items.insert(items.end(), seriesItems.begin(), seriesItems.end());
			}
			queueBatchExport(*eventBus, *directory, std::move(items));
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Group TSV export failed.", error.what())};
		}
	}

	Result<void> handleAddSeries(Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		std::string groupId;
		if (std::string *contextGroupId = context.TryGet<std::string>("group_id"))
			groupId = *contextGroupId;
		workspace->AddSeries("Series", groupId);
		return Result<void>{};
	}

	Result<void> handleDeleteSeries(Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		std::string seriesId = workspace->GetSelectedSeriesId();
		if (std::string *contextSeriesId = context.TryGet<std::string>("series_id"))
			seriesId = *contextSeriesId;
		(void)workspace->DeleteSeries(seriesId);
		return Result<void>{};
	}

	Result<void> handleDeleteCharacteristic(Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		if (CharacteristicId *contextId = context.TryGet<CharacteristicId>("characteristic_id"))
		{
			(void)workspace->DeleteCharacteristic(*contextId);
			return Result<void>{};
		}

		for (const CharacteristicId id : workspace->GetSelectedCharacteristicIds())
			(void)workspace->DeleteCharacteristic(id);
		return Result<void>{};
	}

	Result<void> handleAddScript(Ref<JFMWorkspaceStore> workspace, Ref<JFMUserSettingsStore> settings, CommandContext &)
	{
		if (auto path = OpenFileDialog(PythonScriptFilter, settings != nullptr ? settings->GetDefaultScriptsDirectory() : Path{}))
			(void)workspace->AddScript(*path);
		return Result<void>{};
	}

	Result<void> handleRemoveScript(Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		if (std::string *scriptId = context.TryGet<std::string>("script_id"))
			(void)workspace->RemoveScript(*scriptId);
		return Result<void>{};
	}

	Result<void> handleReloadScript(Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		if (std::string *scriptId = context.TryGet<std::string>("script_id"))
			(void)workspace->ReloadScript(*scriptId);
		return Result<void>{};
	}

	void queueSave(EventBus &eventBus, const Ref<JFMWorkspaceStore> &workspace, const Path &path)
	{
		eventBus.Queue(ProjectSaveRequested{path, workspace->GetProjectSnapshot()});
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
			CommandMeta{SaveProjectCommand, "Save Project", "JFM", "Save the current JFM project.", {}, CommandFlags::AsyncCapable},
			std::bind(handleSaveProject, eventBus, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{SaveProjectAsCommand, "Save Project As...", "JFM", "Save the current JFM project to a new file.", {}, CommandFlags::AsyncCapable},
			std::bind(handleSaveProjectAs, eventBus, workspace, settings, std::placeholders::_1));

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
			CommandMeta{AddSeriesCommand, "Add Series", "JFM", "Add a new series.", {}, CommandFlags::None},
			std::bind(handleAddSeries, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{DeleteSeriesCommand, "Delete Series", "JFM", "Delete a series and its characteristics.", {}, CommandFlags::None},
			std::bind(handleDeleteSeries, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{DeleteCharacteristicCommand, "Delete Characteristic", "JFM", "Delete selected characteristic(s).", {}, CommandFlags::None},
			std::bind(handleDeleteCharacteristic, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{AddScriptCommand, "Add Import Script", "JFM", "Register a custom Python import script.", {}, CommandFlags::None},
			std::bind(handleAddScript, workspace, settings, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{RemoveScriptCommand, "Remove Import Script", "JFM", "Remove a custom Python import script.", {}, CommandFlags::None},
			std::bind(handleRemoveScript, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ReloadScriptCommand, "Reload Import Script", "JFM", "Reload a custom Python import script.", {}, CommandFlags::None},
			std::bind(handleReloadScript, workspace, std::placeholders::_1));

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
	}
} // namespace SST::Prototypes::JFM
