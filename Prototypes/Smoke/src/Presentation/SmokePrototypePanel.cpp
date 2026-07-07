#include "Core/dspch.hpp"

#include "Presentation/SmokePrototypePanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::Smoke
{
	SmokePrototypePanel::SmokePrototypePanel(
		Ref<PrototypeResultStore> store,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store))
	{
	}

	Ref<IPanel> SmokePrototypePanel::Clone() const
	{
		return CreateRef<SmokePrototypePanel>(*this);
	}

	void SmokePrototypePanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			SetVisible(visible);
			ImGui::End();
			return;
		}
		SetVisible(visible);

		ImGui::TextUnformatted("Domain backend");
		ImGui::Separator();

		float voltage = static_cast<float>(m_Input.voltage);
		float temperature = static_cast<float>(m_Input.temperatureKelvin);
		bool changed = ImGui::SliderFloat("Voltage", &voltage, 0.1F, 5.0F)
			|| ImGui::SliderFloat("Temperature K", &temperature, 50.0F, 900.0F);

		if (changed)
		{
			m_Input.voltage = voltage;
			m_Input.temperatureKelvin = temperature;
			if (m_Store != nullptr)
				m_Store->Save(PrototypeBackend::Evaluate(m_Input));
		}

		if (m_Store != nullptr)
		{
			if (const auto &result = m_Store->LastResult())
			{
				ImGui::Text("Carrier density: %.3e", result->carrierDensity);
				ImGui::Text("Mobility: %.3f", result->mobility);
				ImGui::Text("Conductivity: %.6f", result->conductivity);
			}
		}
		else
		{
			ImGui::TextDisabled("Storage unavailable.");
		}

		ImGui::End();
	}
} // namespace SST::Prototypes::Smoke

