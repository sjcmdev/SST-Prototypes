#include "Core/dspch.hpp"

#include "Presentation/JFMFitMonteCarloWidgets.hpp"

#include "Analysis/JFMFitEngine.hpp"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <implot.h>

namespace SST::Prototypes::JFM
{
	bool RenderJFMFitMonteCarloSettings(JFMFitMonteCarloSettings &settings, std::string_view modelId, const char *id)
	{
		bool changed = false;
		ImGui::PushID(id);
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputInt("Fit iterations", &settings.fitMaxIterations, 100, 1000);
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputDouble("Reduced chi2 target", &settings.fitReducedChiSquaredTolerance, 0.1, 1.0, "%.6g");
		ImGui::SeparatorText("Monte Carlo");
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputInt("Samples", &settings.monteCarloSamples, 64, 512);
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputInt("Simplex iterations", &settings.monteCarloMaxIterations, 100, 1000);
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputDouble("Noise (%)", &settings.monteCarloNoisePercent, 0.1, 1.0, "%.6g");
		ImGui::SetNextItemWidth(130.0f);
		changed |= ImGui::InputDouble("Start perturbation (%)", &settings.monteCarloStartPerturbationPercent, 0.5, 5.0, "%.6g");
		int seed = static_cast<int>(settings.monteCarloSeed);
		ImGui::SetNextItemWidth(130.0f);
		if (ImGui::InputInt("Seed", &seed))
		{
			settings.monteCarloSeed = static_cast<std::uint32_t>(std::max(seed, 0));
			changed = true;
		}
		changed |= ImGui::Checkbox("Prefer CUDA", &settings.preferGpu);

		const auto &names = JFMModelParameterNames(modelId);
		if (ImGui::BeginTable("##parameters", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
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

	void RenderJFMCovariancePlot(const JFMMonteCarloResult &result, int &xParameter, int &yParameter, const char *id, float height)
	{
		if (result.parameterNames.size() < 2 || result.samples.size() < 3)
		{
			ImGui::TextDisabled("Monte Carlo covariance unavailable");
			return;
		}
		const int count = static_cast<int>(result.parameterNames.size());
		xParameter = std::clamp(xParameter, 0, count - 1);
		yParameter = std::clamp(yParameter, 0, count - 1);
		if (xParameter == yParameter)
			yParameter = (xParameter + 1) % count;

		ImGui::PushID(id);
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::BeginCombo("X", result.parameterNames[xParameter].c_str()))
		{
			for (int index = 0; index < count; ++index)
				if (ImGui::Selectable(result.parameterNames[index].c_str(), index == xParameter))
					xParameter = index;
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::BeginCombo("Y", result.parameterNames[yParameter].c_str()))
		{
			for (int index = 0; index < count; ++index)
				if (ImGui::Selectable(result.parameterNames[index].c_str(), index == yParameter))
					yParameter = index;
			ImGui::EndCombo();
		}
		if (xParameter == yParameter)
			yParameter = (xParameter + 1) % count;

		std::vector<double> x;
		std::vector<double> y;
		x.reserve(result.samples.size());
		y.reserve(result.samples.size());
		for (const JFMMonteCarloSample &sample : result.samples)
		{
			if (!sample.converged || xParameter >= static_cast<int>(sample.parameters.size()) || yParameter >= static_cast<int>(sample.parameters.size()))
				continue;
			const double xv = sample.parameters[xParameter];
			const double yv = sample.parameters[yParameter];
			if (std::isfinite(xv) && std::isfinite(yv))
			{
				x.push_back(xv);
				y.push_back(yv);
			}
		}
		if (x.size() < 3)
		{
			ImGui::TextDisabled("Too few converged samples");
			ImGui::PopID();
			return;
		}

		double meanX = 0.0;
		double meanY = 0.0;
		for (std::size_t index = 0; index < x.size(); ++index)
		{
			meanX += x[index];
			meanY += y[index];
		}
		meanX /= static_cast<double>(x.size());
		meanY /= static_cast<double>(y.size());
		double covXX = 0.0;
		double covXY = 0.0;
		double covYY = 0.0;
		for (std::size_t index = 0; index < x.size(); ++index)
		{
			const double dx = x[index] - meanX;
			const double dy = y[index] - meanY;
			covXX += dx * dx;
			covXY += dx * dy;
			covYY += dy * dy;
		}
		const double denominator = static_cast<double>(x.size() - 1);
		covXX /= denominator;
		covXY /= denominator;
		covYY /= denominator;

		const double trace = covXX + covYY;
		const double root = std::sqrt(std::max(0.0, (covXX - covYY) * (covXX - covYY) + 4.0 * covXY * covXY));
		const double lambda1 = 0.5 * (trace + root);
		const double lambda2 = 0.5 * (trace - root);
		const double angle = 0.5 * std::atan2(2.0 * covXY, covXX - covYY);
		const double cosAngle = std::cos(angle);
		const double sinAngle = std::sin(angle);
		const double levels[] = {2.30, 6.18, 11.83};
		const ImVec4 colors[] = {{0.18f, 0.85f, 0.95f, 1.0f}, {1.0f, 0.82f, 0.12f, 1.0f}, {1.0f, 0.30f, 0.16f, 1.0f}};

		if (ImPlot::BeginPlot("Covariance", ImVec2(-1.0f, height)))
		{
			ImPlot::SetupAxes(result.parameterNames[xParameter].c_str(), result.parameterNames[yParameter].c_str());
			ImPlotSpec points;
			points.Marker = ImPlotMarker_Circle;
			points.MarkerSize = 3.0f;
			points.LineColor = ImVec4(0.72f, 0.72f, 0.72f, 0.7f);
			points.MarkerFillColor = points.LineColor;
			ImPlot::PlotScatter("Samples", x.data(), y.data(), static_cast<int>(x.size()), points);
			for (int level = 2; level >= 0; --level)
			{
				std::array<double, 129> ellipseX{};
				std::array<double, 129> ellipseY{};
				const double radius1 = std::sqrt(std::max(0.0, lambda1 * levels[level]));
				const double radius2 = std::sqrt(std::max(0.0, lambda2 * levels[level]));
				for (std::size_t point = 0; point < ellipseX.size(); ++point)
				{
					const double phase = 2.0 * 3.14159265358979323846 * static_cast<double>(point) / static_cast<double>(ellipseX.size() - 1);
					const double ex = radius1 * std::cos(phase);
					const double ey = radius2 * std::sin(phase);
					ellipseX[point] = meanX + ex * cosAngle - ey * sinAngle;
					ellipseY[point] = meanY + ex * sinAngle + ey * cosAngle;
				}
				ImPlotSpec line;
				line.LineColor = colors[level];
				line.LineWeight = 2.0f;
				const std::string label = std::to_string(level + 1) + " sigma";
				ImPlot::PlotLine(label.c_str(), ellipseX.data(), ellipseY.data(), static_cast<int>(ellipseX.size()), line);
			}
			ImPlot::EndPlot();
		}
		ImGui::PopID();
	}
} // namespace SST::Prototypes::JFM
