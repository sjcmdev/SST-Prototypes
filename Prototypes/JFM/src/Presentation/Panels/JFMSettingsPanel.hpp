#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"

#include <array>

namespace SST::Prototypes::JFM
{
	class JFMSettingsPanel final : public IPanel
	{
	public:
		explicit JFMSettingsPanel(Ref<JFMUserSettingsStore> settings);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;
		void RenderContent();

	private:
		void renderPathRow(const char *label, const Path &path, void (JFMUserSettingsStore::*setter)(Path));
		void renderPlotDefaults(JFMUserSettings settings);
		void renderNumericSettings(JFMUserSettings settings);
		void renderColorPresets(JFMUserSettings settings);

	private:
		Ref<JFMUserSettingsStore> m_Settings;
		std::array<char, 80> m_NewPresetName = {};
		std::array<float, 4> m_NewPresetStart = {0.15f, 0.75f, 1.00f, 1.00f};
		std::array<float, 4> m_NewPresetEnd = {1.00f, 0.12f, 0.55f, 1.00f};
		bool m_PlotDraftInitialized = false;
		bool m_DefaultPlotSemiLog = true;
		bool m_DefaultPlotAutoFit = true;
		bool m_DefaultPlotShowLegend = true;
		float m_DefaultPlotFitPadding = 0.08f;
		bool m_ShowPrefitCurves = true;
		std::array<float, 4> m_PrefitCurveColor = {0.20f, 0.85f, 0.36f, 1.00f};
		float m_PrefitCurveLineWeight = 2.0f;
		bool m_PrefitCurvesFitRangeOnly = false;
		float m_HoveredCurveLineWeight = 3.5f;
	};
} // namespace SST::Prototypes::JFM
