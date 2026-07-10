#include "Core/dspch.hpp"

#include "Presentation/Panels/RawPlotPanel.hpp"

#include <cmath>

#include <imgui.h>
#include <implot.h>

namespace SST::Prototypes::JFM
{


	const std::vector<double> &selectYValues(const JFMCharacteristic &characteristic)
	{
		if (!characteristic.data.currentDensity.empty())
			return characteristic.data.currentDensity;
		return characteristic.data.current;
	}

	std::vector<double> absolutePositive(const std::vector<double> &values, const std::vector<double> &x, std::vector<double> &filteredX)
	{
		std::vector<double> filteredY;
		const std::size_t count = std::min(values.size(), x.size());
		filteredX.reserve(count);
		filteredY.reserve(count);
		for (std::size_t index = 0; index < count; ++index)
		{
			const double value = std::abs(values[index]);
			if (value <= 0.0)
				continue;
			filteredX.push_back(x[index]);
			filteredY.push_back(value);
		}
		return filteredY;
	}

	std::vector<double> signedLog(const std::vector<double> &values)
	{
		std::vector<double> transformed;
		transformed.reserve(values.size());
		for (double value : values)
		{
			const double sign = value < 0.0 ? -1.0 : 1.0;
			transformed.push_back(sign * std::log10(1.0 + std::abs(value)));
		}
		return transformed;
	}
	

	RawPlotPanel::RawPlotPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Store(std::move(store))
	{
	}

	Ref<IPanel> RawPlotPanel::Clone() const
	{
		return CreateRef<RawPlotPanel>(*this);
	}

	void RawPlotPanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		if (!ImGui::Begin(GetTitle().c_str(), &visible))
		{
			ImGui::End();
			SetVisible(visible);
			return;
		}

		auto selected = m_Store != nullptr ? m_Store->GetSelectedCharacteristic() : std::optional<JFMCharacteristic>{};
		if (!selected)
		{
			ImGui::TextDisabled("No data");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		const std::vector<double> &x = selected->data.voltage;
		const std::vector<double> &rawY = selectYValues(*selected);
		const std::size_t count = std::min(x.size(), rawY.size());
		if (count == 0)
		{
			ImGui::TextDisabled("No raw arrays loaded");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		int scale = static_cast<int>(m_Scale);
		ImGui::RadioButton("Linear", &scale, static_cast<int>(PlotScale::Linear));
		ImGui::SameLine();
		ImGui::RadioButton("Semi-log", &scale, static_cast<int>(PlotScale::SemiLog));
		ImGui::SameLine();
		ImGui::RadioButton("Signed-log", &scale, static_cast<int>(PlotScale::SignedLog));
		m_Scale = static_cast<PlotScale>(scale);

		ImGui::TextUnformatted(selected->info.name.c_str());
		if (ImPlot::BeginPlot("##jfm_raw_plot", ImVec2(-1.0f, -1.0f)))
		{
			ImPlot::SetupAxes("Voltage", m_Scale == PlotScale::SignedLog ? "signed log current" : "Current");
			if (m_Scale == PlotScale::SemiLog)
				ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

			if (m_Scale == PlotScale::Linear)
			{
				ImPlot::PlotLine("raw", x.data(), rawY.data(), static_cast<int>(count));
			}
			else if (m_Scale == PlotScale::SemiLog)
			{
				std::vector<double> filteredX;
				std::vector<double> filteredY = absolutePositive(rawY, x, filteredX);
				if (!filteredX.empty())
					ImPlot::PlotLine("|raw|", filteredX.data(), filteredY.data(), static_cast<int>(filteredX.size()));
			}
			else
			{
				std::vector<double> transformed = signedLog(rawY);
				ImPlot::PlotLine("signed log", x.data(), transformed.data(), static_cast<int>(std::min(x.size(), transformed.size())));
			}
			ImPlot::EndPlot();
		}

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
