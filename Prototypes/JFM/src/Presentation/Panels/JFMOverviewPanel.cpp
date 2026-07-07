#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMOverviewPanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{

	std::size_t countCharacteristics(const JFMProject &project)
	{
		std::size_t count = 0;
		for (const JFMSeries &series : project.series)
			count += series.characteristics.size();
		return count;
	}
	

	JFMOverviewPanel::JFMOverviewPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Store(std::move(store))
	{
	}

	Ref<IPanel> JFMOverviewPanel::Clone() const
	{
		return CreateRef<JFMOverviewPanel>(*this);
	}

	void JFMOverviewPanel::Render()
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
		const auto projectPath = m_Store != nullptr ? m_Store->GetProjectPath() : std::optional<Path>{};
		const auto selected = m_Store != nullptr ? m_Store->GetSelectedCharacteristic() : std::optional<JFMCharacteristic>{};

		ImGui::TextUnformatted(project.name.c_str());
		ImGui::Separator();
		ImGui::Text("Series: %zu", project.series.size());
		ImGui::Text("Characteristics: %zu", countCharacteristics(project));
		ImGui::Text("Fit runs: %zu", project.fitRuns.size());
		ImGui::Text("MC runs: %zu", project.monteCarloRuns.size());
		ImGui::Text("Project file: %s", projectPath ? projectPath->String().c_str() : "<unsaved>");
		ImGui::Spacing();
		if (selected)
		{
			ImGui::Text("Selected: %s", selected->name.c_str());
			ImGui::Text("Points: %zu", selected->voltage.size());
			ImGui::Text("Temperature: %s",
			            selected->temperatureK ? std::to_string(*selected->temperatureK).c_str() : "<missing>");
			ImGui::Text("Source: %s", selected->sourcePath.c_str());
		}
		else
		{
			ImGui::TextDisabled("No characteristic selected");
		}

		ImGui::Separator();
		const std::string status = m_Store != nullptr ? m_Store->GetStatusMessage() : std::string{};
		ImGui::TextWrapped("%s", status.c_str());

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
