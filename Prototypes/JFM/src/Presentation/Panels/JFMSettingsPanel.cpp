#include "Core/dspch.hpp"

#include "Presentation/Panels/JFMSettingsPanel.hpp"

#include "Core/Platform/NativeFileDialog.hpp"

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

		RenderContent();

		ImGui::End();
		SetVisible(visible);
	}

	void JFMSettingsPanel::RenderContent()
	{
		if (m_Settings == nullptr)
			return;

		const JFMUserSettings settings = m_Settings->GetSnapshot();
		ImGui::SeparatorText("JFM Paths");
		renderPathRow("Import directory", settings.defaultImportDirectory, &JFMUserSettingsStore::SetDefaultImportDirectory);
		renderPathRow("Projects directory", settings.defaultProjectsDirectory, &JFMUserSettingsStore::SetDefaultProjectsDirectory);
		renderPathRow("Scripts directory", settings.defaultScriptsDirectory, &JFMUserSettingsStore::SetDefaultScriptsDirectory);
		renderPlotDefaults(settings);
		renderNumericSettings(settings);
		renderColorPresets(settings);
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
		if (!m_PlotDraftInitialized)
		{
			m_DefaultPlotSemiLog = settings.defaultPlotSemiLog;
			m_DefaultPlotAutoFit = settings.defaultPlotAutoFit;
			m_DefaultPlotShowLegend = settings.defaultPlotShowLegend;
			m_DefaultPlotFitPadding = settings.defaultPlotFitPadding;
			m_ShowPrefitCurves = settings.showPrefitCurves;
			m_PrefitCurveColor = settings.prefitCurveColor;
			m_PrefitCurveLineWeight = settings.prefitCurveLineWeight;
			m_PrefitCurvesFitRangeOnly = settings.prefitCurvesFitRangeOnly;
			m_HoveredCurveLineWeight = settings.hoveredCurveLineWeight;
			m_PlotDraftInitialized = true;
		}
		bool changed = false;
		changed |= ImGui::Checkbox("Semi-log by default", &m_DefaultPlotSemiLog);
		changed |= ImGui::Checkbox("Auto fit by default", &m_DefaultPlotAutoFit);
		changed |= ImGui::Checkbox("Show legend by default", &m_DefaultPlotShowLegend);
		ImGui::SliderFloat("Default scroll out", &m_DefaultPlotFitPadding, 0.0f, 0.5f, "%.2f");
		const bool fitPaddingCommitted = ImGui::IsItemDeactivatedAfterEdit();
		bool autoPlot = settings.autoPlotAfterImport;
		if (ImGui::Checkbox("Plot after import by default", &autoPlot))
			m_Settings->SetAutoPlotAfterImport(autoPlot);
		if (changed)
			m_Settings->SetPlotDefaults(m_DefaultPlotSemiLog, m_DefaultPlotAutoFit, m_DefaultPlotShowLegend, m_DefaultPlotFitPadding);
		if (fitPaddingCommitted)
			m_Settings->SetPlotDefaults(m_DefaultPlotSemiLog, m_DefaultPlotAutoFit, m_DefaultPlotShowLegend, m_DefaultPlotFitPadding);

		ImGui::SeparatorText("Prefit curves");
		bool prefitChanged = false;
		prefitChanged |= ImGui::Checkbox("Show prefit curves", &m_ShowPrefitCurves);
		prefitChanged |= ImGui::Checkbox("Show only in fit range", &m_PrefitCurvesFitRangeOnly);
		prefitChanged |= ImGui::ColorEdit4("Prefit curve color", m_PrefitCurveColor.data(), ImGuiColorEditFlags_NoInputs);
		ImGui::SliderFloat("Prefit curve thickness", &m_PrefitCurveLineWeight, 0.5f, 8.0f, "%.1f");
		const bool prefitWeightCommitted = ImGui::IsItemDeactivatedAfterEdit();
		ImGui::SliderFloat("Hovered curve thickness", &m_HoveredCurveLineWeight, 0.5f, 10.0f, "%.1f");
		const bool hoverWeightCommitted = ImGui::IsItemDeactivatedAfterEdit();
		if (prefitChanged || prefitWeightCommitted || hoverWeightCommitted)
			m_Settings->SetPrefitCurveDefaults(m_ShowPrefitCurves, m_PrefitCurveColor, m_PrefitCurveLineWeight, m_PrefitCurvesFitRangeOnly, m_HoveredCurveLineWeight);
	}

	void JFMSettingsPanel::renderNumericSettings(JFMUserSettings settings)
	{
		ImGui::SeparatorText("JFM Numeric");
		JFMFitMonteCarloSettings solver = settings.fitMonteCarlo;
		bool changed = false;
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputDouble("Reduced chi2 target", &solver.fitReducedChiSquaredTolerance, 0.1, 1.0, "%.6g");
		changed |= ImGui::Checkbox("Auto-calibrate GPU simplex iterations", &solver.autoCalibrateGpuSimplexIterations);
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputInt("Simplex calibration runs", &solver.simplexCalibrationRuns, 10, 100);
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputInt("Simplex calibration margin", &solver.simplexCalibrationExtraIterations, 10, 50);
		if (changed)
			m_Settings->SetFitMonteCarloSettings(std::move(solver));

		ImGui::SeparatorText("JFM Auto Prefit");
		JFMAutoPrefitSettings autoPrefit = settings.autoPrefit;
		bool autoPrefitChanged = false;
		ImGui::SetNextItemWidth(130.0f);
		autoPrefitChanged |= ImGui::InputDouble("Residual window fraction", &autoPrefit.residualWindowFraction, 0.01, 0.05, "%.3f");
		ImGui::SetNextItemWidth(130.0f);
		autoPrefitChanged |= ImGui::InputInt("Residual window min points", &autoPrefit.residualWindowMinPoints, 1, 5);
		ImGui::SetNextItemWidth(130.0f);
		autoPrefitChanged |= ImGui::InputInt("Residual window max points", &autoPrefit.residualWindowMaxPoints, 1, 5);
		if (autoPrefitChanged)
			m_Settings->SetAutoPrefitSettings(autoPrefit);
	}

	void JFMSettingsPanel::renderColorPresets(JFMUserSettings settings)
	{
		ImGui::SeparatorText("JFM Color Presets");
		std::vector<JFMColorPreset> presets = settings.colorPresets;
		const char *defaultName = settings.defaultColorPresetName.empty() ? "<none>" : settings.defaultColorPresetName.c_str();
		if (ImGui::BeginCombo("Default preset", defaultName))
		{
			for (const JFMColorPreset &preset : presets)
			{
				ImGui::ColorButton(("##default_start_" + preset.name).c_str(), ImVec4(preset.start[0], preset.start[1], preset.start[2], preset.start[3]));
				ImGui::SameLine();
				ImGui::ColorButton(("##default_end_" + preset.name).c_str(), ImVec4(preset.end[0], preset.end[1], preset.end[2], preset.end[3]));
				ImGui::SameLine();
				if (ImGui::Selectable(preset.name.c_str(), preset.name == settings.defaultColorPresetName))
					m_Settings->SetDefaultColorPreset(preset.name);
			}
			ImGui::EndCombo();
		}
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
