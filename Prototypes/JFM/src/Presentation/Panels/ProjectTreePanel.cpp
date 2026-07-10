#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{

	void renderCharacteristicNode(
		const JFMSeries &series,
		const CharacteristicId &characteristicId,
		const std::string &selectedCharacteristic,
		const Ref<JFMWorkspaceStore> &store)
	{
		const auto characteristic = store != nullptr ? store->FindCharacteristic(characteristicId) : std::optional<JFMCharacteristic>{};
		const std::string &name = characteristic ? characteristic->info.name : characteristicId;
		const bool selected = characteristicId == selectedCharacteristic;
		const std::string label = name + "##" + characteristicId;
		if (ImGui::Selectable(label.c_str(), selected) && store != nullptr)
		{
			store->SelectSeries(series.id);
			store->SelectCharacteristic(characteristicId);
		}
	}

	void renderSeriesNode(
		const JFMSeries &series,
		const std::string &selectedCharacteristic,
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

	void renderProjectNode(
		const JFMProject &project,
		const std::string &selectedCharacteristic,
		const Ref<JFMWorkspaceStore> &store)
	{
		if (!ImGui::TreeNodeEx((project.name + "##project").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			return;

		for (const JFMSeries &series : project.series)
			renderSeriesNode(series, selectedCharacteristic, store);
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
		const std::string selectedCharacteristic = m_Store != nullptr ? m_Store->GetSelectedCharacteristicId() : std::string{};
		renderProjectNode(project, selectedCharacteristic, m_Store);

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
