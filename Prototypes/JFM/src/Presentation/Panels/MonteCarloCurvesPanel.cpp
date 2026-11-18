#include "Core/dspch.hpp"

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
		: IPanel(std::move(title), visibleByDefault),
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

	Ref<IPanel> MonteCarloCurvesPanel::Clone() const
	{
		return CreateRef<MonteCarloCurvesPanel>(m_Store, m_EventBus, GetTitle(), IsVisible());
	}

	void MonteCarloCurvesPanel::AddPlot(JFMCharacteristicRef ref)
	{
		if (ref.project == InvalidProjectHandle || ref.characteristic == InvalidCharacteristicId)
			return;
		if (std::find(m_Plots.begin(), m_Plots.end(), ref) == m_Plots.end())
			m_Plots.push_back(ref);
	}

	void MonteCarloCurvesPanel::Render()
	{
		if (!IsVisible())
			return;
		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::End();
			SetVisible(visible);
			return;
		}
		if (m_Store == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");
			ImGui::End();
			SetVisible(visible);
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
		ImGui::SameLine();
		ImGui::Checkbox("Ideal", &m_ShowIdeal);
		ImGui::SameLine();
		ImGui::Checkbox("Noisy", &m_ShowNoisy);
		ImGui::SameLine();
		ImGui::Checkbox("Fitted", &m_ShowFitted);
		ImGui::SameLine();
		ImGui::Checkbox("Semi-log", &m_SemiLog);
		ImGui::SameLine();
		ImGui::Checkbox("Single iteration", &m_SingleIteration);

		for (JFMCharacteristicRef ref : m_Plots)
		{
			const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult(ref);
			const auto record = m_Store->FindCharacteristicRecord(ref);
			if (result == nullptr || !record || !record->info.temperatureK || result->voltage.empty() || result->samples.empty())
				continue;
			const std::size_t pointCount = result->voltage.size();
			const std::size_t sampleCount = std::min(result->samples.size(), result->noisyCurrent.size() / pointCount);
			if (sampleCount == 0)
				continue;
			m_Iteration = std::clamp(m_Iteration, 0, static_cast<int>(sampleCount - 1));
			if (m_SingleIteration)
			{
				ImGui::SetNextItemWidth(1000.0f);
				ImGui::SliderInt("Iteration", &m_Iteration, 0, static_cast<int>(sampleCount - 1));
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
					spec.LineWeight = 2.5f;
					PlotCurrent("Ideal", result->voltage, result->idealCurrent.data(), pointCount, spec, m_SemiLog);
				}

				const std::size_t first = m_SingleIteration ? static_cast<std::size_t>(m_Iteration) : 0;
				const std::size_t last = m_SingleIteration ? first + 1 : sampleCount;
				for (std::size_t sampleIndex = first; sampleIndex < last; ++sampleIndex)
				{
					if (m_ShowNoisy)
					{
						ImPlotSpec spec;
						spec.LineColor = ImVec4(1.0, 0.0f, 0.0f, m_SingleIteration ? 0.9f : 0.18f);
						spec.LineWeight = 2.0f;
						spec.Flags = ImPlotItemFlags_NoLegend;
						const std::string label = "##mc_noisy_" + std::to_string(sampleIndex);
						PlotCurrent(label.c_str(), result->voltage, result->noisyCurrent.data() + sampleIndex * pointCount, pointCount, spec, m_SemiLog);
					}
					if (m_ShowFitted)
					{
						std::vector<double> fitted = FitCurveForSample(*result, result->samples[sampleIndex], record->info.monteCarlo ? record->info.monteCarlo->modelId : "SD-RsRsh", *record->info.temperatureK);
						ImPlotSpec spec;
						spec.LineColor = ImVec4(0.10f, 0.56f, 1.0f, m_SingleIteration ? 1.0f : 0.28f);
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
		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
