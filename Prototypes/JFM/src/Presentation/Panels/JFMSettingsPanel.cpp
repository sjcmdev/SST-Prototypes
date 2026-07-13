#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMSettingsPanel.hpp"

#include "App/NativeFileDialog.hpp"

#include <cstddef>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <utility>

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
			ImGui::SeparatorText("JFM Paths");
			renderPathRow("Import directory", settings.defaultImportDirectory, &JFMUserSettingsStore::SetDefaultImportDirectory);
			renderPathRow("Projects directory", settings.defaultProjectsDirectory, &JFMUserSettingsStore::SetDefaultProjectsDirectory);
			renderPathRow("Scripts directory", settings.defaultScriptsDirectory, &JFMUserSettingsStore::SetDefaultScriptsDirectory);
			renderPlotDefaults(settings);
			renderColorPresets(settings);
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

	void JFMSettingsPanel::renderPlotDefaults(JFMUserSettings settings)
	{
		ImGui::SeparatorText("JFM Plot Defaults");
		bool semiLog = settings.defaultPlotSemiLog;
		bool autoFit = settings.defaultPlotAutoFit;
		bool showLegend = settings.defaultPlotShowLegend;
		float fitPadding = settings.defaultPlotFitPadding;
		bool changed = false;
		changed |= ImGui::Checkbox("Semi-log by default", &semiLog);
		changed |= ImGui::Checkbox("Auto fit by default", &autoFit);
		changed |= ImGui::Checkbox("Show legend by default", &showLegend);
		changed |= ImGui::SliderFloat("Default scroll out", &fitPadding, 0.0f, 0.5f, "%.2f");
		bool autoPlot = settings.autoPlotAfterImport;
		if (ImGui::Checkbox("Plot after import by default", &autoPlot))
			m_Settings->SetAutoPlotAfterImport(autoPlot);
		if (changed)
			m_Settings->SetPlotDefaults(semiLog, autoFit, showLegend, fitPadding);
	}

	void JFMSettingsPanel::renderColorPresets(JFMUserSettings settings)
	{
		ImGui::SeparatorText("JFM Color Presets");
		std::vector<JFMColorPreset> presets = settings.colorPresets;
		for (std::size_t index = 0; index < presets.size();)
		{
			ImGui::PushID(static_cast<int>(index));
			ImGui::ColorButton("##start", ImVec4(presets[index].start[0], presets[index].start[1], presets[index].start[2], presets[index].start[3]));
			ImGui::SameLine();
			ImGui::ColorButton("##end", ImVec4(presets[index].end[0], presets[index].end[1], presets[index].end[2], presets[index].end[3]));
			ImGui::SameLine();
			ImGui::TextUnformatted(presets[index].name.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				presets.erase(presets.begin() + static_cast<std::ptrdiff_t>(index));
				m_Settings->SetColorPresets(presets);
				ImGui::PopID();
				continue;
			}
			ImGui::PopID();
			++index;
		}

		ImGui::Separator();
		if (m_NewPresetName[0] == '\0')
			std::snprintf(m_NewPresetName.data(), m_NewPresetName.size(), "%s", "Custom");
		ImGui::InputText("Preset name", m_NewPresetName.data(), m_NewPresetName.size());
		ImGui::ColorEdit4("Start color", m_NewPresetStart.data(), ImGuiColorEditFlags_NoInputs);
		ImGui::SameLine();
		ImGui::ColorEdit4("End color", m_NewPresetEnd.data(), ImGuiColorEditFlags_NoInputs);
		if (ImGui::Button("Add preset"))
		{
			JFMColorPreset preset;
			preset.name = m_NewPresetName.data();
			preset.start = m_NewPresetStart;
			preset.end = m_NewPresetEnd;
			presets.push_back(std::move(preset));
			m_Settings->SetColorPresets(std::move(presets));
		}
	}
} // namespace SST::Prototypes::JFM
