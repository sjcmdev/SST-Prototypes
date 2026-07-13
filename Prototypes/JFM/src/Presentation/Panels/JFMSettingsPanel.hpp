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
		void renderColorPresets(JFMUserSettings settings);

	private:
		Ref<JFMUserSettingsStore> m_Settings;
		std::array<char, 80> m_NewPresetName = {};
		std::array<float, 4> m_NewPresetStart = {0.15f, 0.75f, 1.00f, 1.00f};
		std::array<float, 4> m_NewPresetEnd = {1.00f, 0.12f, 0.55f, 1.00f};
	};
} // namespace SST::Prototypes::JFM
