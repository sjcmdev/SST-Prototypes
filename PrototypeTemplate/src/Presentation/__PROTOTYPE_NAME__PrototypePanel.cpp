#include "Core/dspch.hpp"

#include "Presentation/__PROTOTYPE_NAME__PrototypePanel.hpp"

#include <imgui.h>

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
    static constexpr double MinVoltage = 0.1, MaxVoltage = 5, MinTemperature = 50, MaxTemperature = 900;
	__PROTOTYPE_NAME__PrototypePanel::__PROTOTYPE_NAME__PrototypePanel(
		PrototypeWorkflow *store,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(store)
	{
	}

	Ref<IPanel> __PROTOTYPE_NAME__PrototypePanel::Clone() const
	{
		return CreateRef<__PROTOTYPE_NAME__PrototypePanel>(*this);
	}

	void __PROTOTYPE_NAME__PrototypePanel::Render()
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


        auto &doc = m_Store->document;
        ImGui::Text("Path: %s", doc.path.Empty() ? "Untitled" : doc.path.Utf8().c_str());
        ImGui::Text("Revision: %llu%s", static_cast<unsigned long long>(doc.revision), doc.dirty ? " (unsaved)" : "");
        auto input = doc.input;
        bool changed = ImGui::SliderScalar("Voltage", ImGuiDataType_Double, &input.voltage,
            &MinVoltage, &MaxVoltage);
        changed |= ImGui::SliderScalar("Temperature K", ImGuiDataType_Double, &input.temperatureKelvin,
            &MinTemperature, &MaxTemperature);
        if (changed) doc.Edit(input);
        ImGui::Text(m_Store->scientificNotation ? "Conductivity (last evaluation): %.6e" : "Conductivity (last evaluation): %.6f", doc.result.conductivity);
        ImGui::TextWrapped("%s", m_Store->status.c_str());
        if (const auto job = m_Store->jobs->GetJob(m_Store->activeJob))
        {
            ImGui::TextUnformatted(job->currentStage.c_str());
            ImGui::ProgressBar(job->totalWork > 0 ? job->completedWork / job->totalWork : 0);
            if (ImGui::Button("Cancel")) (void)m_Store->jobs->RequestCancel(job->id);
        }

		ImGui::End();
	}
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

