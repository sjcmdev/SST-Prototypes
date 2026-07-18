#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include "App/NativeFileDialog.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Domain/JFMPrefitModels.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>

#include <imgui.h>

namespace SST::Prototypes::JFM
{
	namespace ProjectTreeDetail
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

		std::vector<CharacteristicId> sortedCharacteristicIds(
			const JFMSeries &series,
			const Ref<JFMWorkspaceStore> &store,
			ProjectHandle project,
			int sortMode)
		{
			std::vector<CharacteristicId> ids = series.characteristicIds;
			std::sort(ids.begin(), ids.end(), [&](CharacteristicId left, CharacteristicId right) {
				const auto leftCharacteristic = store->FindCharacteristicRecord({project, left});
				const auto rightCharacteristic = store->FindCharacteristicRecord({project, right});
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
			return store == nullptr ? std::vector<CharacteristicId>{} : store->GetGroupCharacteristicIds(project, groupId);
		}
	} // namespace ProjectTreeDetail
	using namespace ProjectTreeDetail;

	ProjectTreePanel::ProjectTreePanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<JFMImportDialogState> importDialog,
		Ref<EventBus> eventBus,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<ContextManager> contextManager,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_ImportDialog(std::move(importDialog)),
		  m_EventBus(std::move(eventBus)),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_ContextManager(std::move(contextManager))
	{
		if (m_EventBus != nullptr)
		{
			AddSubscription(m_EventBus->Subscribe<ProjectTreeRenameRequested>([this](const ProjectTreeRenameRequested &event) {
				StartRename(event.item);
			}));
			AddSubscription(m_EventBus->Subscribe<ProjectTreePasteRequested>([this](const ProjectTreePasteRequested &event) {
				m_CurrentProject = event.target.project;
				m_FocusedTargetId = event.target.id;
				switch (event.target.kind)
				{
				case JFMTreeItemKind::Project: m_FocusedTarget = RenameTarget::Project; break;
				case JFMTreeItemKind::Group: m_FocusedTarget = RenameTarget::Group; break;
				case JFMTreeItemKind::Series: m_FocusedTarget = RenameTarget::Series; break;
				case JFMTreeItemKind::Characteristic: m_FocusedTarget = RenameTarget::Characteristic; break;
				case JFMTreeItemKind::None: m_FocusedTarget = RenameTarget::None; break;
				}
				OpenPastePrompt(event.duplicate);
			}));
		}
	}

	Ref<IPanel> ProjectTreePanel::Clone() const
	{
		return CreateRef<ProjectTreePanel>(m_Store, m_Settings, m_ImportDialog, m_EventBus, m_CommandRegistry, m_ContextManager, GetTitle(), IsVisible());
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

	void ProjectTreePanel::Render()
	{
		if (!IsVisible())
		{
			if (auto contextManager = m_ContextManager.lock())
				contextManager->SetActive("jfm.project_tree.focused", false);
			return;
		}

		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			if (auto contextManager = m_ContextManager.lock())
				contextManager->SetActive("jfm.project_tree.focused", false);
			ImGui::End();
			SetVisible(visible);
			return;
		}
		if (auto contextManager = m_ContextManager.lock())
			contextManager->SetActive(
				"jfm.project_tree.focused",
				ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput);

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

	void ProjectTreePanel::FocusItem(RenameTarget target, std::string id)
	{
		m_FocusedTarget = target;
		m_FocusedTargetId = id;
		if (m_Store == nullptr)
			return;
		JFMTreeItemRef item = MakeTreeItem(target, std::move(id));
		if (item.kind == JFMTreeItemKind::Characteristic)
			item.characteristicIds = m_Store->GetSelectedCharacteristicIds();
		m_Store->SetFocusedTreeItem(std::move(item));
		if (target == RenameTarget::Group)
			m_Store->SelectGroup(m_FocusedTargetId);
		else if (target == RenameTarget::Project)
			m_Store->SelectGroup({});
		else
		{
			const JFMProject project = m_Store->GetProjectSnapshot(m_CurrentProject);
			if (target == RenameTarget::Series)
			{
				const auto series = findSeries(project, m_FocusedTargetId);
				m_Store->SelectGroup(series ? series->get().groupId : std::string{});
			}
			else if (target == RenameTarget::Characteristic && !m_FocusedTargetId.empty())
			{
				const CharacteristicId characteristicId = static_cast<CharacteristicId>(std::stoull(m_FocusedTargetId));
				const std::string seriesId = seriesIdForCharacteristic(project, characteristicId);
				const auto series = findSeries(project, seriesId);
				m_Store->SelectGroup(series ? series->get().groupId : std::string{});
			}
		}
	}

	JFMTreeItemRef ProjectTreePanel::MakeTreeItem(RenameTarget target, std::string id, std::vector<CharacteristicId> characteristicIds) const
	{
		JFMTreeItemKind kind = JFMTreeItemKind::None;
		switch (target)
		{
		case RenameTarget::Project: kind = JFMTreeItemKind::Project; break;
		case RenameTarget::Group: kind = JFMTreeItemKind::Group; break;
		case RenameTarget::Series: kind = JFMTreeItemKind::Series; break;
		case RenameTarget::Characteristic: kind = JFMTreeItemKind::Characteristic; break;
		case RenameTarget::None: break;
		}
		return {m_CurrentProject, kind, std::move(id), std::move(characteristicIds)};
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

	void ProjectTreePanel::StartRename(const JFMTreeItemRef &item)
	{
		if (m_Store == nullptr || item.project == InvalidProjectHandle)
			return;

		m_CurrentProject = item.project;
		const JFMProject project = m_Store->GetProjectSnapshot(item.project);
		switch (item.kind)
		{
		case JFMTreeItemKind::Project:
			StartRename(RenameTarget::Project, {}, project.name);
			break;
		case JFMTreeItemKind::Group:
			if (const auto group = findGroup(project, item.id))
				StartRename(RenameTarget::Group, item.id, group->get().name);
			break;
		case JFMTreeItemKind::Series:
			if (const auto series = findSeries(project, item.id))
				StartRename(RenameTarget::Series, item.id, series->get().name);
			break;
		case JFMTreeItemKind::Characteristic:
			if (!item.id.empty())
			{
				const auto record = m_Store->FindCharacteristicRecord(
					{item.project, static_cast<CharacteristicId>(std::stoull(item.id))});
				if (record)
					StartRename(RenameTarget::Characteristic, item.id, record->info.name);
			}
			break;
		case JFMTreeItemKind::None:
			break;
		}
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
			const auto characteristic = m_Store->FindCharacteristicRecord({m_CurrentProject, characteristicId});
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
		if (m_EventBus != nullptr && !ids.empty())
			m_EventBus->Queue(PlotRequested{m_CurrentProject, title, {{"selection", std::move(title), std::move(ids), {}, {}}}});
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
			FocusItem(RenameTarget::Project, {});
		if (ImGui::BeginPopupContextItem())
		{
			FocusItem(RenameTarget::Project, {});
			if (ImGui::MenuItem("Add Group"))
				ExecuteCommand(JFMAddGroupCommandId);
			if (ImGui::MenuItem("Add Series"))
				ExecuteCommand(JFMAddSeriesCommandId);
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
			if (ImGui::MenuItem("Paste", nullptr, false, m_Store->GetTreeClipboard().has_value()))
				ExecuteCommand(JFMRequestPasteTreeItemCommandId);
			if (ImGui::MenuItem("Rename"))
				ExecuteCommand(JFMRequestRenameTreeItemCommandId);
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
			FocusItem(RenameTarget::Group, group.id);
		if (shownName != group.name && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", group.name.c_str());
		if (ImGui::BeginPopupContextItem())
		{
			FocusItem(RenameTarget::Group, group.id);
			if (ImGui::MenuItem("Add Group"))
			{
				CommandContext context;
				context.Set("parent_group_id", group.id);
				ExecuteCommand(JFMAddGroupCommandId, std::move(context));
			}
			if (ImGui::MenuItem("Add Series"))
			{
				CommandContext context;
				context.Set("group_id", group.id);
				ExecuteCommand(JFMAddSeriesCommandId, std::move(context));
			}
			if (ImGui::MenuItem("Import Files"))
				OpenImportDialog({}, group.id);
			if (ImGui::MenuItem("Import Folder as Series"))
				OpenImportFolderDialog({}, group.id, false);
			if (ImGui::MenuItem("Import Folder Tree"))
				OpenImportFolderDialog({}, group.id, true);
			if (ImGui::MenuItem("Plot Group"))
				QueuePlot(idsForGroup(m_Store, m_CurrentProject, group.id), shownName);
			if (ImGui::BeginMenu("Prefit model"))
			{
				const std::vector<CharacteristicId> ids = idsForGroup(m_Store, m_CurrentProject, group.id);
				for (const char *model : PrefitModels)
				{
					if (ImGui::MenuItem(model, nullptr, false, !ids.empty()))
					{
						CommandContext context;
						context.Set("characteristic_ids", ids);
						context.Set("model_id", std::string(model));
						ExecuteCommand(JFMSetPrefitModelCommandId, std::move(context));
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Plot Child Groups"))
				QueuePlotsForChildGroups(project, group.id);
			if (ImGui::MenuItem("Export Raw TSV"))
				ExportGroup(project, group);
			if (ImGui::MenuItem("Rename"))
				ExecuteCommand(JFMRequestRenameTreeItemCommandId);
			if (ImGui::MenuItem("Edit Short Name"))
				StartShortNameEdit(project, RenameTarget::Group, group.id, shownName);
			if (ImGui::MenuItem("Copy"))
				CopyFocused(project, state.selectedIds);
			if (ImGui::MenuItem("Paste", nullptr, false, m_Store->GetTreeClipboard().has_value()))
				ExecuteCommand(JFMRequestPasteTreeItemCommandId);
			if (ImGui::MenuItem("Duplicate"))
				ExecuteCommand(JFMRequestDuplicateTreeItemCommandId);
			if (ImGui::MenuItem("Delete Group"))
			{
				CommandContext context;
				context.Set("item", JFMTreeItemRef{m_CurrentProject, JFMTreeItemKind::Group, group.id, {}});
				ExecuteCommand(JFMDeleteTreeItemCommandId, std::move(context));
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
			FocusItem(RenameTarget::Series, series.id);
		if (shownName != series.name && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", series.name.c_str());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("JFM_CHARACTERISTIC_ID"))
			{
				const CharacteristicId id = *static_cast<const CharacteristicId *>(payload->Data);
				std::vector<CharacteristicId> idsToMove = containsId(state.selectedIds, id) ? state.selectedIds : std::vector<CharacteristicId>{id};
				CommandContext context;
				context.Set("characteristic_ids", idsToMove);
				context.Set("target_series_id", series.id);
				ExecuteCommand(JFMMoveCharacteristicsCommandId, std::move(context));
				m_Store->SetSelectedCharacteristics(idsToMove, id, series.id);
				state.selectedIds = m_Store->GetSelectedCharacteristicIds();
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem())
		{
			FocusItem(RenameTarget::Series, series.id);
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
			if (ImGui::BeginMenu("Prefit model"))
			{
				for (const char *model : PrefitModels)
				{
					if (ImGui::MenuItem(model, nullptr, false, !series.characteristicIds.empty()))
					{
						CommandContext context;
						context.Set("characteristic_ids", series.characteristicIds);
						context.Set("model_id", std::string(model));
						ExecuteCommand(JFMSetPrefitModelCommandId, std::move(context));
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem("Export Raw TSV"))
			{
				CommandContext context;
				context.Set("series_id", series.id);
				ExecuteCommand("jfm.export.series_raw_tsv", std::move(context));
			}
			if (ImGui::MenuItem("Rename"))
				ExecuteCommand(JFMRequestRenameTreeItemCommandId);
			if (ImGui::MenuItem("Edit Short Name"))
				StartShortNameEdit(project, RenameTarget::Series, series.id, shownName);
			if (ImGui::MenuItem("Copy"))
				CopyFocused(project, state.selectedIds);
			if (ImGui::MenuItem("Paste", nullptr, false, m_Store->GetTreeClipboard().has_value()))
				ExecuteCommand(JFMRequestPasteTreeItemCommandId);
			if (ImGui::MenuItem("Duplicate"))
				ExecuteCommand(JFMRequestDuplicateTreeItemCommandId);
			if (ImGui::MenuItem("Delete Series"))
			{
				CommandContext context;
				context.Set("item", JFMTreeItemRef{m_CurrentProject, JFMTreeItemKind::Series, series.id, {}});
				ExecuteCommand(JFMDeleteTreeItemCommandId, std::move(context));
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
		const auto characteristic = m_Store->FindCharacteristicRecord({m_CurrentProject, characteristicId});
		const std::string name = characteristic ? characteristic->info.name : std::to_string(characteristicId);
		const std::string shownName = characteristic ? displayName(characteristic->info.name, characteristic->info.metadata) : name;
		const bool hasWarning = characteristic
			&& (!characteristic->info.qc.warnings.empty()
				|| (!characteristic->info.sourcePath.Empty() && (!characteristic->data || characteristic->data->voltage.empty())));
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
			FocusItem(RenameTarget::Characteristic, std::to_string(characteristicId));
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
			FocusItem(RenameTarget::Characteristic, std::to_string(characteristicId));
			if (ImGui::MenuItem("Delete"))
			{
				const std::vector<CharacteristicId> ids = selected && state.selectedIds.size() > 1
					? state.selectedIds
					: std::vector<CharacteristicId>{characteristicId};
				CommandContext context;
				context.Set("item", JFMTreeItemRef{m_CurrentProject, JFMTreeItemKind::Characteristic, std::to_string(characteristicId), ids});
				ExecuteCommand(JFMDeleteTreeItemCommandId, std::move(context));
			}
			if (ImGui::MenuItem("Rename"))
				ExecuteCommand(JFMRequestRenameTreeItemCommandId);
			if (ImGui::MenuItem("Edit Short Name"))
				StartShortNameEdit(m_Store->GetProjectSnapshot(m_CurrentProject), RenameTarget::Characteristic, std::to_string(characteristicId), shownName);
			if (ImGui::MenuItem("Copy"))
				CopyFocused(m_Store->GetProjectSnapshot(m_CurrentProject), state.selectedIds);
			if (ImGui::MenuItem("Duplicate"))
				ExecuteCommand(JFMRequestDuplicateTreeItemCommandId);
			ImGui::EndPopup();
		}
	}

	void ProjectTreePanel::QueueGroupingPlot(const JFMGroupingDefinition &grouping)
	{
		if (m_EventBus == nullptr || m_Store == nullptr)
			return;
		std::vector<JFMPlotCurveGroupRequest> groups;
		for (const JFMGroupingBucket &bucket : m_Store->ResolveGrouping(m_CurrentProject, grouping.id))
		{
			groups.emplace_back(JFMPlotCurveGroupRequest{
				grouping.id + ":" + bucket.key,
				bucket.label,
				bucket.characteristicIds,
				grouping.id,
				bucket.key});
		}
		m_EventBus->Queue(PlotRequested{m_CurrentProject, grouping.name, std::move(groups)});
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
					CommandContext context;
					context.Set("grouping_id", grouping.id);
					ExecuteCommand(JFMDeleteGroupingCommandId, std::move(context));
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
			CommandContext context;
			context.Set("grouping", preview);
			context.Set("create", create);
			ExecuteCommand(JFMUpsertGroupingCommandId, std::move(context));
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
					CommandContext context;
					context.Set("path", *path);
					ExecuteCommand(JFMAddScriptCommandId, std::move(context));
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
					CommandContext context;
					context.Set("script_id", script.id);
					ExecuteCommand(JFMReloadScriptCommandId, std::move(context));
				}
				if (ImGui::MenuItem("Remove"))
				{
					CommandContext context;
					context.Set("script_id", script.id);
					ExecuteCommand(JFMRemoveScriptCommandId, std::move(context));
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
		if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
			CommitRename();
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			m_RenameTarget = RenameTarget::None;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void ProjectTreePanel::CommitRename()
	{
		CommandContext context;
		context.Set("item", MakeTreeItem(m_RenameTarget, m_RenameTargetId));
		context.Set("name", std::string(m_RenameBuffer.data()));
		ExecuteCommand(JFMRenameTreeItemCommandId, std::move(context));
		m_RenameTarget = RenameTarget::None;
		ImGui::CloseCurrentPopup();
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
		if (ImGui::Button("OK") || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
			CommitShortName();
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
		{
			m_ShortNameBuffer.fill('\0');
			CommitShortName();
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

	void ProjectTreePanel::CommitShortName()
	{
		std::vector<CharacteristicId> targetIds = m_ShortNameTargetIds;
		if (m_ShortNameTarget == RenameTarget::Characteristic && targetIds.empty())
			targetIds.emplace_back(static_cast<CharacteristicId>(std::stoull(m_ShortNameTargetId)));
		CommandContext context;
		context.Set("item", MakeTreeItem(m_ShortNameTarget, m_ShortNameTargetId, std::move(targetIds)));
		context.Set("short_name", std::string(m_ShortNameBuffer.data()));
		ExecuteCommand(JFMSetShortNameCommandId, std::move(context));
		m_ShortNameTarget = RenameTarget::None;
		m_ShortNameTargetIds.clear();
		ImGui::CloseCurrentPopup();
	}

	void ProjectTreePanel::HandleKeyboard(const JFMProject &project, TreeRenderState &state)
	{
		const bool treeFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
		if (!treeFocused)
			return;

		const ImGuiIO &io = ImGui::GetIO();
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
		(void)project;
		CommandContext context;
		context.Set("group_id", group.id);
		ExecuteCommand("jfm.export.group_raw_tsv", std::move(context));
	}

	void ProjectTreePanel::CopyFocused(const JFMProject &, const std::vector<CharacteristicId> &selectedIds)
	{
		if (m_FocusedTarget == RenameTarget::Project || m_FocusedTarget == RenameTarget::None)
			return;
		std::vector<CharacteristicId> ids;
		if (m_FocusedTarget == RenameTarget::Characteristic)
		{
			const CharacteristicId id = static_cast<CharacteristicId>(std::stoull(m_FocusedTargetId));
			ids = containsId(selectedIds, id) ? selectedIds : std::vector<CharacteristicId>{id};
		}
		CommandContext context;
		context.Set("item", MakeTreeItem(m_FocusedTarget, m_FocusedTargetId, std::move(ids)));
		ExecuteCommand(JFMCopyTreeItemCommandId, std::move(context));
	}

	void ProjectTreePanel::OpenPastePrompt(bool duplicateFocused)
	{
		if (!duplicateFocused && (m_Store == nullptr || !m_Store->GetTreeClipboard()))
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
		(void)project;
		CommandContext context;
		context.Set("target", MakeTreeItem(m_FocusedTarget, m_FocusedTargetId));
		context.Set("extract_to_group", extractToGroup);
		if (m_PasteDuplicateFocused)
			context.Set("item", MakeTreeItem(m_FocusedTarget, m_FocusedTargetId, m_Store->GetSelectedCharacteristicIds()));
		ExecuteCommand(m_PasteDuplicateFocused ? JFMDuplicateTreeItemCommandId : JFMPasteTreeItemCommandId, std::move(context));
	}
} // namespace SST::Prototypes::JFM
