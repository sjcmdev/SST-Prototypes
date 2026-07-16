#include "Core/dspch.hpp"

#include "Presentation/Panels/CharacteristicDetailsPanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{
	CharacteristicDetailsPanel::CharacteristicDetailsPanel(Ref<JFMWorkspaceStore> store)
		: IPanel("Characteristic Details", true), m_Store(std::move(store))
	{
	}

	Ref<IPanel> CharacteristicDetailsPanel::Clone() const
	{
		return CreateRef<CharacteristicDetailsPanel>(*this);
	}

	void CharacteristicDetailsPanel::Render()
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

		auto selected = m_Store != nullptr ? m_Store->GetSelectedCharacteristicRecord() : std::optional<JFMCharacteristicRecord>{};
		if (!selected)
		{
			ImGui::TextDisabled("No characteristic selected");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		const JFMCharacteristicInfo &info = selected->info;
		ImGui::Text("Name: %s", info.name.c_str());
		ImGui::Text("Source: %s", info.sourcePath.String().c_str());
		ImGui::Text("Parser: %s", info.parser.c_str());
		if (info.temperatureK)
			ImGui::Text("Temperature: %.2f K (%s)", *info.temperatureK, info.temperatureSource.c_str());
		else
			ImGui::Text("Temperature: missing");

		if (ImGui::CollapsingHeader("Metadata", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const auto &[key, value] : info.metadata)
				ImGui::Text("%s: %s", key.c_str(), value.c_str());
			if (info.metadata.empty())
				ImGui::TextDisabled("No metadata");
		}

		if (ImGui::CollapsingHeader("QC warnings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (const std::string &warning : info.qc.warnings)
				ImGui::BulletText("%s", warning.c_str());
			if (info.qc.warnings.empty())
				ImGui::TextDisabled("No warnings");
		}

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
