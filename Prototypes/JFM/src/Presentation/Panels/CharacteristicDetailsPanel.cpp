#include "Core/dspch.hpp"

#include "Presentation/Panels/CharacteristicDetailsPanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{
	CharacteristicDetailsPanel::CharacteristicDetailsPanel(Ref<JFMWorkspaceStore> store)
		: EditorWindow("jfm.details", "Characteristic Details", true, {.duplicable = true}), m_Store(std::move(store))
	{
	}

	WindowDuplicate CharacteristicDetailsPanel::Duplicate() const
	{
		return {CreateRef<CharacteristicDetailsPanel>(*this), {}};
	}

	void CharacteristicDetailsPanel::RenderContent()
	{
		auto selected = m_Store != nullptr ? m_Store->GetSelectedCharacteristicRecord() : std::optional<JFMCharacteristicRecord>{};
		if (!selected)
		{
			ImGui::TextDisabled("No characteristic selected");

			return;
		}

		const JFMCharacteristicInfo &info = selected->info;
		ImGui::Text("Name: %s", info.name.c_str());
		ImGui::Text("Source: %s", info.sourcePath.Utf8().c_str());
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
	}
} // namespace SST::Prototypes::JFM
