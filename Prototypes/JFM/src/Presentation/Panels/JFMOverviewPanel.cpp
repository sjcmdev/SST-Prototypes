#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMOverviewPanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{

	std::size_t countCharacteristics(const JFMProject &project)
	{
		return project.characteristics.size();
	}
	

	JFMOverviewPanel::JFMOverviewPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: EditorWindow("jfm.overview", std::move(title), visibleByDefault, {.duplicable = true}), m_Store(std::move(store))
	{
	}

	WindowDuplicate JFMOverviewPanel::Duplicate() const
	{
		return {CreateRef<JFMOverviewPanel>(*this), {}};
	}

	void JFMOverviewPanel::RenderContent()
	{
		const JFMProject project = m_Store != nullptr ? m_Store->GetProjectSnapshot() : JFMProject{};
		const auto projectPath = m_Store != nullptr ? m_Store->GetProjectPath() : std::optional<Path>{};
		const auto selected = m_Store != nullptr ? m_Store->GetSelectedCharacteristicRecord() : std::optional<JFMCharacteristicRecord>{};

		ImGui::TextUnformatted(project.name.c_str());
		ImGui::Separator();
		ImGui::Text("Series: %zu", project.series.size());
		ImGui::Text("Characteristics: %zu", countCharacteristics(project));
		ImGui::Text("Fit runs: %zu", project.fitRuns.size());
		ImGui::Text("MC runs: %zu", project.monteCarloRuns.size());
		ImGui::Text("Project file: %s", projectPath ? projectPath->Utf8().c_str() : "<unsaved>");
		ImGui::Spacing();
		if (selected)
		{
			ImGui::Text("Selected: %s", selected->info.name.c_str());
			ImGui::Text("Points: %zu", selected->data ? selected->data->voltage.size() : 0);
			ImGui::Text("Temperature: %s",
			            selected->info.temperatureK ? std::to_string(*selected->info.temperatureK).c_str() : "<missing>");
			ImGui::Text("Source: %s", selected->info.sourcePath.Utf8().c_str());
		}
		else
		{
			ImGui::TextDisabled("No characteristic selected");
		}

		ImGui::Separator();
		const std::string status = m_Store != nullptr ? m_Store->GetStatusMessage() : std::string{};
		ImGui::TextWrapped("%s", status.c_str());

	}
} // namespace SST::Prototypes::JFM
