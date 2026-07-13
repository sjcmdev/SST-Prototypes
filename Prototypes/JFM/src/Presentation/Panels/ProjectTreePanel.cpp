#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{

	void renderCharacteristicNode(
		const JFMSeries &series,
		const CharacteristicId &characteristicId,
		const CharacteristicId selectedCharacteristic,
		const Ref<JFMWorkspaceStore> &store)
	{
		const auto characteristic = store != nullptr ? store->FindCharacteristic(characteristicId) : std::optional<JFMCharacteristic>{};
		const std::string name = characteristic ? characteristic->info.name : std::to_string(characteristicId);
		const bool hasWarning = characteristic
			&& (!characteristic->info.qc.warnings.empty()
				|| (!characteristic->info.sourcePath.Empty() && characteristic->data.voltage.empty()));
		const bool selected = characteristicId == selectedCharacteristic;
		const std::string label = name + (hasWarning ? " [!]" : "") + "##" + std::to_string(characteristicId);
		if (ImGui::Selectable(label.c_str(), selected) && store != nullptr)
		{
			store->SelectSeries(series.id);
			store->SelectCharacteristic(characteristicId);
		}
		if (hasWarning && ImGui::IsItemHovered())
			ImGui::SetTooltip("Raw arrays unavailable; see Import Status.");
	}

	void renderSeriesNode(
		const JFMSeries &series,
		const CharacteristicId selectedCharacteristic,
		const Ref<JFMWorkspaceStore> &store)
	{
		const std::string seriesLabel = series.name + "##" + series.id;
		if (!ImGui::TreeNodeEx(seriesLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			return;

		for (const CharacteristicId &characteristicId : series.characteristicIds)
			renderCharacteristicNode(series, characteristicId, selectedCharacteristic, store);
		if (series.characteristicIds.empty())
			ImGui::TextDisabled("No characteristics");
		ImGui::TreePop();
	}

	void renderGroupNode(
		const JFMProject &project,
		const JFMGroup &group,
		const CharacteristicId selectedCharacteristic,
		const Ref<JFMWorkspaceStore> &store)
	{
		const std::string groupLabel = group.name + "##" + group.id;
		if (!ImGui::TreeNodeEx(groupLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			return;

		for (const JFMGroup &child : project.groups)
		{
			if (child.parentId == group.id)
				renderGroupNode(project, child, selectedCharacteristic, store);
		}
		for (const JFMSeries &series : project.series)
		{
			if (series.groupId == group.id)
				renderSeriesNode(series, selectedCharacteristic, store);
		}
		ImGui::TreePop();
	}

	void renderProjectNode(
		const JFMProject &project,
		const CharacteristicId selectedCharacteristic,
		const Ref<JFMWorkspaceStore> &store)
	{
		if (!ImGui::TreeNodeEx((project.name + "##project").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			return;

		for (const JFMGroup &group : project.groups)
		{
			if (group.parentId.empty())
				renderGroupNode(project, group, selectedCharacteristic, store);
		}
		for (const JFMSeries &series : project.series)
		{
			if (series.groupId.empty())
				renderSeriesNode(series, selectedCharacteristic, store);
		}
		ImGui::TreePop();
	}
	

	ProjectTreePanel::ProjectTreePanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Store(std::move(store))
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

		const JFMProject project = m_Store != nullptr ? m_Store->GetProjectSnapshot() : JFMProject{};
		const CharacteristicId selectedCharacteristic = m_Store != nullptr ? m_Store->GetSelectedCharacteristicId() : InvalidCharacteristicId;
		renderProjectNode(project, selectedCharacteristic, m_Store);

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
