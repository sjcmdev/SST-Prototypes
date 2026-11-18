#include "Core/dspch.hpp"

#include "Presentation/__PROTOTYPE_NAME__PrototypePanel.hpp"

#include <imgui.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <sst/ui/Theme/ScientificPlotPreset.hpp>

namespace SST::Prototypes::__PROTOTYPE_NAME__
{
    static constexpr double MinVoltage = 0.1, MaxVoltage = 5, MinTemperature = 50, MaxTemperature = 900;
	__PROTOTYPE_NAME__PrototypePanel::__PROTOTYPE_NAME__PrototypePanel(
		PrototypeWorkflow *store,
		std::string title,
		bool visibleByDefault)
		: EditorWindow("prototype.document", std::move(title), visibleByDefault, {.duplicable = true}),
		  m_Store(store)
	{
	}

	WindowDuplicate __PROTOTYPE_NAME__PrototypePanel::Duplicate() const
	{
		return {CreateRef<__PROTOTYPE_NAME__PrototypePanel>(*this), {}};
	}

	void __PROTOTYPE_NAME__PrototypePanel::RenderContent()
	{
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
        const double voltage[] = {0.0, doc.input.voltage};
        const double current[] = {0.0, doc.result.conductivity * doc.input.voltage};
        sst::ui::plot::PlotSpec plot;
        plot.x.label = "Voltage (V)";
        plot.y.label = "Conductivity x voltage";
        plot.autoFit = sst::ui::plot::AutoFit::Always;
        sst::ui::plot::SeriesEntryView series{"Evaluation", {voltage, current, 2}};
        const auto preset = sst::ui::plot::ScientificPreset();
        series.style.color = sst::ui::plot::ScientificColor(0);
        series.style.lineWidthPt = preset.lineWidthPt;
        sst::ui::plot::PlotViewState plotState;
        sst::ui::plot::RenderPlot("##evaluation", plot, &series, 1, plotState, {}, ImVec2(-1, 180));
        if (const auto job = m_Store->jobs->GetJob(m_Store->activeJob))
        {
            ImGui::TextUnformatted(job->currentStage.c_str());
            ImGui::ProgressBar(job->totalWork > 0 ? job->completedWork / job->totalWork : 0);
            if (ImGui::Button("Cancel")) (void)m_Store->jobs->RequestCancel(job->id);
        }

	}
} // namespace SST::Prototypes::__PROTOTYPE_NAME__

