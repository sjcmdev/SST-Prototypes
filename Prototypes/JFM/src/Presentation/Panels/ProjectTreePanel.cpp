#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include "App/NativeFileDialog.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <sstream>

#include <imgui.h>

namespace SST::Prototypes::JFM
{
	namespace
	{
		constexpr NativeFileDialogFilter PythonScriptFilter{"Python", "py"};

		bool containsId(const std::vector<CharacteristicId> &ids, CharacteristicId id)
		{
			return std::find(ids.begin(), ids.end(), id) != ids.end();
		}

		void appendUnique(std::vector<CharacteristicId> &ids, CharacteristicId id)
		{
			if (id != InvalidCharacteristicId && !containsId(ids, id))
				ids.push_back(id);
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

		std::string shortNameFrom(const std::unordered_map<std::string, std::string> &metadata)
		{
			const auto it = metadata.find("short_name");
			return it == metadata.end() ? std::string{} : it->second;
		}

		std::string displayName(const std::string &name, const std::unordered_map<std::string, std::string> &metadata)
		{
			const std::string shortName = shortNameFrom(metadata);
			return shortName.empty() ? name : shortName;
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

		std::string formatTemperature(std::optional<double> temperature)
		{
			if (!temperature)
				return {};
			std::ostringstream stream;
			stream << *temperature;
			return stream.str();
		}

		std::string expandShortNamePattern(std::string pattern, const JFMCharacteristicInfo &info)
		{
			const std::string stem = info.sourcePath.Empty() ? info.name : info.sourcePath.Native().stem().string();
			const std::string folder = info.sourcePath.Empty() ? std::string{} : info.sourcePath.parent_path().filename().String();
			const std::string parentFolder = info.sourcePath.Empty() ? std::string{} : info.sourcePath.parent_path().parent_path().filename().String();
			const std::string temperature = formatTemperature(info.temperatureK);
			pattern = replaceAll(std::move(pattern), "{name}", info.name);
			pattern = replaceAll(std::move(pattern), "{id}", std::to_string(info.id));
			pattern = replaceAll(std::move(pattern), "{stem}", stem);
			pattern = replaceAll(std::move(pattern), "{folder}", folder);
			pattern = replaceAll(std::move(pattern), "{parent_folder}", parentFolder);
			pattern = replaceAll(std::move(pattern), "{T}", temperature);
			pattern = replaceAll(std::move(pattern), "{temperature_K}", temperature);
			for (const auto &[key, value] : info.metadata)
				pattern = replaceAll(std::move(pattern), "{" + key + "}", value);
			return pattern;
		}

		std::optional<std::reference_wrapper<const JFMGroup>> findGroup(const JFMProject &project, const std::string &groupId)
		{
			const auto it = std::find_if(project.groups.begin(), project.groups.end(), [&groupId](const JFMGroup &group) {
				return group.id == groupId;
			});
			if (it == project.groups.end())
				return std::nullopt;
			return std::cref(*it);
		}

		std::optional<std::reference_wrapper<const JFMSeries>> findSeries(const JFMProject &project, const std::string &seriesId)
		{
			const auto it = std::find_if(project.series.begin(), project.series.end(), [&seriesId](const JFMSeries &series) {
				return series.id == seriesId;
			});
			if (it == project.series.end())
				return std::nullopt;
			return std::cref(*it);
		}

		Path groupPath(const JFMProject &project, std::string groupId)
		{
			std::vector<std::string> names;
			while (!groupId.empty())
			{
				auto group = findGroup(project, groupId);
				if (!group)
					break;
				names.push_back(safeFileName(group->get().name));
				groupId = group->get().parentId;
			}

			Path path;
			for (auto it = names.rbegin(); it != names.rend(); ++it)
				path = path / Path(*it);
			return path;
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

		std::optional<Path> chooseOutputDirectory(const Ref<JFMUserSettingsStore> &settings)
		{
			std::vector<Path> paths = PickFoldersDialog(false, settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
			if (paths.empty())
				return std::nullopt;
			return paths.front();
		}

		std::vector<JFMExportItem> makeExportItems(const std::vector<JFMCharacteristic> &characteristics, const Path &prefix)
		{
			std::vector<JFMExportItem> items;
			items.reserve(characteristics.size());
			for (const JFMCharacteristic &characteristic : characteristics)
				items.push_back(JFMExportItem{prefix / Path(safeFileName(characteristic.info.name) + ".tsv"), characteristic});
			return items;
		}

		std::vector<CharacteristicId> sortedCharacteristicIds(
			const JFMSeries &series,
			const Ref<JFMWorkspaceStore> &store,
			ProjectHandle project,
			int sortMode)
		{
			std::vector<CharacteristicId> ids = series.characteristicIds;
			std::sort(ids.begin(), ids.end(), [&](CharacteristicId left, CharacteristicId right) {
				const auto leftCharacteristic = store->FindCharacteristic({project, left});
				const auto rightCharacteristic = store->FindCharacteristic({project, right});
				if (!leftCharacteristic || !rightCharacteristic)
					return left < right;
				if (sortMode == 1 && leftCharacteristic->info.temperatureK != rightCharacteristic->info.temperatureK)
					return leftCharacteristic->info.temperatureK.value_or(-1.0) < rightCharacteristic->info.temperatureK.value_or(-1.0);
				return leftCharacteristic->info.name < rightCharacteristic->info.name;
			});
			return ids;
		}

		std::string seriesIdForCharacteristic(const JFMProject &project, CharacteristicId id)
		{
			for (const JFMSeries &series : project.series)
			{
				if (containsId(series.characteristicIds, id))
					return series.id;
			}
			return {};
		}

		std::vector<CharacteristicId> idsForGroup(
			const Ref<JFMWorkspaceStore> &store,
			ProjectHandle project,
			const std::string &groupId)
		{
			std::vector<CharacteristicId> ids;
			if (store == nullptr)
				return ids;
			for (const JFMCharacteristic &characteristic : store->GetGroupCharacteristics(project, groupId))
				ids.push_back(characteristic.info.id);
			return ids;
		}
	}

	ProjectTreePanel::ProjectTreePanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<JFMImportDialogState> importDialog,
		Ref<JFMPlotRequestState> plotRequests,
		Ref<EventBus> eventBus,
		WeakRef<CommandRegistry> commandRegistry,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_ImportDialog(std::move(importDialog)),
		  m_PlotRequests(std::move(plotRequests)),
		  m_EventBus(std::move(eventBus)),
		  m_CommandRegistry(std::move(commandRegistry))
	{
	}

	Ref<IPanel> ProjectTreePanel::Clone() const
	{
		return CreateRef<ProjectTreePanel>(*this);
	}

	void ProjectTreePanel::ExecuteCommand(const char *commandId, CommandContext context)
	{
		auto registry = m_CommandRegistry.lock();
		if (registry == nullptr)
		{
			if (m_Store != nullptr)
				m_Store->SetStatusMessage(m_CurrentProject, "Command runtime unavailable.");
			return;
		}
		context.SetContextId(ContextID{"jfm.project_tree"});
		context.SetSource("Project Tree");
		(void)registry->Execute(CommandID{commandId}, std::move(context));
	}

	void ProjectTreePanel::ExecuteEdit(std::string description, JFMProjectEditRequest::Apply mutation)
	{
		CommandContext context;
		context.Set(
			"edit",
			JFMProjectEditRequest{
				m_CurrentProject,
				std::move(description),
				std::move(mutation)});
		ExecuteCommand(JFMApplyProjectEditCommandId, std::move(context));
	}

	void ProjectTreePanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			ImGui::End();
			SetVisible(visible);
			return;
		}

		if (m_Store == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		const char *sortModes[] = {"Name", "Temperature"};
		ImGui::SetNextItemWidth(150.0f);
		ImGui::Combo("Sort", &m_SortMode, sortModes, IM_ARRAYSIZE(sortModes));

		m_CurrentProject = m_Store->GetActiveProjectHandle();
		const JFMProject project = m_Store->GetProjectSnapshot(m_CurrentProject);
		TreeRenderState state;
		state.primarySelection = m_Store->GetSelectedCharacteristicId();
		state.selectedIds = m_Store->GetSelectedCharacteristicIds();

		for (const JFMProjectDocumentInfo &document : m_Store->GetProjects())
		{
			if (document.handle == m_CurrentProject)
			{
				RenderProjectNode(document.handle, project, state);
				continue;
			}

			const std::string label = document.name + (document.dirty ? " *" : "") + "##project_" + std::to_string(document.handle);
			const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_None);
			if (ImGui::IsItemClicked())
				(void)m_Store->SetActiveProject(document.handle);
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Set Active"))
					(void)m_Store->SetActiveProject(document.handle);
				ImGui::EndPopup();
			}
			if (open)
			{
				ImGui::TextDisabled("Inactive project");
				ImGui::TextWrapped("%s", document.status.c_str());
				ImGui::TreePop();
			}
		}
		HandleKeyboard(project, state);
		RenderRenamePopup();
		RenderShortNamePopup();
		RenderPastePopup(project);
		RenderGroupingPopup(project);

		ImGui::End();
		SetVisible(visible);
	}

	void ProjectTreePanel::FocusItem(RenameTarget target, std::string id, std::string name)
	{
		m_FocusedTarget = target;
		m_FocusedTargetId = std::move(id);
		m_FocusedTargetName = std::move(name);
	}

	void ProjectTreePanel::StartRename(RenameTarget target, std::string id, std::string name)
	{
		if (target == RenameTarget::None || name.empty())
			return;
		m_RenameTarget = target;
		m_RenameTargetId = std::move(id);
		std::snprintf(m_RenameBuffer.data(), m_RenameBuffer.size(), "%s", name.c_str());
		m_OpenRenamePopup = true;
	}

	void ProjectTreePanel::StartShortNameEdit(const JFMProject &project, RenameTarget target, std::string id, const std::string &fallbackName)
	{
		std::string shortName;
		if (target == RenameTarget::Group)
		{
			if (const auto group = findGroup(project, id))
				shortName = shortNameFrom(group->get().metadata);
		}
		else if (target == RenameTarget::Series)
		{
			if (const auto series = findSeries(project, id))
				shortName = shortNameFrom(series->get().metadata);
		}
		else if (target == RenameTarget::Characteristic)
		{
			const CharacteristicId characteristicId = static_cast<CharacteristicId>(std::stoull(id));
			const auto characteristic = m_Store->FindCharacteristic({m_CurrentProject, characteristicId});
			if (characteristic)
				shortName = shortNameFrom(characteristic->info.metadata);
			const std::vector<CharacteristicId> selectedIds = m_Store->GetSelectedCharacteristicIds();
			m_ShortNameTargetIds = containsId(selectedIds, characteristicId) ? selectedIds : std::vector<CharacteristicId>{characteristicId};
		}
		else
		{
			return;
		}

		m_ShortNameTarget = target;
		m_ShortNameTargetId = std::move(id);
		if (m_ShortNameTarget != RenameTarget::Characteristic)
			m_ShortNameTargetIds.clear();
		std::snprintf(m_ShortNameBuffer.data(), m_ShortNameBuffer.size(), "%s", shortName.empty() ? fallbackName.c_str() : shortName.c_str());
		m_OpenShortNamePopup = true;
	}

	void ProjectTreePanel::QueuePlot(std::vector<CharacteristicId> ids, std::string title)
	{
		if (m_PlotRequests != nullptr)
			m_PlotRequests->QueuePlot(std::move(ids), std::move(title), m_CurrentProject);
	}

	void ProjectTreePanel::QueuePlotsForProjectGroups(const JFMProject &project)
	{
		for (const JFMGroup &group : project.groups)
		{
			if (group.parentId.empty())
				QueuePlot(idsForGroup(m_Store, m_CurrentProject, group.id), displayName(group.name, group.metadata));
		}
	}

	void ProjectTreePanel::QueuePlotsForChildGroups(const JFMProject &project, const std::string &parentGroupId)
	{
		for (const JFMGroup &group : project.groups)
		{
			if (group.parentId == parentGroupId)
				QueuePlot(idsForGroup(m_Store, m_CurrentProject, group.id), displayName(group.name, group.metadata));
		}
	}

	void ProjectTreePanel::OpenImportDialog(std::string targetSeriesId, std::string targetGroupId)
	{
		CommandContext context;
		context.Set("project", m_CurrentProject);
		context.Set("target_series_id", std::move(targetSeriesId));
		context.Set("target_group_id", std::move(targetGroupId));
		ExecuteCommand("jfm.import.files", std::move(context));
	}

	void ProjectTreePanel::OpenImportFolderDialog(std::string targetSeriesId, std::string targetGroupId, bool recursive)
	{
		CommandContext context;
		context.Set("project", m_CurrentProject);
		context.Set("target_series_id", std::move(targetSeriesId));
		context.Set("target_group_id", std::move(targetGroupId));
		ExecuteCommand(recursive ? "jfm.import.directories_recursive" : "jfm.import.directory", std::move(context));
	}

	void ProjectTreePanel::RenderProjectNode(ProjectHandle projectHandle, const JFMProject &project, TreeRenderState &state)
	{
		const std::string projectLabel = project.name + (m_Store->IsDirty(projectHandle) ? " *" : "") + " [active]##project_" + std::to_string(projectHandle);
		if (!ImGui::TreeNodeEx(projectLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			return;

		if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
			FocusItem(RenameTarget::Project, {}, project.name);
		if (ImGui::BeginPopupContextItem())
		{
			FocusItem(RenameTarget::Project, {}, project.name);
			if (ImGui::MenuItem("Add Group"))
				ExecuteEdit("Add group", [](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					(void)workspace.AddGroup(handle, "Group");
					return Result<void>{};
				});
			if (ImGui::MenuItem("Add Series"))
				ExecuteEdit("Add series", [](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					(void)workspace.AddSeries(handle, "Series");
					return Result<void>{};
				});
			if (ImGui::MenuItem("Import Series"))
				OpenImportDialog();
			if (ImGui::MenuItem("Import Folder as Series"))
				OpenImportFolderDialog({}, {}, false);
			if (ImGui::MenuItem("Import Folder Tree"))
				OpenImportFolderDialog({}, {}, true);
			if (ImGui::MenuItem("Create Grouping"))
				OpenGroupingPopup(project);
			if (ImGui::MenuItem("Plot Project Groups"))
				QueuePlotsForProjectGroups(project);
			if (ImGui::MenuItem("Paste", nullptr, false, m_ClipboardTarget != RenameTarget::None))
				OpenPastePrompt(false);
			if (ImGui::MenuItem("Rename"))
				StartRename(RenameTarget::Project, {}, project.name);
			ImGui::EndPopup();
		}

		for (const JFMGroup &group : project.groups)
		{
			if (group.parentId.empty())
				RenderGroupNode(project, group, state);
		}
		for (const JFMSeries &series : project.series)
		{
			if (series.groupId.empty())
				RenderSeriesNode(project, series, state);
		}
		RenderGroupingsNode(project);
		RenderScriptsNode(project);
		ImGui::TreePop();
	}

	void ProjectTreePanel::RenderGroupNode(const JFMProject &project, const JFMGroup &group, TreeRenderState &state)
	{
		const std::string shownName = displayName(group.name, group.metadata);
		const bool open = ImGui::TreeNodeEx((shownName + "##" + group.id).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
			FocusItem(RenameTarget::Group, group.id, group.name);
		if (shownName != group.name && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", group.name.c_str());
		if (ImGui::BeginPopupContextItem())
		{
			FocusItem(RenameTarget::Group, group.id, group.name);
			if (ImGui::MenuItem("Add Group"))
			{
				const std::string parentId = group.id;
				ExecuteEdit("Add group", [parentId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					(void)workspace.AddGroup(handle, "Group", parentId);
					return Result<void>{};
				});
			}
			if (ImGui::MenuItem("Add Series"))
			{
				const std::string groupId = group.id;
				ExecuteEdit("Add series", [groupId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					(void)workspace.AddSeries(handle, "Series", groupId);
					return Result<void>{};
				});
			}
			if (ImGui::MenuItem("Import Files"))
				OpenImportDialog({}, group.id);
			if (ImGui::MenuItem("Import Folder as Series"))
				OpenImportFolderDialog({}, group.id, false);
			if (ImGui::MenuItem("Import Folder Tree"))
				OpenImportFolderDialog({}, group.id, true);
			if (ImGui::MenuItem("Plot Group"))
				QueuePlot(idsForGroup(m_Store, m_CurrentProject, group.id), shownName);
			if (ImGui::MenuItem("Plot Child Groups"))
				QueuePlotsForChildGroups(project, group.id);
			if (ImGui::MenuItem("Export Raw TSV"))
				ExportGroup(project, group);
			if (ImGui::MenuItem("Rename"))
				StartRename(RenameTarget::Group, group.id, group.name);
			if (ImGui::MenuItem("Edit Short Name"))
				StartShortNameEdit(project, RenameTarget::Group, group.id, shownName);
			if (ImGui::MenuItem("Copy"))
				CopyFocused(project, state.selectedIds);
			if (ImGui::MenuItem("Paste", nullptr, false, m_ClipboardTarget != RenameTarget::None))
				OpenPastePrompt(false);
			if (ImGui::MenuItem("Duplicate"))
			{
				CopyFocused(project, state.selectedIds);
				PasteClipboard(project, false);
			}
			if (ImGui::MenuItem("Delete Group"))
			{
				const std::string groupId = group.id;
				ExecuteEdit("Delete group", [groupId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					(void)workspace.DeleteGroup(handle, groupId);
					return Result<void>{};
				});
			}
			ImGui::EndPopup();
		}
		if (!open)
			return;

		for (const JFMGroup &child : project.groups)
		{
			if (child.parentId == group.id)
				RenderGroupNode(project, child, state);
		}
		for (const JFMSeries &series : project.series)
		{
			if (series.groupId == group.id)
				RenderSeriesNode(project, series, state);
		}
		ImGui::TreePop();
	}

	void ProjectTreePanel::RenderSeriesNode(const JFMProject &project, const JFMSeries &series, TreeRenderState &state)
	{
		const std::string shownName = displayName(series.name, series.metadata);
		const bool open = ImGui::TreeNodeEx((shownName + "##" + series.id).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
		if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
			FocusItem(RenameTarget::Series, series.id, series.name);
		if (shownName != series.name && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", series.name.c_str());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("JFM_CHARACTERISTIC_ID"))
			{
				const CharacteristicId id = *static_cast<const CharacteristicId *>(payload->Data);
				std::vector<CharacteristicId> idsToMove = containsId(state.selectedIds, id) ? state.selectedIds : std::vector<CharacteristicId>{id};
				const std::string targetSeriesId = series.id;
				ExecuteEdit("Move characteristics", [idsToMove, targetSeriesId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					for (CharacteristicId movedId : idsToMove)
						(void)workspace.MoveCharacteristic(handle, movedId, targetSeriesId);
					return Result<void>{};
				});
				m_Store->SetSelectedCharacteristics(idsToMove, id, series.id);
				state.selectedIds = m_Store->GetSelectedCharacteristicIds();
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem())
		{
			FocusItem(RenameTarget::Series, series.id, series.name);
			if (ImGui::MenuItem("Select Series"))
			{
				m_Store->SelectSeries(series.id);
				m_Store->SetSelectedCharacteristics(series.characteristicIds, series.characteristicIds.empty() ? InvalidCharacteristicId : series.characteristicIds.front(), series.id);
			}
			if (ImGui::MenuItem("Add Characteristics"))
				OpenImportDialog(series.id);
			if (ImGui::MenuItem("Import Folder"))
				OpenImportFolderDialog(series.id, {}, false);
			if (ImGui::MenuItem("Import Folder Recursively"))
				OpenImportFolderDialog(series.id, {}, true);
			if (ImGui::MenuItem("Plot Series"))
				QueuePlot(series.characteristicIds, shownName);
			if (ImGui::MenuItem("Export Raw TSV"))
			{
				if (auto directory = chooseOutputDirectory(m_Settings); directory && m_EventBus != nullptr)
					m_EventBus->Queue(ExportRawTsvBatchRequested{*directory, makeExportItems(m_Store->GetSeriesCharacteristics(series.id), Path(safeFileName(series.name)))});
			}
			if (ImGui::MenuItem("Rename"))
				StartRename(RenameTarget::Series, series.id, series.name);
			if (ImGui::MenuItem("Edit Short Name"))
				StartShortNameEdit(project, RenameTarget::Series, series.id, shownName);
			if (ImGui::MenuItem("Copy"))
				CopyFocused(project, state.selectedIds);
			if (ImGui::MenuItem("Paste", nullptr, false, m_ClipboardTarget != RenameTarget::None))
				OpenPastePrompt(false);
			if (ImGui::MenuItem("Duplicate"))
			{
				CopyFocused(project, state.selectedIds);
				PasteClipboard(project, false);
			}
			if (ImGui::MenuItem("Delete Series"))
			{
				const std::string seriesId = series.id;
				ExecuteEdit("Delete series", [seriesId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					(void)workspace.DeleteSeries(handle, seriesId);
					return Result<void>{};
				});
			}
			ImGui::EndPopup();
		}
		if (!open)
			return;

		const std::vector<CharacteristicId> visibleIds = sortedCharacteristicIds(series, m_Store, m_CurrentProject, m_SortMode);
		state.visibleCharacteristicIds.insert(state.visibleCharacteristicIds.end(), visibleIds.begin(), visibleIds.end());
		for (CharacteristicId characteristicId : visibleIds)
			RenderCharacteristicNode(series, characteristicId, visibleIds, state);
		if (series.characteristicIds.empty())
			ImGui::TextDisabled("No characteristics");
		ImGui::TreePop();
	}

	void ProjectTreePanel::RenderCharacteristicNode(const JFMSeries &series, CharacteristicId characteristicId, const std::vector<CharacteristicId> &visibleIds, TreeRenderState &state)
	{
		const auto characteristic = m_Store->FindCharacteristic({m_CurrentProject, characteristicId});
		const std::string name = characteristic ? characteristic->info.name : std::to_string(characteristicId);
		const std::string shownName = characteristic ? displayName(characteristic->info.name, characteristic->info.metadata) : name;
		const bool hasWarning = characteristic
			&& (!characteristic->info.qc.warnings.empty()
				|| (!characteristic->info.sourcePath.Empty() && characteristic->data.voltage.empty()));
		const bool selected = containsId(state.selectedIds, characteristicId);
		const std::string label = std::string(selected ? "[x] " : "[ ] ") + shownName + (hasWarning ? " [!]" : "") + "##" + std::to_string(characteristicId);

		if (selected)
		{
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.42f, 0.30f, 0.18f, 0.92f));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.55f, 0.38f, 0.22f, 0.96f));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.70f, 0.46f, 0.24f, 1.00f));
		}
		if (ImGui::Selectable(label.c_str(), selected))
		{
			std::vector<CharacteristicId> nextSelection = state.selectedIds;
			const ImGuiIO &io = ImGui::GetIO();
			if (io.KeyShift && state.primarySelection != InvalidCharacteristicId)
			{
				nextSelection.clear();
				bool collecting = false;
				for (CharacteristicId id : visibleIds)
				{
					if (id == state.primarySelection || id == characteristicId)
					{
						nextSelection.push_back(id);
						collecting = !collecting;
						if (state.primarySelection == characteristicId)
							break;
						continue;
					}
					if (collecting)
						nextSelection.push_back(id);
				}
			}
			else if (io.KeyCtrl)
			{
				if (containsId(nextSelection, characteristicId))
					nextSelection.erase(std::remove(nextSelection.begin(), nextSelection.end(), characteristicId), nextSelection.end());
				else
					nextSelection.push_back(characteristicId);
			}
			else
			{
				nextSelection = {characteristicId};
			}
			m_Store->SelectSeries(series.id);
			m_Store->SetSelectedCharacteristics(std::move(nextSelection), characteristicId, series.id);
			state.primarySelection = characteristicId;
			state.selectedIds = m_Store->GetSelectedCharacteristicIds();
		}
		if (selected)
			ImGui::PopStyleColor(3);
		if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
			FocusItem(RenameTarget::Characteristic, std::to_string(characteristicId), name);
		if (shownName != name && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", name.c_str());
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("JFM_CHARACTERISTIC_ID", &characteristicId, sizeof(CharacteristicId));
			if (selected && state.selectedIds.size() > 1)
				ImGui::Text("%d characteristics", static_cast<int>(state.selectedIds.size()));
			else
				ImGui::TextUnformatted(name.c_str());
			ImGui::EndDragDropSource();
		}
		if (hasWarning && ImGui::IsItemHovered())
			ImGui::SetTooltip("Raw arrays unavailable; see Import Status.");
		if (ImGui::BeginPopupContextItem())
		{
			FocusItem(RenameTarget::Characteristic, std::to_string(characteristicId), name);
			if (ImGui::MenuItem("Delete"))
			{
				const std::vector<CharacteristicId> ids = selected && state.selectedIds.size() > 1
					? state.selectedIds
					: std::vector<CharacteristicId>{characteristicId};
				ExecuteEdit("Delete characteristics", [ids](JFMWorkspaceStore &workspace, ProjectHandle handle) {
					for (CharacteristicId id : ids)
						(void)workspace.DeleteCharacteristic(handle, id);
					return Result<void>{};
				});
			}
			if (ImGui::MenuItem("Rename"))
				StartRename(RenameTarget::Characteristic, std::to_string(characteristicId), name);
			if (ImGui::MenuItem("Edit Short Name"))
				StartShortNameEdit(m_Store->GetProjectSnapshot(m_CurrentProject), RenameTarget::Characteristic, std::to_string(characteristicId), shownName);
			if (ImGui::MenuItem("Copy"))
				CopyFocused(m_Store->GetProjectSnapshot(m_CurrentProject), state.selectedIds);
			if (ImGui::MenuItem("Duplicate"))
			{
				CopyFocused(m_Store->GetProjectSnapshot(m_CurrentProject), state.selectedIds);
				PasteClipboard(m_Store->GetProjectSnapshot(m_CurrentProject), false);
			}
			ImGui::EndPopup();
		}
	}

	void ProjectTreePanel::QueueGroupingPlot(const JFMGroupingDefinition &grouping)
	{
		if (m_PlotRequests == nullptr || m_Store == nullptr)
			return;
		std::vector<JFMPlotRequest::CurveGroup> groups;
		for (const JFMGroupingBucket &bucket : m_Store->ResolveGrouping(m_CurrentProject, grouping.id))
		{
			groups.emplace_back(JFMPlotRequest::CurveGroup{
				grouping.id + ":" + bucket.key,
				bucket.label,
				bucket.characteristicIds,
				grouping.id,
				bucket.key});
		}
		m_PlotRequests->QueueGroups(m_CurrentProject, grouping.name, std::move(groups));
	}

	void ProjectTreePanel::OpenGroupingPopup(const JFMProject &project, const JFMGroupingDefinition *grouping)
	{
		m_EditingGroupingId = grouping == nullptr ? std::string{} : grouping->id;
		const std::string name = grouping == nullptr ? "Grouping" : grouping->name;
		const std::string key = grouping == nullptr ? "temperature_K" : grouping->metadataKey;
		std::snprintf(m_GroupingName.data(), m_GroupingName.size(), "%s", name.c_str());
		std::snprintf(m_GroupingKey.data(), m_GroupingKey.size(), "%s", key.c_str());
		m_GroupingMode = grouping == nullptr || grouping->mode == JFMGroupingMode::NumericStep ? 1 : 0;
		m_GroupingStep = grouping == nullptr ? 1.0f : static_cast<float>(grouping->numericStep);
		m_GroupingIncludeMissing = grouping == nullptr || grouping->includeMissing;
		m_GroupingSeriesIds.clear();
		if (grouping == nullptr)
		{
			for (const JFMSeries &series : project.series)
				m_GroupingSeriesIds.emplace_back(series.id);
		}
		else
		{
			m_GroupingSeriesIds = grouping->sourceSeriesIds;
		}
		m_OpenGroupingPopup = true;
	}

	void ProjectTreePanel::RenderGroupingsNode(const JFMProject &project)
	{
		const bool open = ImGui::TreeNodeEx("Groupings##jfm_groupings", ImGuiTreeNodeFlags_DefaultOpen);
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Create Grouping"))
				OpenGroupingPopup(project);
			ImGui::EndPopup();
		}
		if (!open)
			return;
		if (ImGui::SmallButton("+ Grouping"))
			OpenGroupingPopup(project);
		for (const JFMGroupingDefinition &grouping : project.groupings)
		{
			const std::vector<JFMGroupingBucket> buckets = m_Store->ResolveGrouping(m_CurrentProject, grouping.id);
			const std::string label = grouping.name + " (" + std::to_string(buckets.size()) + ")##" + grouping.id;
			ImGui::Selectable(label.c_str(), false);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				QueueGroupingPlot(grouping);
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Plot Grouping"))
					QueueGroupingPlot(grouping);
				if (ImGui::MenuItem("Edit"))
					OpenGroupingPopup(project, &grouping);
				if (ImGui::MenuItem("Delete"))
				{
					const std::string groupingId = grouping.id;
					ExecuteEdit("Delete grouping", [groupingId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
						(void)workspace.DeleteGrouping(handle, groupingId);
						return Result<void>{};
					});
				}
				ImGui::EndPopup();
			}
		}
		if (project.groupings.empty())
			ImGui::TextDisabled("No groupings");
		ImGui::TreePop();
	}

	void ProjectTreePanel::RenderGroupingPopup(const JFMProject &project)
	{
		if (m_OpenGroupingPopup)
		{
			ImGui::OpenPopup("Grouping##jfm_tree");
			m_OpenGroupingPopup = false;
		}
		if (!ImGui::BeginPopupModal("Grouping##jfm_tree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::InputText("Name", m_GroupingName.data(), m_GroupingName.size());
		const std::vector<std::string> keys = m_Store->GetAvailableGroupingKeys(m_CurrentProject);
		if (ImGui::BeginCombo("Metadata", m_GroupingKey.data()))
		{
			for (const std::string &key : keys)
			{
				if (ImGui::Selectable(key.c_str(), key == m_GroupingKey.data()))
					std::snprintf(m_GroupingKey.data(), m_GroupingKey.size(), "%s", key.c_str());
			}
			ImGui::EndCombo();
		}
		ImGui::InputText("Custom key", m_GroupingKey.data(), m_GroupingKey.size());
		ImGui::RadioButton("Exact", &m_GroupingMode, 0);
		ImGui::SameLine();
		ImGui::RadioButton("Numeric step", &m_GroupingMode, 1);
		if (m_GroupingMode == 1)
		{
			ImGui::SetNextItemWidth(180.0f);
			ImGui::DragFloat("Step / tolerance", &m_GroupingStep, 0.1f, 0.0001f, 1000000.0f, "%.4g");
		}
		ImGui::Checkbox("Include missing values", &m_GroupingIncludeMissing);

		ImGui::SeparatorText("Source series");
		ImGui::BeginChild("##grouping_series", ImVec2(420.0f, 150.0f), true);
		for (const JFMSeries &series : project.series)
		{
			bool selected = std::find(m_GroupingSeriesIds.begin(), m_GroupingSeriesIds.end(), series.id) != m_GroupingSeriesIds.end();
			if (ImGui::Checkbox((displayName(series.name, series.metadata) + "##" + series.id).c_str(), &selected))
			{
				m_GroupingSeriesIds.erase(std::remove(m_GroupingSeriesIds.begin(), m_GroupingSeriesIds.end(), series.id), m_GroupingSeriesIds.end());
				if (selected)
					m_GroupingSeriesIds.emplace_back(series.id);
			}
		}
		ImGui::EndChild();

		JFMGroupingDefinition preview;
		preview.id = m_EditingGroupingId;
		preview.name = m_GroupingName.data();
		preview.sourceSeriesIds = m_GroupingSeriesIds;
		preview.metadataKey = m_GroupingKey.data();
		preview.mode = m_GroupingMode == 0 ? JFMGroupingMode::Exact : JFMGroupingMode::NumericStep;
		preview.numericStep = std::max(0.0001, static_cast<double>(m_GroupingStep));
		preview.includeMissing = m_GroupingIncludeMissing;
		const std::vector<JFMGroupingBucket> buckets = m_Store->ResolveGrouping(m_CurrentProject, preview);
		ImGui::SeparatorText("Preview");
		if (ImGui::BeginTable("##grouping_preview", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Bucket");
			ImGui::TableSetupColumn("Count");
			ImGui::TableHeadersRow();
			for (const JFMGroupingBucket &bucket : buckets)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(bucket.label.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%d", static_cast<int>(bucket.characteristicIds.size()));
			}
			ImGui::EndTable();
		}

		const bool valid = preview.name.size() > 0 && preview.metadataKey.size() > 0 && !preview.sourceSeriesIds.empty();
		ImGui::BeginDisabled(!valid);
		if (ImGui::Button(m_EditingGroupingId.empty() ? "Create" : "Save"))
		{
			const bool create = m_EditingGroupingId.empty();
			ExecuteEdit(create ? "Create grouping" : "Reconfigure grouping", [preview, create](JFMWorkspaceStore &workspace, ProjectHandle handle) {
				if (create)
					(void)workspace.AddGrouping(handle, preview);
				else
					(void)workspace.UpdateGrouping(handle, preview);
				return Result<void>{};
			});
			m_EditingGroupingId.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_EditingGroupingId.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ProjectTreePanel::RenderScriptsNode(const JFMProject &project)
	{
		if (!ImGui::TreeNodeEx("Scripts##jfm_scripts", ImGuiTreeNodeFlags_DefaultOpen))
			return;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Add Script"))
			{
				if (auto path = OpenFileDialog(PythonScriptFilter, m_Settings != nullptr ? m_Settings->GetDefaultScriptsDirectory() : Path{}))
				{
					const Path scriptPath = *path;
					ExecuteEdit("Add script", [scriptPath](JFMWorkspaceStore &workspace, ProjectHandle handle) {
						(void)workspace.AddScript(handle, scriptPath);
						return Result<void>{};
					});
				}
			}
			ImGui::EndPopup();
		}
		for (const JFMScriptInfo &script : project.scripts)
		{
			const std::string label = script.name + "##" + script.id;
			ImGui::Selectable(label.c_str(), false);
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Reload"))
				{
					const std::string scriptId = script.id;
					ExecuteEdit("Reload script", [scriptId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
						(void)workspace.ReloadScript(handle, scriptId);
						return Result<void>{};
					});
				}
				if (ImGui::MenuItem("Remove"))
				{
					const std::string scriptId = script.id;
					ExecuteEdit("Remove script", [scriptId](JFMWorkspaceStore &workspace, ProjectHandle handle) {
						(void)workspace.RemoveScript(handle, scriptId);
						return Result<void>{};
					});
				}
				ImGui::EndPopup();
			}
		}
		if (project.scripts.empty())
			ImGui::TextDisabled("No scripts");
		ImGui::TreePop();
	}

	void ProjectTreePanel::RenderRenamePopup()
	{
		if (m_OpenRenamePopup)
		{
			ImGui::OpenPopup("Rename##jfm_tree");
			m_OpenRenamePopup = false;
		}
		if (!ImGui::BeginPopupModal("Rename##jfm_tree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::InputText("Name", m_RenameBuffer.data(), m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		const auto commitRename = [&]() {
			const std::string name = m_RenameBuffer.data();
			const RenameTarget target = m_RenameTarget;
			const std::string targetId = m_RenameTargetId;
			ExecuteEdit("Rename item", [target, targetId, name](JFMWorkspaceStore &workspace, ProjectHandle handle) {
				if (target == RenameTarget::Project)
					(void)workspace.RenameProject(handle, name);
				else if (target == RenameTarget::Group)
					(void)workspace.RenameGroup(handle, targetId, name);
				else if (target == RenameTarget::Series)
					(void)workspace.RenameSeries(handle, targetId, name);
				else if (target == RenameTarget::Characteristic)
					(void)workspace.RenameCharacteristic(handle, static_cast<CharacteristicId>(std::stoull(targetId)), name);
				return Result<void>{};
			});
			m_RenameTarget = RenameTarget::None;
			ImGui::CloseCurrentPopup();
		};
		if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
			commitRename();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_RenameTarget = RenameTarget::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ProjectTreePanel::RenderShortNamePopup()
	{
		if (m_OpenShortNamePopup)
		{
			ImGui::OpenPopup("Short Name##jfm_tree");
			m_OpenShortNamePopup = false;
		}
		if (!ImGui::BeginPopupModal("Short Name##jfm_tree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::InputText("Short name", m_ShortNameBuffer.data(), m_ShortNameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextDisabled("Variables");
		ImGui::TextDisabled("{name}");
		ImGui::TextDisabled("{stem}");
		ImGui::TextDisabled("{folder}");
		ImGui::TextDisabled("{T}");
		ImGui::EndGroup();
		if (m_ShortNameTarget == RenameTarget::Characteristic && m_ShortNameTargetIds.size() > 1)
			ImGui::TextDisabled("Applies to %d selected characteristics.", static_cast<int>(m_ShortNameTargetIds.size()));
		const auto commit = [&]() {
			const std::string shortName = m_ShortNameBuffer.data();
			const RenameTarget target = m_ShortNameTarget;
			const std::string targetId = m_ShortNameTargetId;
			std::vector<CharacteristicId> targetIds = m_ShortNameTargetIds;
			if (target == RenameTarget::Characteristic && targetIds.empty())
				targetIds.emplace_back(static_cast<CharacteristicId>(std::stoull(targetId)));
			ExecuteEdit("Edit short name", [target, targetId, targetIds, shortName](JFMWorkspaceStore &workspace, ProjectHandle handle) {
				if (target == RenameTarget::Group)
					(void)workspace.SetGroupShortName(handle, targetId, shortName);
				else if (target == RenameTarget::Series)
					(void)workspace.SetSeriesShortName(handle, targetId, shortName);
				else if (target == RenameTarget::Characteristic)
				{
					for (CharacteristicId id : targetIds)
					{
						const auto characteristic = workspace.FindCharacteristic({handle, id});
						(void)workspace.SetCharacteristicShortName(
							handle,
							id,
							characteristic ? expandShortNamePattern(shortName, characteristic->info) : shortName);
					}
				}
				return Result<void>{};
			});
			m_ShortNameTarget = RenameTarget::None;
			m_ShortNameTargetIds.clear();
			ImGui::CloseCurrentPopup();
		};
		if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
			commit();
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			m_ShortNameBuffer.fill('\0');
			commit();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_ShortNameTarget = RenameTarget::None;
			m_ShortNameTargetIds.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ProjectTreePanel::HandleKeyboard(const JFMProject &project, TreeRenderState &state)
	{
		const bool treeFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
		if (!treeFocused)
			return;

		const ImGuiIO &io = ImGui::GetIO();
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
			CopyFocused(project, state.selectedIds);
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
			OpenPastePrompt(false);
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
		{
			CopyFocused(project, state.selectedIds);
			PasteClipboard(project, false);
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !state.selectedIds.empty())
		{
			const std::vector<CharacteristicId> ids = state.selectedIds;
			ExecuteEdit("Delete characteristics", [ids](JFMWorkspaceStore &workspace, ProjectHandle handle) {
				for (CharacteristicId id : ids)
					(void)workspace.DeleteCharacteristic(handle, id);
				return Result<void>{};
			});
			state.selectedIds.clear();
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F2))
			StartRename(m_FocusedTarget, m_FocusedTargetId, m_FocusedTargetName);
		if (io.KeyShift && !state.visibleCharacteristicIds.empty()
			&& (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)))
		{
			const auto currentIt = std::find(state.visibleCharacteristicIds.begin(), state.visibleCharacteristicIds.end(), state.primarySelection);
			const std::size_t currentIndex = currentIt == state.visibleCharacteristicIds.end()
				? 0
				: static_cast<std::size_t>(std::distance(state.visibleCharacteristicIds.begin(), currentIt));
			const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
			const std::size_t nextIndex = down
				? std::min(currentIndex + 1, state.visibleCharacteristicIds.size() - 1)
				: (currentIndex == 0 ? 0 : currentIndex - 1);
			std::vector<CharacteristicId> nextSelection = state.selectedIds;
			appendUnique(nextSelection, state.visibleCharacteristicIds[nextIndex]);
			const CharacteristicId primary = state.visibleCharacteristicIds[nextIndex];
			m_Store->SetSelectedCharacteristics(nextSelection, primary, seriesIdForCharacteristic(project, primary));
			state.primarySelection = primary;
			state.selectedIds = m_Store->GetSelectedCharacteristicIds();
		}
	}

	void ProjectTreePanel::ExportGroup(const JFMProject &project, const JFMGroup &group)
	{
		if (m_EventBus == nullptr)
			return;
		auto directory = chooseOutputDirectory(m_Settings);
		if (!directory)
			return;

		std::set<std::string> groupIds;
		collectGroupIds(project, group.id, groupIds);
		std::vector<JFMExportItem> items;
		for (const JFMSeries &series : project.series)
		{
			if (groupIds.find(series.groupId) == groupIds.end())
				continue;
			std::vector<JFMExportItem> seriesItems = makeExportItems(
				m_Store->GetSeriesCharacteristics(series.id),
				groupPath(project, series.groupId) / Path(safeFileName(series.name)));
			items.insert(items.end(), seriesItems.begin(), seriesItems.end());
		}
		m_EventBus->Queue(ExportRawTsvBatchRequested{*directory, std::move(items)});
	}

	void ProjectTreePanel::CopyFocused(const JFMProject &, const std::vector<CharacteristicId> &selectedIds)
	{
		m_ClipboardTarget = m_FocusedTarget;
		m_ClipboardTargetId = m_FocusedTargetId;
		m_ClipboardCharacteristicIds.clear();
		if (m_FocusedTarget == RenameTarget::Characteristic)
		{
			const CharacteristicId id = static_cast<CharacteristicId>(std::stoull(m_FocusedTargetId));
			m_ClipboardCharacteristicIds = containsId(selectedIds, id) ? selectedIds : std::vector<CharacteristicId>{id};
		}
		if (m_ClipboardTarget == RenameTarget::Project || m_ClipboardTarget == RenameTarget::None)
			m_ClipboardTarget = RenameTarget::None;
	}

	void ProjectTreePanel::OpenPastePrompt(bool duplicateFocused)
	{
		if (m_ClipboardTarget == RenameTarget::None)
			return;
		m_PasteDuplicateFocused = duplicateFocused;
		m_OpenPastePopup = true;
	}

	void ProjectTreePanel::RenderPastePopup(const JFMProject &project)
	{
		if (m_OpenPastePopup)
		{
			ImGui::OpenPopup("Paste Options##jfm_tree");
			m_OpenPastePopup = false;
		}
		if (!ImGui::BeginPopupModal("Paste Options##jfm_tree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			return;

		ImGui::TextUnformatted(m_PasteDuplicateFocused ? "Duplicate items" : "Paste items");
		if (ImGui::Button("Current location"))
		{
			PasteClipboard(project, false);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Separate group"))
		{
			PasteClipboard(project, true);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	void ProjectTreePanel::PasteClipboard(const JFMProject &project, bool extractToGroup)
	{
		std::string targetGroupId;
		std::string targetSeriesId = m_Store->GetSelectedSeriesId();
		if (m_FocusedTarget == RenameTarget::Group)
			targetGroupId = m_FocusedTargetId;
		else if (m_FocusedTarget == RenameTarget::Series)
		{
			targetSeriesId = m_FocusedTargetId;
			if (const auto series = findSeries(project, m_FocusedTargetId))
				targetGroupId = series->get().groupId;
		}
		else if (m_FocusedTarget == RenameTarget::Characteristic)
		{
			targetSeriesId = seriesIdForCharacteristic(project, static_cast<CharacteristicId>(std::stoull(m_FocusedTargetId)));
			if (const auto series = findSeries(project, targetSeriesId))
				targetGroupId = series->get().groupId;
		}

		const RenameTarget clipboardTarget = m_ClipboardTarget;
		const std::string clipboardTargetId = m_ClipboardTargetId;
		const std::vector<CharacteristicId> clipboardCharacteristicIds = m_ClipboardCharacteristicIds;
		ExecuteEdit(
			clipboardTarget == RenameTarget::Characteristic ? "Duplicate characteristics" : "Duplicate project items",
			[project,
			 clipboardTarget,
			 clipboardTargetId,
			 clipboardCharacteristicIds,
			 targetGroupId,
			 targetSeriesId,
			 extractToGroup](JFMWorkspaceStore &workspace, ProjectHandle handle) mutable {
				const auto cloneCharacteristics = [&](const std::vector<CharacteristicId> &ids, const std::string &seriesId) {
					std::vector<JFMCharacteristic> clones;
					clones.reserve(ids.size());
					for (CharacteristicId id : ids)
					{
						auto characteristic = workspace.FindCharacteristic({handle, id});
						if (!characteristic)
							continue;
						characteristic->info.id = InvalidCharacteristicId;
						characteristic->info.name = copyName(characteristic->info.name);
						clones.emplace_back(std::move(*characteristic));
					}
					if (!clones.empty())
						(void)workspace.AddCharacteristics(handle, std::move(clones), seriesId);
				};

				std::function<void(const JFMSeries &, const std::string &)> cloneSeries;
				cloneSeries = [&](const JFMSeries &source, const std::string &groupId) {
					const JFMSeries series = workspace.AddSeries(handle, copyName(source.name), groupId);
					if (const std::string shortName = shortNameFrom(source.metadata); !shortName.empty())
						(void)workspace.SetSeriesShortName(handle, series.id, copyName(shortName));
					cloneCharacteristics(source.characteristicIds, series.id);
				};

				if (extractToGroup)
					targetGroupId = workspace.AddGroup(handle, "Pasted", targetGroupId).id;

				if (clipboardTarget == RenameTarget::Characteristic)
				{
					if (extractToGroup || targetSeriesId.empty())
						targetSeriesId = workspace.AddSeries(handle, "Pasted Characteristics", targetGroupId).id;
					cloneCharacteristics(clipboardCharacteristicIds, targetSeriesId);
					return Result<void>{};
				}

				if (clipboardTarget == RenameTarget::Series)
				{
					if (const auto source = findSeries(project, clipboardTargetId))
						cloneSeries(source->get(), extractToGroup ? targetGroupId : (targetGroupId.empty() ? source->get().groupId : targetGroupId));
					return Result<void>{};
				}

				if (clipboardTarget != RenameTarget::Group)
					return Result<void>{};
				const auto sourceGroup = findGroup(project, clipboardTargetId);
				if (!sourceGroup)
					return Result<void>{};

				const auto copyGroupRecursive = [&](const auto &self, const std::string &sourceGroupId, const std::string &parentId) -> void {
					const auto group = findGroup(project, sourceGroupId);
					if (!group)
						return;
					const JFMGroup created = workspace.AddGroup(handle, copyName(group->get().name), parentId);
					if (const std::string shortName = shortNameFrom(group->get().metadata); !shortName.empty())
						(void)workspace.SetGroupShortName(handle, created.id, copyName(shortName));
					for (const JFMSeries &series : project.series)
					{
						if (series.groupId == sourceGroupId)
							cloneSeries(series, created.id);
					}
					for (const JFMGroup &child : project.groups)
					{
						if (child.parentId == sourceGroupId)
							self(self, child.id, created.id);
					}
				};
				copyGroupRecursive(copyGroupRecursive, sourceGroup->get().id, extractToGroup ? targetGroupId : sourceGroup->get().parentId);
				return Result<void>{};
			});
	}
} // namespace SST::Prototypes::JFM
