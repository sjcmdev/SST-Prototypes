#include "Core/dspch.hpp"

#include "App/JFMCommands.hpp"

#include <filesystem>

#include <nfd.h>

#include "Core/Commands/CommandRegistry.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Input/KeymapResolver.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/KeyCodes.hpp"
#include "Events/JFMEvents.hpp"

namespace SST::Prototypes::JFM
{

	const CommandID NewProjectCommand{"jfm.project.new"};
	const CommandID OpenProjectCommand{"jfm.project.open"};
	const CommandID SaveProjectCommand{"jfm.project.save"};
	const CommandID SaveProjectAsCommand{"jfm.project.save_as"};
	const CommandID ImportFilesCommand{"jfm.import.files"};
	const CommandID ImportDirectoryCommand{"jfm.import.directory"};
	const CommandID ExportRawTsvCommand{"jfm.export.raw_tsv"};

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

	std::string lastNfdError()
	{
		const char *error = NFD_GetError();
		return error != nullptr ? std::string(error) : "Native file dialog failed.";
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

	std::optional<Path> chooseProjectOpenPath()
	{
		NFD_Init();
		nfdu8filteritem_t filters[] = {{"JFM Project", "jfm.yaml,yaml"}};
		nfdu8char_t *rawPath = nullptr;
		const nfdresult_t result = NFD_OpenDialogU8(&rawPath, filters, 1, nullptr);
		if (result == NFD_CANCEL)
		{
			NFD_Quit();
			return std::nullopt;
		}
		if (result == NFD_ERROR)
		{
			const std::string error = lastNfdError();
			NFD_Quit();
			throw std::runtime_error(error);
		}

		std::string pathText(rawPath);
		NFD_FreePathU8(rawPath);
		NFD_Quit();
		return Path::FromResolved(pathText);
	}

	std::optional<Path> chooseProjectSavePath()
	{
		NFD_Init();
		nfdu8filteritem_t filters[] = {{"JFM Project", "jfm.yaml,yaml"}};
		nfdu8char_t *rawPath = nullptr;
		const nfdresult_t result = NFD_SaveDialogU8(&rawPath, filters, 1, nullptr, "project.jfm.yaml");
		if (result == NFD_CANCEL)
		{
			NFD_Quit();
			return std::nullopt;
		}
		if (result == NFD_ERROR)
		{
			const std::string error = lastNfdError();
			NFD_Quit();
			throw std::runtime_error(error);
		}

		std::string pathText(rawPath);
		NFD_FreePathU8(rawPath);
		NFD_Quit();
		return ensureProjectExtension(Path::FromResolved(pathText));
	}

	std::optional<Path> chooseRawTsvSavePath(const std::string &defaultName)
	{
		NFD_Init();
		nfdu8filteritem_t filters[] = {{"TSV", "tsv"}};
		nfdu8char_t *rawPath = nullptr;
		const nfdresult_t result = NFD_SaveDialogU8(&rawPath, filters, 1, nullptr, defaultName.c_str());
		if (result == NFD_CANCEL)
		{
			NFD_Quit();
			return std::nullopt;
		}
		if (result == NFD_ERROR)
		{
			const std::string error = lastNfdError();
			NFD_Quit();
			throw std::runtime_error(error);
		}

		std::string pathText(rawPath);
		NFD_FreePathU8(rawPath);
		NFD_Quit();
		return ensureExtension(Path::FromResolved(pathText), ".tsv");
	}

	std::vector<Path> chooseImportFiles()
	{
		NFD_Init();
		nfdu8filteritem_t filters[] = {{"JFM Data", "dat,iv,txt,csv"}};
		const nfdpathset_t *pathSet = nullptr;
		const nfdresult_t result = NFD_OpenDialogMultipleU8(&pathSet, filters, 1, nullptr);
		if (result == NFD_CANCEL)
		{
			NFD_Quit();
			return {};
		}
		if (result == NFD_ERROR)
		{
			const std::string error = lastNfdError();
			NFD_Quit();
			throw std::runtime_error(error);
		}

		nfdpathsetsize_t count = 0;
		std::vector<Path> paths;
		if (NFD_PathSet_GetCount(pathSet, &count) == NFD_OKAY)
		{
			for (nfdpathsetsize_t index = 0; index < count; ++index)
			{
				nfdu8char_t *rawPath = nullptr;
				if (NFD_PathSet_GetPathU8(pathSet, index, &rawPath) == NFD_OKAY && rawPath != nullptr)
				{
					paths.push_back(Path::FromResolved(std::string(rawPath)));
					NFD_PathSet_FreePathU8(rawPath);
				}
			}
		}

		NFD_PathSet_Free(pathSet);
		NFD_Quit();
		return paths;
	}

	std::optional<Path> chooseImportDirectory()
	{
		NFD_Init();
		nfdu8char_t *rawPath = nullptr;
		const nfdresult_t result = NFD_PickFolderU8(&rawPath, nullptr);
		if (result == NFD_CANCEL)
		{
			NFD_Quit();
			return std::nullopt;
		}
		if (result == NFD_ERROR)
		{
			const std::string error = lastNfdError();
			NFD_Quit();
			throw std::runtime_error(error);
		}

		std::string pathText(rawPath);
		NFD_FreePathU8(rawPath);
		NFD_Quit();
		return Path::FromResolved(pathText);
	}

	std::vector<Path> dataFilesInDirectory(const Path &directory)
	{
		std::vector<Path> paths;
		for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Native()))
		{
			if (!entry.is_regular_file())
				continue;
			const std::string extension = entry.path().extension().string();
			if (extension == ".dat" || extension == ".DAT" || extension == ".iv" || extension == ".IV")
				paths.push_back(Path::FromResolved(entry.path()));
		}
		return paths;
	}

	Result<void> handleNewProject(Ref<JFMWorkspaceStore> workspace, CommandContext &)
	{
		workspace->ResetProject("Untitled JFM Project");
		return Result<void>{};
	}

	Result<void> handleOpenProject(Ref<EventBus> eventBus, CommandContext &context)
	{
		try
		{
			std::optional<Path> path;
			if (Path *contextPath = context.TryGet<Path>("path"))
				path = *contextPath;
			else
				path = chooseProjectOpenPath();
			if (path)
				eventBus->Queue(ProjectLoadRequested{*path});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Open project failed.", error.what())};
		}
	}

	Result<void> handleSaveProject(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, CommandContext &)
	{
		try
		{
			std::optional<Path> path = workspace->GetProjectPath();
			if (!path)
				path = chooseProjectSavePath();
			if (path)
				queueSave(*eventBus, workspace, *path);
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Save project failed.", error.what())};
		}
	}

	Result<void> handleSaveProjectAs(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, CommandContext &)
	{
		try
		{
			std::optional<Path> path = chooseProjectSavePath();
			if (path)
				queueSave(*eventBus, workspace, *path);
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Save project as failed.", error.what())};
		}
	}

	Result<void> handleImportFiles(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, CommandContext &context)
	{
		try
		{
			std::vector<Path> paths;
			if (std::vector<Path> *contextPaths = context.TryGet<std::vector<Path>>("paths"))
				paths = *contextPaths;
			else
				paths = chooseImportFiles();
			if (!paths.empty())
				eventBus->Queue(ImportRequested{paths, workspace->GetSelectedSeriesId()});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Import files failed.", error.what())};
		}
	}

	Result<void> handleImportDirectory(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, CommandContext &)
	{
		try
		{
			const std::optional<Path> directory = chooseImportDirectory();
			if (!directory)
				return Result<void>{};
			std::vector<Path> paths = dataFilesInDirectory(*directory);
			if (!paths.empty())
				eventBus->Queue(ImportRequested{paths, workspace->GetSelectedSeriesId()});
			else
				workspace->SetStatusMessage("No .dat or .iv files found in selected directory.");
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Import directory failed.", error.what())};
		}
	}

	Result<void> handleExportRawTsv(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> workspace, CommandContext &)
	{
		try
		{
			auto selected = workspace->GetSelectedCharacteristic();
			if (!selected)
			{
				workspace->SetStatusMessage("Raw TSV export failed: no characteristic selected.");
				return Result<void>{};
			}
			const std::string defaultName = selected->name.empty() ? "raw.tsv" : selected->name + ".tsv";
			std::optional<Path> path = chooseRawTsvSavePath(defaultName);
			if (path)
				eventBus->Queue(ExportRawTsvRequested{*path, *selected});
			return Result<void>{};
		}
		catch (const std::exception &error)
		{
			return Result<void>{makeCommandError("Raw TSV export failed.", error.what())};
		}
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
		if (registry == nullptr || resolver == nullptr || eventBus == nullptr || workspace == nullptr)
			throw std::runtime_error("JFM startup failed: command registry, keymap resolver, event bus, or workspace is unavailable.");

		registerCommand(
			registry,
			CommandMeta{NewProjectCommand, "New Project", "JFM", "Create a new JFM project.", {}, CommandFlags::None},
			std::bind(handleNewProject, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{OpenProjectCommand, "Open Project...", "JFM", "Open a JFM project file.", {}, CommandFlags::AsyncCapable},
			std::bind(handleOpenProject, eventBus, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{SaveProjectCommand, "Save Project", "JFM", "Save the current JFM project.", {}, CommandFlags::AsyncCapable},
			std::bind(handleSaveProject, eventBus, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{SaveProjectAsCommand, "Save Project As...", "JFM", "Save the current JFM project to a new file.", {}, CommandFlags::AsyncCapable},
			std::bind(handleSaveProjectAs, eventBus, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ImportFilesCommand, "Import Files", "JFM", "Import JFM data files through Python.", {}, CommandFlags::AsyncCapable},
			std::bind(handleImportFiles, eventBus, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ImportDirectoryCommand, "Import Directory", "JFM", "Import a directory of JFM data files through Python.", {}, CommandFlags::AsyncCapable},
			std::bind(handleImportDirectory, eventBus, workspace, std::placeholders::_1));

		registerCommand(
			registry,
			CommandMeta{ExportRawTsvCommand, "Export Raw TSV", "JFM", "Export selected raw data as TSV.", {}, CommandFlags::AsyncCapable},
			std::bind(handleExportRawTsv, eventBus, workspace, std::placeholders::_1));

		registerBinding(resolver, {"jfm.new_project", KeyChord{KeyCode::N, KeyModifiers::Ctrl}, NewProjectCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.open_project", KeyChord{KeyCode::O, KeyModifiers::Ctrl}, OpenProjectCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.save_project", KeyChord{KeyCode::S, KeyModifiers::Ctrl}, SaveProjectCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.save_project_as", KeyChord{KeyCode::S, KeyModifiers::Ctrl | KeyModifiers::Shift}, SaveProjectAsCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.redo_alt", KeyChord{KeyCode::Z, KeyModifiers::Ctrl | KeyModifiers::Shift}, CommandID{"edit.redo"}, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.import_files", KeyChord{KeyCode::I, KeyModifiers::Ctrl | KeyModifiers::Shift}, ImportFilesCommand, {}, KeymapLayer::Project, true});
		registerBinding(resolver, {"jfm.export_raw_tsv", KeyChord{KeyCode::E, KeyModifiers::Ctrl | KeyModifiers::Shift}, ExportRawTsvCommand, {}, KeymapLayer::Project, true});
	}
} // namespace SST::Prototypes::JFM
