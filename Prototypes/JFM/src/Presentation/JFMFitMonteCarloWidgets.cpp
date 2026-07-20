#include "Core/dspch.hpp"

#include "Presentation/JFMFitMonteCarloWidgets.hpp"

#include "Analysis/JFMFitEngine.hpp"
#include "Analysis/JFMMonteCarloStatistics.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <imgui.h>
#include <implot.h>
#include <optional>

namespace SST::Prototypes::JFM
{
	bool RenderJFMFitMonteCarloSettings(JFMFitMonteCarloSettings &settings, std::string_view modelId, const char *id, bool showParameterBounds)
	{
		bool changed = false;
		ImGui::PushID(id);
		int simplexIterations = std::max(settings.fitMaxIterations, settings.monteCarloMaxIterations);
		ImGui::SetNextItemWidth(130.0f);
		if (ImGui::InputInt("Simplex iterations", &simplexIterations, 100, 1000))
		{
			settings.fitMaxIterations = simplexIterations;
			settings.monteCarloMaxIterations = simplexIterations;
			changed = true;
		}
		ImGui::SeparatorText("Monte Carlo");
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputInt("N", &settings.monteCarloSamples, 64, 512);
		changed |= ImGui::Checkbox("Save samples", &settings.saveSamples);
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputDouble("Sigma (%)", &settings.monteCarloNoisePercent, 0.1, 1.0, "%.6g");
		int seed = static_cast<int>(settings.monteCarloSeed);
		ImGui::SetNextItemWidth(130.0f);
		if (ImGui::InputInt("Seed", &seed))
		{
			settings.monteCarloSeed = static_cast<std::uint32_t>(std::max(seed, 0));
			changed = true;
		}
		const char *backend = settings.preferGpu ? "CUDA" : "CPU";
		ImGui::SetNextItemWidth(130.0f);
		if (ImGui::BeginCombo("Backend", backend))
		{
			if (ImGui::Selectable("CUDA", settings.preferGpu))
			{
				settings.preferGpu = true;
				changed = true;
			}
			if (ImGui::Selectable("CPU", !settings.preferGpu))
			{
				settings.preferGpu = false;
				changed = true;
			}
			ImGui::EndCombo();
		}

		const auto &names = JFMModelParameterNames(modelId);
		if (showParameterBounds && ImGui::BeginTable("##parameters", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Parameter");
			ImGui::TableSetupColumn("Free", ImGuiTableColumnFlags_WidthFixed, 44.0f);
			ImGui::TableSetupColumn("Minimum");
			ImGui::TableSetupColumn("Maximum");
			ImGui::TableHeadersRow();
			for (std::string_view nameView : names)
			{
				const std::string name(nameView);
				JFMParameterSolverSetting &parameter = settings.parameters[name];
				ImGui::PushID(name.c_str());
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(name.c_str());
				ImGui::TableSetColumnIndex(1);
				changed |= ImGui::Checkbox("##free", &parameter.monteCarloFree);
				ImGui::TableSetColumnIndex(2);
				ImGui::SetNextItemWidth(-1.0f);
				changed |= ImGui::InputDouble("##min", &parameter.minimum, 0.0, 0.0, "%.6g");
				ImGui::TableSetColumnIndex(3);
				ImGui::SetNextItemWidth(-1.0f);
				changed |= ImGui::InputDouble("##max", &parameter.maximum, 0.0, 0.0, "%.6g");
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
		return changed;
	}

	namespace
	{
		std::vector<JFMMonteCarloParameterPair> DefaultPairsForModel(std::string_view modelId)
		{
			if (modelId == "SD-RsRsh-alpha-Rsh2")
				return {{"A", "I0"}, {"Rs", "Rsh"}, {"Rsh", "Rsh2"}, {"alpha", "Rsh2"}};
			return {{"A", "I0"}, {"Rs", "Rsh"}};
		}

		bool IsKnownDefaultPairs(const std::vector<JFMMonteCarloParameterPair> &pairs)
		{
			return pairs == std::vector<JFMMonteCarloParameterPair>{{"A", "I0"}, {"Rs", "Rsh"}}
				|| pairs == std::vector<JFMMonteCarloParameterPair>{{"A", "I0"}, {"Rs", "Rsh"}, {"Rsh", "Rsh2"}, {"alpha", "Rsh2"}}
				|| pairs == std::vector<JFMMonteCarloParameterPair>{{"i0", "a"}, {"RsH", "Rs"}};
		}
	} // namespace

	bool RenderJFMMonteCarloScatterDefaults(JFMFitMonteCarloSettings &settings, const char *id)
	{
		return RenderJFMMonteCarloScatterDefaults(settings, "SD-RsRsh", id);
	}

	bool RenderJFMMonteCarloScatterDefaults(JFMFitMonteCarloSettings &settings, std::string_view modelId, const char *id)
	{
		bool changed = false;
		static constexpr const char *labels[] = {"<= 1 sigma", "1-2 sigma", "2-3 sigma"};
		const auto &parameters = JFMModelParameterNames(modelId);
		ImGui::PushID(id);
		if (settings.defaultParameterPairs.empty() || IsKnownDefaultPairs(settings.defaultParameterPairs))
			settings.defaultParameterPairs = DefaultPairsForModel(modelId);
		for (std::size_t index = 0; index < settings.showSigmaClasses.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			changed |= ImGui::Checkbox(labels[index], &settings.showSigmaClasses[index]);
			ImGui::SameLine();
			changed |= ImGui::ColorEdit4("##color", settings.sigmaColors[index].data(), ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine();
			ImGui::PopID();
		}
		ImGui::SetNextItemWidth(170.0f);
		changed |= ImGui::SliderFloat("Zoom-out", &settings.scatterZoomOut, 0.0f, 1.0f, "%.2f");
		ImGui::SeparatorText("Default parameter pairs");
		if (ImGui::Button("Add pair"))
		{
			settings.defaultParameterPairs.push_back({"A", "I0"});
			changed = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Model defaults"))
		{
			settings.defaultParameterPairs = DefaultPairsForModel(modelId);
			changed = true;
		}
		std::optional<std::size_t> removePair;
		for (std::size_t index = 0; index < settings.defaultParameterPairs.size(); ++index)
		{
			JFMMonteCarloParameterPair &pair = settings.defaultParameterPairs[index];
			ImGui::PushID(static_cast<int>(index));
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::BeginCombo("##x", pair.x.c_str()))
			{
				for (std::string_view parameter : parameters)
					if (ImGui::Selectable(std::string(parameter).c_str(), pair.x == parameter))
					{
						pair.x = parameter;
						changed = true;
					}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted("vs");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::BeginCombo("##y", pair.y.c_str()))
			{
				for (std::string_view parameter : parameters)
					if (ImGui::Selectable(std::string(parameter).c_str(), pair.y == parameter))
					{
						pair.y = parameter;
						changed = true;
					}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
				removePair = index;
			ImGui::PopID();
		}
		if (removePair)
		{
			settings.defaultParameterPairs.erase(settings.defaultParameterPairs.begin() + static_cast<std::ptrdiff_t>(*removePair));
			changed = true;
		}
		ImGui::PopID();
		return changed;
	}

	bool RenderJFMCovariancePlot(const JFMMonteCarloResult &result, JFMMonteCarloScatterPlotOptions &options, const char *id, float height)
	{
		bool changed = false;
		if (result.parameterNames.size() < 2 || result.samples.size() < 3)
		{
			ImGui::TextDisabled("Monte Carlo covariance unavailable");
			return false;
		}
		const int count = static_cast<int>(result.parameterNames.size());
		options.xParameter = std::clamp(options.xParameter, 0, count - 1);
		options.yParameter = std::clamp(options.yParameter, 0, count - 1);
		if (options.xParameter == options.yParameter)
			options.yParameter = (options.xParameter + 1) % count;

		ImGui::PushID(id);
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::BeginCombo("X", result.parameterNames[options.xParameter].c_str()))
		{
			for (int index = 0; index < count; ++index)
				if (ImGui::Selectable(result.parameterNames[index].c_str(), index == options.xParameter))
				{
					options.xParameter = index;
					changed = true;
				}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::BeginCombo("Y", result.parameterNames[options.yParameter].c_str()))
		{
			for (int index = 0; index < count; ++index)
				if (ImGui::Selectable(result.parameterNames[index].c_str(), index == options.yParameter))
				{
					options.yParameter = index;
					changed = true;
				}
			ImGui::EndCombo();
		}
		if (options.xParameter == options.yParameter)
		{
			options.yParameter = (options.xParameter + 1) % count;
			changed = true;
		}
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Auto-fit", &options.autoFit);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		changed |= ImGui::SliderFloat("Zoom-out", &options.zoomOut, 0.0f, 1.0f, "%.2f");

		static constexpr const char *classLabels[] = {"<= 1 sigma", "1-2 sigma", "2-3 sigma"};
		for (std::size_t index = 0; index < options.showSigmaClasses.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			changed |= ImGui::Checkbox(classLabels[index], &options.showSigmaClasses[index]);
			ImGui::SameLine();
			changed |= ImGui::ColorEdit4("##color", options.sigmaColors[index].data(), ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine();
			ImGui::PopID();
		}
		ImGui::NewLine();

		const auto statistics = CalculateJFMMonteCarloScatterStatistics(result, options.xParameter, options.yParameter);
		if (!statistics)
		{
			ImGui::TextDisabled("Too few converged samples");
			ImGui::PopID();
			return changed;
		}

		std::array<std::vector<double>, 3> x;
		std::array<std::vector<double>, 3> y;
		bool hasVisiblePoint = false;
		double minimumX = 0.0;
		double maximumX = 0.0;
		double minimumY = 0.0;
		double maximumY = 0.0;
		for (const JFMMonteCarloScatterPoint &point : statistics->points)
		{
			const std::size_t displayClass = std::min<std::size_t>(point.sigmaClass, x.size() - 1);
			x[displayClass].push_back(point.x);
			y[displayClass].push_back(point.y);
			if (!hasVisiblePoint)
			{
				minimumX = maximumX = point.x;
				minimumY = maximumY = point.y;
				hasVisiblePoint = true;
			}
			else
			{
				minimumX = std::min(minimumX, point.x);
				maximumX = std::max(maximumX, point.x);
				minimumY = std::min(minimumY, point.y);
				maximumY = std::max(maximumY, point.y);
			}
		}
		if (!hasVisiblePoint)
		{
			ImGui::TextDisabled("No visible sigma samples");
			ImGui::PopID();
			return changed;
		}

		if (ImPlot::BeginPlot("##Covariance", ImVec2(-1.0f, height), ImPlotFlags_NoTitle))
		{
			ImPlot::SetupAxes(result.parameterNames[options.xParameter].c_str(), result.parameterNames[options.yParameter].c_str());
			if (options.autoFit)
			{
				const double spanX = std::max(maximumX - minimumX, std::max(std::abs(maximumX), 1.0) * 1.0e-9);
				const double spanY = std::max(maximumY - minimumY, std::max(std::abs(maximumY), 1.0) * 1.0e-9);
				ImPlot::SetupAxisLimits(ImAxis_X1, minimumX - spanX * options.zoomOut, maximumX + spanX * options.zoomOut, ImGuiCond_Always);
				ImPlot::SetupAxisLimits(ImAxis_Y1, minimumY - spanY * options.zoomOut, maximumY + spanY * options.zoomOut, ImGuiCond_Always);
			}
			for (int sigmaClass = static_cast<int>(x.size()) - 1; sigmaClass >= 0; --sigmaClass)
			{
				if (!options.showSigmaClasses[sigmaClass] || x[sigmaClass].empty())
					continue;
				ImPlotSpec points;
				points.Marker = ImPlotMarker_Circle;
				points.MarkerSize = 3.0f;
				points.LineColor = ImVec4(
					options.sigmaColors[sigmaClass][0], options.sigmaColors[sigmaClass][1],
					options.sigmaColors[sigmaClass][2], options.sigmaColors[sigmaClass][3]);
				points.MarkerFillColor = points.LineColor;
				ImPlot::PlotScatter(classLabels[sigmaClass], x[sigmaClass].data(), y[sigmaClass].data(), static_cast<int>(x[sigmaClass].size()), points);
			}
			ImPlot::EndPlot();
		}
		ImGui::PopID();
		return changed;
	}
} // namespace SST::Prototypes::JFM
