#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include "App/NativeFileDialog.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <set>

#include <imgui.h>

namespace SST::Prototypes::JFM
{
	namespace
	{
		constexpr NativeFileDialogFilter JFMDataFilter{"JFM Data", "dat,iv,txt,csv"};
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

		std::vector<Path> chooseImportFiles(const Ref<JFMUserSettingsStore> &settings)
		{
			return OpenFilesDialog(JFMDataFilter, settings != nullptr ? settings->GetDefaultImportDirectory() : Path{});
		}

		std::vector<Path> chooseImportFolders(const Ref<JFMUserSettingsStore> &settings, bool multiple)
		{
			return PickFoldersDialog(multiple, settings != nullptr ? settings->GetDefaultImportDirectory() : Path{});
		}

		std::optional<Path> chooseOutputDirectory(const Ref<JFMUserSettingsStore> &settings)
		{
			std::vector<Path> paths = PickFoldersDialog(false, settings != nullptr ? settings->GetDefaultProjectsDirectory() : Path{});
			if (paths.empty())
				return std::nullopt;
			return paths.front();
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
			int sortMode)
		{
			std::vector<CharacteristicId> ids = series.characteristicIds;
			std::sort(ids.begin(), ids.end(), [&](CharacteristicId left, CharacteristicId right) {
				const auto leftCharacteristic = store->FindCharacteristic(left);
				const auto rightCharacteristic = store->FindCharacteristic(right);
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
	}

	ProjectTreePanel::ProjectTreePanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<JFMImportDialogState> importDialog,
		Ref<JFMPlotRequestState> plotRequests,
		Ref<EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_ImportDialog(std::move(importDialog)),
		  m_PlotRequests(std::move(plotRequests)),
		  m_EventBus(std::move(eventBus))
	{
	}

	Ref<IPanel> ProjectTreePanel::Clone() const
	{
		return CreateRef<ProjectTreePanel>(*this);
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

		const JFMProject project = m_Store->GetProjectSnapshot();
		const CharacteristicId primarySelection = m_Store->GetSelectedCharacteristicId();
		std::vector<CharacteristicId> selectedIds = m_Store->GetSelectedCharacteristicIds();
		std::vector<CharacteristicId> visibleCharacteristicIds;

		const auto focusItem = [&](RenameTarget target, std::string id, std::string name) {
			m_FocusedTarget = target;
			m_FocusedTargetId = std::move(id);
			m_FocusedTargetName = std::move(name);
		};

		const auto startRename = [&](RenameTarget target, std::string id, std::string name) {
			if (target == RenameTarget::None || name.empty())
				return;
			m_RenameTarget = target;
			m_RenameTargetId = std::move(id);
			std::snprintf(m_RenameBuffer.data(), m_RenameBuffer.size(), "%s", name.c_str());
			m_OpenRenamePopup = true;
		};

		const auto plotCharacteristics = [&](std::vector<CharacteristicId> ids) {
			if (m_PlotRequests != nullptr)
				m_PlotRequests->QueuePlot(std::move(ids));
		};

		const auto openImportDialog = [&](std::string targetSeriesId = {}) {
			std::vector<Path> paths = chooseImportFiles(m_Settings);
			if (!paths.empty() && m_Settings != nullptr)
				m_Settings->SetDefaultImportDirectory(paths.front().parent_path());
			if (!paths.empty() && m_ImportDialog != nullptr)
				m_ImportDialog->Open(JFMImportDialogRequest{std::move(paths), std::move(targetSeriesId), false});
		};

		const auto openImportFolderDialog = [&](std::string targetSeriesId, bool recursive) {
			std::vector<Path> paths = chooseImportFolders(m_Settings, false);
			if (!paths.empty() && m_Settings != nullptr)
				m_Settings->SetDefaultImportDirectory(paths.front());
			if (!paths.empty() && m_ImportDialog != nullptr)
				m_ImportDialog->Open(JFMImportDialogRequest{std::move(paths), std::move(targetSeriesId), recursive});
		};

		const auto renderCharacteristicNode = [&](const JFMSeries &series, CharacteristicId characteristicId, const std::vector<CharacteristicId> &visibleIds) {
			const auto characteristic = m_Store != nullptr ? m_Store->FindCharacteristic(characteristicId) : std::optional<JFMCharacteristic>{};
			const std::string name = characteristic ? characteristic->info.name : std::to_string(characteristicId);
			const bool hasWarning = characteristic
				&& (!characteristic->info.qc.warnings.empty()
					|| (!characteristic->info.sourcePath.Empty() && characteristic->data.voltage.empty()));
			const bool selected = containsId(selectedIds, characteristicId);
			const std::string label = std::string(selected ? "[x] " : "[ ] ") + name + (hasWarning ? " [!]" : "") + "##" + std::to_string(characteristicId);

			if (selected)
			{
				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.42f, 0.30f, 0.18f, 0.92f));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.55f, 0.38f, 0.22f, 0.96f));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.70f, 0.46f, 0.24f, 1.00f));
			}
			if (ImGui::Selectable(label.c_str(), selected) && m_Store != nullptr)
			{
				std::vector<CharacteristicId> nextSelection = selectedIds;
				const ImGuiIO &io = ImGui::GetIO();
				if (io.KeyShift && primarySelection != InvalidCharacteristicId)
				{
					nextSelection.clear();
					bool collecting = false;
					for (CharacteristicId id : visibleIds)
					{
						if (id == primarySelection || id == characteristicId)
						{
							nextSelection.push_back(id);
							collecting = !collecting;
							if (primarySelection == characteristicId)
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
				selectedIds = m_Store->GetSelectedCharacteristicIds();
			}
			if (selected)
				ImGui::PopStyleColor(3);
			if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
				focusItem(RenameTarget::Characteristic, std::to_string(characteristicId), name);
			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("JFM_CHARACTERISTIC_ID", &characteristicId, sizeof(CharacteristicId));
				if (selected && selectedIds.size() > 1)
					ImGui::Text("%d characteristics", static_cast<int>(selectedIds.size()));
				else
					ImGui::TextUnformatted(name.c_str());
				ImGui::EndDragDropSource();
			}
			if (hasWarning && ImGui::IsItemHovered())
				ImGui::SetTooltip("Raw arrays unavailable; see Import Status.");
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Delete"))
				{
					if (selected && selectedIds.size() > 1)
					{
						for (CharacteristicId id : selectedIds)
							(void)m_Store->DeleteCharacteristic(id);
					}
					else
					{
						(void)m_Store->DeleteCharacteristic(characteristicId);
					}
				}
				if (ImGui::MenuItem("Rename"))
					startRename(RenameTarget::Characteristic, std::to_string(characteristicId), name);
				ImGui::EndPopup();
			}
		};

		const auto renderSeriesNode = [&](const JFMSeries &series, const auto &renderSeriesNodeRef) -> void {
			(void)renderSeriesNodeRef;
			const std::string seriesLabel = series.name + "##" + series.id;
			const bool open = ImGui::TreeNodeEx(seriesLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
				focusItem(RenameTarget::Series, series.id, series.name);
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("JFM_CHARACTERISTIC_ID"))
				{
					const CharacteristicId id = *static_cast<const CharacteristicId *>(payload->Data);
					std::vector<CharacteristicId> idsToMove = containsId(selectedIds, id) ? selectedIds : std::vector<CharacteristicId>{id};
					for (CharacteristicId movedId : idsToMove)
						(void)m_Store->MoveCharacteristic(movedId, series.id);
					m_Store->SetSelectedCharacteristics(idsToMove, id, series.id);
					selectedIds = m_Store->GetSelectedCharacteristicIds();
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Select Series"))
				{
					m_Store->SelectSeries(series.id);
					m_Store->SetSelectedCharacteristics(series.characteristicIds, series.characteristicIds.empty() ? InvalidCharacteristicId : series.characteristicIds.front(), series.id);
				}
				if (ImGui::MenuItem("Add Characteristics"))
					openImportDialog(series.id);
				if (ImGui::MenuItem("Import Folder"))
					openImportFolderDialog(series.id, false);
				if (ImGui::MenuItem("Import Folder Recursively"))
					openImportFolderDialog(series.id, true);
				if (ImGui::MenuItem("Plot Series"))
					plotCharacteristics(series.characteristicIds);
				if (ImGui::MenuItem("Export Raw TSV"))
				{
					if (auto directory = chooseOutputDirectory(m_Settings); directory && m_EventBus != nullptr)
						m_EventBus->Queue(ExportRawTsvBatchRequested{*directory, makeExportItems(m_Store->GetSeriesCharacteristics(series.id), Path(safeFileName(series.name)))});
				}
				if (ImGui::MenuItem("Rename"))
					startRename(RenameTarget::Series, series.id, series.name);
				if (ImGui::MenuItem("Delete Series"))
					(void)m_Store->DeleteSeries(series.id);
				ImGui::EndPopup();
			}
			if (!open)
				return;

			const std::vector<CharacteristicId> visibleIds = sortedCharacteristicIds(series, m_Store, m_SortMode);
			visibleCharacteristicIds.insert(visibleCharacteristicIds.end(), visibleIds.begin(), visibleIds.end());
			for (CharacteristicId characteristicId : visibleIds)
				renderCharacteristicNode(series, characteristicId, visibleIds);
			if (series.characteristicIds.empty())
				ImGui::TextDisabled("No characteristics");
			ImGui::TreePop();
		};

		const auto exportGroup = [&](const JFMGroup &group) {
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
		};

		const auto renderGroupNode = [&](const JFMGroup &group, const auto &renderGroupNodeRef, const auto &renderSeriesNodeRef) -> void {
			const std::string groupLabel = group.name + "##" + group.id;
			const bool open = ImGui::TreeNodeEx(groupLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
				focusItem(RenameTarget::Group, group.id, group.name);
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Add Series"))
					(void)m_Store->AddSeries("Series", group.id);
				if (ImGui::MenuItem("Plot Group"))
				{
					std::vector<CharacteristicId> ids;
					for (const JFMCharacteristic &characteristic : m_Store->GetGroupCharacteristics(group.id))
						ids.push_back(characteristic.info.id);
					plotCharacteristics(std::move(ids));
				}
				if (ImGui::MenuItem("Export Raw TSV"))
					exportGroup(group);
				if (ImGui::MenuItem("Rename"))
					startRename(RenameTarget::Group, group.id, group.name);
				if (ImGui::MenuItem("Delete Group"))
					(void)m_Store->DeleteGroup(group.id);
				ImGui::EndPopup();
			}
			if (!open)
				return;

			for (const JFMGroup &child : project.groups)
			{
				if (child.parentId == group.id)
					renderGroupNodeRef(child, renderGroupNodeRef, renderSeriesNodeRef);
			}
			for (const JFMSeries &series : project.series)
			{
				if (series.groupId == group.id)
					renderSeriesNodeRef(series, renderSeriesNodeRef);
			}
			ImGui::TreePop();
		};

		if (ImGui::TreeNodeEx((project.name + "##project").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::IsItemClicked() || ImGui::IsItemFocused())
				focusItem(RenameTarget::Project, {}, project.name);
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Add Series"))
					(void)m_Store->AddSeries("Series");
				if (ImGui::MenuItem("Import Series"))
					openImportDialog();
				if (ImGui::MenuItem("Import Folder as Series"))
					openImportFolderDialog({}, false);
				if (ImGui::MenuItem("Import Folder Recursively as Series"))
					openImportFolderDialog({}, true);
				if (ImGui::MenuItem("Rename"))
					startRename(RenameTarget::Project, {}, project.name);
				ImGui::EndPopup();
			}
			for (const JFMGroup &group : project.groups)
			{
				if (group.parentId.empty())
					renderGroupNode(group, renderGroupNode, renderSeriesNode);
			}
			for (const JFMSeries &series : project.series)
			{
				if (series.groupId.empty())
					renderSeriesNode(series, renderSeriesNode);
			}
			if (ImGui::TreeNodeEx("Scripts##jfm_scripts", ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (ImGui::BeginPopupContextItem())
				{
					if (ImGui::MenuItem("Add Script"))
					{
						if (auto path = OpenFileDialog(PythonScriptFilter, m_Settings != nullptr ? m_Settings->GetDefaultScriptsDirectory() : Path{}))
							(void)m_Store->AddScript(*path);
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
							(void)m_Store->ReloadScript(script.id);
						if (ImGui::MenuItem("Remove"))
							(void)m_Store->RemoveScript(script.id);
						ImGui::EndPopup();
					}
				}
				if (project.scripts.empty())
					ImGui::TextDisabled("No scripts");
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}

		const bool treeFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput;
		if (treeFocused && ImGui::IsKeyPressed(ImGuiKey_Delete) && !selectedIds.empty())
		{
			for (CharacteristicId id : selectedIds)
				(void)m_Store->DeleteCharacteristic(id);
			selectedIds.clear();
		}
		if (treeFocused && ImGui::IsKeyPressed(ImGuiKey_F2))
			startRename(m_FocusedTarget, m_FocusedTargetId, m_FocusedTargetName);
		if (treeFocused && ImGui::GetIO().KeyShift && !visibleCharacteristicIds.empty()
			&& (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)))
		{
			const auto currentIt = std::find(visibleCharacteristicIds.begin(), visibleCharacteristicIds.end(), primarySelection);
			const std::size_t currentIndex = currentIt == visibleCharacteristicIds.end()
				? 0
				: static_cast<std::size_t>(std::distance(visibleCharacteristicIds.begin(), currentIt));
			const bool down = ImGui::IsKeyPressed(ImGuiKey_DownArrow);
			const std::size_t nextIndex = down
				? std::min(currentIndex + 1, visibleCharacteristicIds.size() - 1)
				: (currentIndex == 0 ? 0 : currentIndex - 1);
			std::vector<CharacteristicId> nextSelection = selectedIds;
			appendUnique(nextSelection, visibleCharacteristicIds[nextIndex]);
			const CharacteristicId primary = visibleCharacteristicIds[nextIndex];
			m_Store->SetSelectedCharacteristics(nextSelection, primary, seriesIdForCharacteristic(project, primary));
		}

		if (m_OpenRenamePopup)
		{
			ImGui::OpenPopup("Rename##jfm_tree");
			m_OpenRenamePopup = false;
		}
		if (ImGui::BeginPopupModal("Rename##jfm_tree", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", m_RenameBuffer.data(), m_RenameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
			const auto commitRename = [&]() {
				const std::string name = m_RenameBuffer.data();
				if (m_RenameTarget == RenameTarget::Project)
					(void)m_Store->RenameProject(name);
				else if (m_RenameTarget == RenameTarget::Group)
					(void)m_Store->RenameGroup(m_RenameTargetId, name);
				else if (m_RenameTarget == RenameTarget::Series)
					(void)m_Store->RenameSeries(m_RenameTargetId, name);
				else if (m_RenameTarget == RenameTarget::Characteristic)
					(void)m_Store->RenameCharacteristic(static_cast<CharacteristicId>(std::stoull(m_RenameTargetId)), name);
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

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
