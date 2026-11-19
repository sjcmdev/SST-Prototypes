#include "Core/dspch.hpp"

#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include "Presentation/JFMPlotWidgets.hpp"

#include "Analysis/JFMFitEngine.hpp"
#include "Analysis/JFMMonteCarloStatistics.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <imgui.h>
#include <sst/ui/Widgets/LabelledControls.hpp>
#include <sst/ui/Widgets/ParameterBoundsRow.hpp>
#include <implot.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <optional>

namespace SST::Prototypes::JFM
{
    bool RenderJFMFitMethod(JFMFitMonteCarloSettings &settings, const JFMCharacteristicInfo* characteristic)
    {
        bool changed = false;
        if (sst::ui::widgets::BeginCombo("Fit method", settings.fitMethod == "nm" ? "NM" : "LM")) {
            for (const auto *method : {"lm", "nm"})
                if (ImGui::Selectable(method[0] == 'l' ? "LM" : "NM", settings.fitMethod == method)) {
                    settings.fitMethod = method; changed = true;
                }
            sst::ui::widgets::EndCombo();
        }
        if (sst::ui::widgets::BeginCombo("Objective", settings.objectiveMode.c_str(), 0, "Auto: all fitted currents > 0 use log_legacy; signed fit-range data use isc_shift.\nisc_shift: constant measured -I(V=0), propagated sigma, tangent linear below k*sigma.\nsigned_sigma: (model-data)/(sigma_rel*abs(data)+sigma_abs).\nlog_legacy is a unit-dependent regression reference, not statistical chi squared.")) {
            for (const auto *mode : {"auto", "isc_shift", "signed_sigma", "log_legacy"})
                if (ImGui::Selectable(mode, settings.objectiveMode == mode)) { settings.objectiveMode = mode; changed = true; }
            sst::ui::widgets::EndCombo();
        }
        changed |= sst::ui::widgets::Double("Switch k (sigma)", &settings.objectiveSwitchK, .1f, "%.3g", "For isc_shift only. Positive threshold for the continuous log/linear transition. Ctrl+click for exact entry.");
        changed |= sst::ui::widgets::Double("Sigma rel (%)", &settings.monteCarloNoisePercent, .1f, "%.6g", "Current sigma = relative fraction * abs(current) + absolute floor; used by the fit and MC noise generator. Ctrl+click for exact entry.");
        bool automatic = settings.sigmaAbsolute == 0;
        if (sst::ui::widgets::Checkbox("Automatic sigma floor", &automatic, "For statistical modes: a data-derived floor, active even at zero relative noise. Enter a calibrated instrument floor when known.")) {
            settings.sigmaAbsolute = automatic ? 0 : 1e-6; changed = true;
        }
        if (!automatic) {
            int exponent = static_cast<int>(std::floor(std::log10(settings.sigmaAbsolute)));
            float mantissa = static_cast<float>(settings.sigmaAbsolute/std::pow(10.,exponent));
            bool floorChanged = sst::ui::widgets::SliderFloat(("Sigma abs mantissa " + JFMParameterLabel("I0", characteristic).substr(3)).c_str(), &mantissa, 1.f, 9.999f, "%.4g");
            float exponentSlider = static_cast<float>(exponent);
            floorChanged |= sst::ui::widgets::SliderFloat("Sigma abs exponent (10^)", &exponentSlider, -30.f, 10.f, "%.0f");
            exponent = static_cast<int>(std::round(exponentSlider));
            if (floorChanged) { settings.sigmaAbsolute = mantissa*std::pow(10.,std::clamp(exponent,-30,10)); changed = true; }
        }
        return changed;
    }
	bool RenderJFMFitMonteCarloSettings(JFMFitMonteCarloSettings &settings, std::string_view modelId, const char *id, bool showParameterBounds, const JFMCharacteristicInfo* characteristic)
	{
		bool changed = false;
		ImGui::PushID(id);
        changed |= RenderJFMFitMethod(settings, characteristic);
		int simplexIterations = std::max(settings.fitMaxIterations, settings.monteCarloMaxIterations);
		if (sst::ui::widgets::Int("Fit iterations", &simplexIterations))
		{
			settings.fitMaxIterations = simplexIterations;
			settings.monteCarloMaxIterations = simplexIterations;
			changed = true;
		}
		ImGui::SeparatorText("Monte Carlo");
		changed |= sst::ui::widgets::Int("N", &settings.monteCarloSamples);
		changed |= sst::ui::widgets::Checkbox("Save samples", &settings.saveSamples);
		int seed = static_cast<int>(settings.monteCarloSeed);
		if (sst::ui::widgets::Int("Seed", &seed))
		{
			settings.monteCarloSeed = static_cast<std::uint32_t>(std::max(seed, 0));
			changed = true;
		}
		const char *backend = settings.preferGpu ? "CUDA" : "CPU";
		if (sst::ui::widgets::BeginCombo("Backend", backend))
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
			sst::ui::widgets::EndCombo();
		}

		if (showParameterBounds) ImGui::TextWrapped("Bounds retain the fitted voltage/current (or current-density) column units.");
		const auto &names = JFMModelParameterNames(modelId);
		if (showParameterBounds && ImGui::BeginTable("##parameters", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings, ImVec2(0.0f, (names.size()+2)*ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ScrollbarSize+8.0f)))
		{
			ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, std::max(190.0f, 14.0f*ImGui::GetFontSize()));
			ImGui::TableSetupColumn("Free", ImGuiTableColumnFlags_WidthFixed, std::max(44.0f, ImGui::CalcTextSize("Free").x+2*ImGui::GetStyle().CellPadding.x));
			ImGui::TableSetupColumn("Minimum", ImGuiTableColumnFlags_WidthFixed, std::max(220.0f, 16.0f*ImGui::GetFontSize()));
			ImGui::TableSetupColumn("Maximum", ImGuiTableColumnFlags_WidthFixed, std::max(220.0f, 16.0f*ImGui::GetFontSize()));
			ImGui::TableHeadersRow();
			for (std::string_view nameView : names)
			{
				const std::string name(nameView);
				JFMParameterSolverSetting &parameter = settings.parameters[name];
				changed |= sst::ui::widgets::ParameterBoundsRow(name.c_str(),
					JFMParameterLabel(name, characteristic).c_str(),
					parameter.monteCarloFree, parameter.minimum, parameter.maximum, name != "A" && name != "alpha");
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
		return changed;
	}

	namespace
	{
		using sst::ui::widgets::SameLineIfFits;

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
		const auto &labels = JFMSigmaLabels;
		const auto &parameters = JFMModelParameterNames(modelId);
		ImGui::PushID(id);
		if (IsKnownDefaultPairs(settings.defaultParameterPairs))
			settings.defaultParameterPairs = DefaultPairsForModel(modelId);
		for (std::size_t index = 0; index < settings.showSigmaClasses.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			ImGui::TextUnformatted(labels[index]); ImGui::SameLine();
            changed |= ImGui::Checkbox("##visible", &settings.showSigmaClasses[index]);
			ImGui::SameLine();
			changed |= ImGui::ColorEdit4("##color", settings.sigmaColors[index].data(), ImGuiColorEditFlags_NoInputs);
			ImGui::PopID();
            if (index + 1 < labels.size())
                SameLineIfFits(ImGui::CalcTextSize(labels[index + 1]).x + 2 * ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().ItemSpacing.x);
		}
		changed |= sst::ui::widgets::SliderFloat("Zoom-out", &settings.scatterZoomOut, 0.0f, 1.0f, "%.2f");
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

	bool RenderJFMCovariancePlot(const JFMMonteCarloResult &result, JFMMonteCarloScatterPlotOptions &options, const char *id, float height, JFMPlotDocument &document, const JFMCharacteristicInfo *characteristic)
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
		const auto &classLabels = JFMSigmaLabels;
		if (ImGui::BeginChild("##scatter_controls", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() * 2.0f + 2.0f*ImGui::GetStyle().WindowPadding.y), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::TextUnformatted("X"); ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("##x", result.parameterNames[options.xParameter].c_str()))
			{
				for (int index = 0; index < count; ++index)
					if (ImGui::Selectable(result.parameterNames[index].c_str(), index == options.xParameter))
					{
						options.xParameter = index;
						changed = true;
					}
				ImGui::EndCombo();
			}
			SameLineIfFits(160.0f);
			ImGui::TextUnformatted("Y"); ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("##y", result.parameterNames[options.yParameter].c_str()))
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
			SameLineIfFits(170.0f);
			for (std::size_t index = 0; index < options.showSigmaClasses.size(); ++index)
			{
				ImGui::PushID(static_cast<int>(index));
				ImGui::TextUnformatted(classLabels[index]); ImGui::SameLine();
                changed |= ImGui::Checkbox("##visible", &options.showSigmaClasses[index]);
				ImGui::SameLine();
				changed |= ImGui::ColorEdit4("##color", options.sigmaColors[index].data(), ImGuiColorEditFlags_NoInputs);
				ImGui::PopID();
				SameLineIfFits(170.0f);
			}
			ImGui::TextUnformatted("Auto-fit"); ImGui::SameLine();
            changed |= ImGui::Checkbox("##auto_fit", &options.autoFit);
			SameLineIfFits(190.0f);
			ImGui::TextUnformatted("Zoom-out"); ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            changed |= ImGui::SliderFloat("##zoom_out", &options.zoomOut, 0.0f, 1.0f, "%.2f");
			SameLineIfFits(130.0f);
			ImGui::SetNextItemWidth(100.0f);
			static constexpr const char *markers[] = {"Circle", "Square", "Diamond", "Cross", "Plus", "Asterisk"};
			ImGui::TextUnformatted("Marker"); ImGui::SameLine();
            changed |= ImGui::Combo("##marker", &options.marker, markers, IM_ARRAYSIZE(markers));
		}
		ImGui::EndChild();

		const auto statistics = CalculateJFMMonteCarloScatterStatistics(result, options.xParameter, options.yParameter);
		if (!statistics)
		{
			ImGui::TextDisabled("Too few accepted samples");
			ImGui::PopID();
			return changed;
		}

        JFMPlotExportControls(document);
        const bool plotFont = BeginJFMPlotFrame(document);
        const auto xLabel = JFMParameterLabel(result.parameterNames[options.xParameter], characteristic);
        const auto yLabel = JFMParameterLabel(result.parameterNames[options.yParameter], characteristic);
        const auto bounds = PopulateJFMMonteCarloPlot(document, *statistics, options, xLabel, yLabel);
		if (plotFont && ImPlot::BeginPlot("##Covariance", ImVec2(-1.0f, std::min(height, std::max(1.0f, ImGui::GetContentRegionAvail().y))), ImPlotFlags_NoTitle))
		{
			ImPlot::SetupAxes(xLabel.c_str(), yLabel.c_str());
            ImPlot::SetupAxisFormat(ImAxis_X1, "%.6g");
            ImPlot::SetupAxisFormat(ImAxis_Y1, "%.6g");
            ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside);
            if (options.autoFit)
                ImPlot::SetupAxesLimits(bounds.minX, bounds.maxX, bounds.minY, bounds.maxY, ImGuiCond_Always);
            for (const auto &[plotId, label] : document.rendered) {
                auto entry = document.project.series(plotId); entry.id = label;
                sst::ui::plot::RenderSeries(entry);
            }
			const ImPlotRect limits = ImPlot::GetPlotLimits();
			options.xMinimum = limits.X.Min;
			options.xMaximum = limits.X.Max;
			options.yMinimum = limits.Y.Min;
			options.yMaximum = limits.Y.Max;
			options.hasAxisLimits = true;
			sst::ui::plot::PlotSpec figureSpec;
			figureSpec.x.label = xLabel;
			figureSpec.y.label = yLabel;
            figureSpec.legend.outside = true;
            figureSpec.x.labels.format = figureSpec.y.labels.format = sst::ui::plot::LabelFormat::Custom;
            figureSpec.x.labels.formatString = figureSpec.y.labels.formatString = "%.6g";
			EndJFMPlot(document, figureSpec);
		}
		if (plotFont) ImGui::PopFont();
		ImGui::PopID();
		return changed;
	}
} // namespace SST::Prototypes::JFM
