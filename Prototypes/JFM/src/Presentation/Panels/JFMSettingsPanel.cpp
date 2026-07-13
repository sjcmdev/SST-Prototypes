#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMSettingsPanel.hpp"

#include "App/NativeFileDialog.hpp"

#include <imgui.h>

namespace SST::Prototypes::JFM
{
	JFMSettingsPanel::JFMSettingsPanel(Ref<JFMUserSettingsStore> settings)
		: IPanel("JFM Settings", false), m_Settings(std::move(settings))
	{
	}

	Ref<IPanel> JFMSettingsPanel::Clone() const
	{
		return CreateRef<JFMSettingsPanel>(*this);
	}

	void JFMSettingsPanel::Render()
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

		if (m_Settings != nullptr)
		{
			const JFMUserSettings settings = m_Settings->GetSnapshot();
			renderPathRow("Import directory", settings.defaultImportDirectory, &JFMUserSettingsStore::SetDefaultImportDirectory);
			renderPathRow("Projects directory", settings.defaultProjectsDirectory, &JFMUserSettingsStore::SetDefaultProjectsDirectory);
		}

		ImGui::End();
		SetVisible(visible);
	}

	void JFMSettingsPanel::renderPathRow(const char *label, const Path &path, void (JFMUserSettingsStore::*setter)(Path))
	{
		ImGui::TextUnformatted(label);
		ImGui::SameLine(160.0f);
		ImGui::TextWrapped("%s", path.String().c_str());
		ImGui::SameLine();
		if (ImGui::Button(("...##" + std::string(label)).c_str()))
		{
			if (std::vector<Path> paths = PickFoldersDialog(false, path); !paths.empty() && m_Settings != nullptr)
				((*m_Settings).*setter)(paths.front());
		}
	}
} // namespace SST::Prototypes::JFM
