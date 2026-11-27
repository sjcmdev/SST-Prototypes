#include "Core/dspch.hpp"
#include <map>

#include "Presentation/Panels/MonteCarloCurvesPanel.hpp"

#include "Analysis/JFMFitEngine.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <implot.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <unordered_map>

namespace SST::Prototypes::JFM
{
	namespace
	{
		std::vector<double> FitCurveForSample(
			const JFMMonteCarloResult &result,
			const JFMMonteCarloSample &sample,
			std::string_view modelId,
			double temperatureK)
		{
			std::unordered_map<std::string, double> parameters;
			const std::size_t count = std::min(result.parameterNames.size(), sample.parameters.size());
			for (std::size_t index = 0; index < count; ++index)
				parameters[result.parameterNames[index]] = sample.parameters[index];
			std::vector<double> current;
			current.reserve(result.voltage.size());
			for (double voltage : result.voltage)
				current.push_back(EvaluateJFMCurrent(modelId, voltage, temperatureK, parameters));
			return current;
		}

		void PlotCurrent(const char *label, const std::vector<double> &voltage, const double *current, std::size_t count, ImPlotSpec spec, bool semiLog)
		{
			count = std::min(count, voltage.size());
			if (!semiLog)
			{
				sst::ui::plot::RenderSeries({label, {voltage.data(), current, count}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(spec)});
				return;
			}
			std::vector<double> x;
			std::vector<double> y;
			x.reserve(count);
			y.reserve(count);
			for (std::size_t index = 0; index < count; ++index)
			{
				const double value = std::abs(current[index]);
				if (value <= 0.0)
					continue;
				x.push_back(voltage[index]);
				y.push_back(value);
			}
			sst::ui::plot::RenderSeries({label, {x.data(), y.data(), x.size()}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(spec)});
		}
	}

	MonteCarloCurvesPanel::MonteCarloCurvesPanel(Ref<JFMWorkspaceStore> store, Ref<EventBus> eventBus, std::string title, bool visibleByDefault)
		: EditorWindow("jfm.mc-curves", std::move(title), visibleByDefault, {.duplicable = true}),
		  m_Store(std::move(store)),
		  m_EventBus(std::move(eventBus))
	{
		if (m_EventBus != nullptr)
		{
			AddSubscription(m_EventBus->Subscribe<MonteCarloRequested>([this](const MonteCarloRequested &event) {
				for (CharacteristicId id : event.ids)
					AddPlot({event.project, id});
			}));
			AddSubscription(m_EventBus->Subscribe<MonteCarloCompleted>([this](const MonteCarloCompleted &event) {
				AddPlot(event.ref);
			}));
		}
	}

	WindowDuplicate MonteCarloCurvesPanel::Duplicate() const
	{
		return {CreateRef<MonteCarloCurvesPanel>(m_Store, m_EventBus, GetTitle(), IsVisible()), {}};
	}

	void MonteCarloCurvesPanel::AddPlot(JFMCharacteristicRef ref)
	{
		if (ref.project == InvalidProjectHandle || ref.characteristic == InvalidCharacteristicId)
			return;
		if (std::find(m_Plots.begin(), m_Plots.end(), ref) == m_Plots.end())
			m_Plots.push_back(ref);
	}

	void MonteCarloCurvesPanel::RenderContent()
	{
		ZoneScopedN("JFM.preview");

		if (m_Store == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");

			return;
		}

		const ProjectHandle project = m_Store->GetActiveProjectHandle();
		if (ImGui::Button("Add selected"))
		{
			for (CharacteristicId id : m_Store->GetSelectedCharacteristicIds())
				AddPlot({project, id});
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear"))
			m_Plots.clear();
        if (ImGui::BeginTable("##curve_controls", 2, ImGuiTableFlags_SizingFixedFit))
        {
            const std::pair<const char *, bool *> controls[] = {
                {"Ideal", &m_ShowIdeal}, {"Noisy", &m_ShowNoisy}, {"Fitted", &m_ShowFitted},
                {"Semi-log", &m_SemiLog}, {"Single iteration", &m_SingleIteration},
                {"Show rejected (dimmed)", &m_ShowRejected}};
            for (const auto &[label, value] : controls)
            {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label); ImGui::TableSetColumnIndex(1);
                ImGui::PushID(label); ImGui::Checkbox("##value", value); ImGui::PopID();
            }
            ImGui::EndTable();
        }

		for (JFMCharacteristicRef ref : m_Plots)
		{
			const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult(ref);
			const auto record = m_Store->FindCharacteristicRecord(ref);
			if (result == nullptr || !record || !record->info.temperatureK || result->voltage.empty() || result->samples.empty())
				continue;
            const auto accepted = std::count_if(result->samples.begin(), result->samples.end(), [](const auto &s) { return s.converged; });
            const auto converged = std::count_if(result->samples.begin(), result->samples.end(), [](const auto &s) { return s.solverConverged; });
            const bool previous = record->info.monteCarlo && record->info.monteCarlo->status == "running";
            ImGui::TextDisabled("%s", previous ? "MC running - showing previous result" : "MC result");
            ImGui::Text("%s: %d/%d accepted, %d converged", record->info.name.c_str(), static_cast<int>(accepted), static_cast<int>(result->samples.size()), static_cast<int>(converged));
			std::map<std::string, int> reasons;
            int fallback = 0, flat = 0;
            for (const auto &s : result->samples) { ++reasons[s.stopReason]; fallback += s.fitMethod == "LM→NM"; flat += s.flatDirections; }
            ImGui::Text("LM fallback: %d | Flat directions: %d", fallback, flat);
            for (const auto &[reason, count] : reasons) ImGui::Text("%s: %d", reason.c_str(), count);
            if (m_SingleIteration) {
                const auto &s = result->samples[std::min(static_cast<std::size_t>(std::max(0,m_Iteration)),result->samples.size()-1)];
                ImGui::Text("Method: %s | Stop: %s", s.fitMethod.c_str(), s.stopReason.c_str());
            }
            const std::size_t pointCount = result->voltage.size();
			const std::size_t sampleCount = result->noiseSeed ? result->samples.size() : std::min(result->samples.size(), result->noisyCurrent.size() / pointCount);
			if (sampleCount == 0)
				continue;
			m_Iteration = std::clamp(m_Iteration, 0, static_cast<int>(sampleCount - 1));
			if (m_SingleIteration)
			{
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::TextUnformatted("Iteration"); ImGui::SameLine();
                ImGui::PushID(static_cast<int>(ref.project));
                ImGui::PushID(static_cast<int>(ref.characteristic));
                ImGui::SliderInt("##iteration", &m_Iteration, 0, static_cast<int>(sampleCount - 1));
                ImGui::PopID(); ImGui::PopID();
			}

			const std::string title = record->info.name + "###mc_curves_" + std::to_string(ref.project) + "_" + std::to_string(ref.characteristic);
			if (ImPlot::BeginPlot(title.c_str(), ImVec2(-1.0f, std::max(360.0f, ImGui::GetContentRegionAvail().y - 20.0f)), ImPlotFlags_NoTitle))
			{
				ImPlot::SetupAxes("Voltage (V)", "Current");
				if (m_SemiLog)
					ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
				if (m_ShowIdeal && result->idealCurrent.size() >= pointCount)
				{
					ImPlotSpec spec;
					spec.LineColor = ImVec4(1.0f, 0.88f, 0.05f, 1.0f);
					if (previous) spec.LineColor.w *= 0.45f;
					spec.LineWeight = 2.5f;
					PlotCurrent("Ideal", result->voltage, result->idealCurrent.data(), pointCount, spec, m_SemiLog);
				}

				const std::size_t first = m_SingleIteration ? static_cast<std::size_t>(m_Iteration) : 0;
				const std::size_t last = m_SingleIteration ? first + 1 : sampleCount;
				for (std::size_t sampleIndex = first; sampleIndex < last; ++sampleIndex)
				{
                    const bool rejected = !result->samples[sampleIndex].converged;
                    if (rejected && !m_ShowRejected) continue;
					if (m_ShowNoisy)
					{
						ImPlotSpec spec;
						spec.LineColor = ImVec4(1.0, 0.0f, 0.0f, rejected ? 0.06f : m_SingleIteration ? 0.9f : 0.18f);
						if (previous) spec.LineColor.w *= 0.45f;
						spec.LineWeight = 2.0f;
						spec.Flags = ImPlotItemFlags_NoLegend;
						const std::string label = "##mc_noisy_" + std::to_string(sampleIndex);
                        if(sampleIndex<result->noisyCurrent.size()/pointCount)
                            PlotCurrent(label.c_str(),result->voltage,result->noisyCurrent.data()+sampleIndex*pointCount,pointCount,spec,m_SemiLog);
                        else {
                            const auto curve=JFMMonteCarloNoisyCurve(*result,sampleIndex);
                            PlotCurrent(label.c_str(),result->voltage,curve.data(),curve.size(),spec,m_SemiLog);
                        }
					}
					if (m_ShowFitted)
					{
						std::vector<double> fitted = FitCurveForSample(*result, result->samples[sampleIndex], record->info.monteCarlo ? record->info.monteCarlo->modelId : "SD-RsRsh", *record->info.temperatureK);
						ImPlotSpec spec;
						spec.LineColor = ImVec4(0.10f, 0.56f, 1.0f, rejected ? 0.08f : m_SingleIteration ? 1.0f : 0.28f);
						if (previous) spec.LineColor.w *= 0.45f;
						spec.LineWeight = 2.5;
						spec.Flags = ImPlotItemFlags_NoLegend;
						const std::string label = "##mc_fit_" + std::to_string(sampleIndex);
						PlotCurrent(label.c_str(), result->voltage, fitted.data(), std::min(pointCount, fitted.size()), spec, m_SemiLog);
					}
				}
				ImPlot::EndPlot();
			}
		}
		if (m_Plots.empty())
			ImGui::TextDisabled("No MC curves");
	}
    int MonteCarloCurvesPanel::WindowFlags() const
    {
        return ImGuiWindowFlags_HorizontalScrollbar;
    }
} // namespace SST::Prototypes::JFM
