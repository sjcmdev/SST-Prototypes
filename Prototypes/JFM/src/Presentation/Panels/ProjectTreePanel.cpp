#include "Core/dspch.hpp"

#include "Presentation/Panels/ProjectTreePanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{
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

		if (ImGui::TreeNodeEx((project.name + "##project").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{ //* Note: Za dużo wcięć. można to podzielićna mniejsze kawałki kodu
			for (const JFMSeries &series : project.series)
			{
				const std::string seriesLabel = series.name + "##" + series.id;
				if (ImGui::TreeNodeEx(seriesLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					for (const JFMCharacteristic &characteristic : series.characteristics)
					{
						const bool selected = characteristic.id == selectedCharacteristic;
						const std::string label = characteristic.name + "##" + characteristic.id;
						if (ImGui::Selectable(label.c_str(), selected) && m_Store != nullptr)
						{
							m_Store->SelectSeries(series.id);
							m_Store->SelectCharacteristic(characteristic.id);
						}
					}
					if (series.characteristics.empty())
						ImGui::TextDisabled("No characteristics");
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
