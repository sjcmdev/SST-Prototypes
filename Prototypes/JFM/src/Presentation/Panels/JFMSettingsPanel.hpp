#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/Utils/Memory.hpp"
#include "Presentation/Panels/IPanel.hpp"

namespace SST::Prototypes::JFM
{
	class JFMSettingsPanel final : public IPanel
	{
	public:
		explicit JFMSettingsPanel(Ref<JFMUserSettingsStore> settings);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		void renderPathRow(const char *label, const Path &path, void (JFMUserSettingsStore::*setter)(Path));

	private:
		Ref<JFMUserSettingsStore> m_Settings;
	};
} // namespace SST::Prototypes::JFM
