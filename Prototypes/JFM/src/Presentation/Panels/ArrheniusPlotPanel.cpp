#include "Core/dspch.hpp"

#include "Presentation/Panels/ArrheniusPlotPanel.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

#include <imgui.h>
#include <implot.h>

namespace SST::Prototypes::JFM
{
	namespace
	{
		constexpr const char *QuantityLabels[] = {
			"A(T) [T]",
			"1/A(T) [T]",
			"A*ln(I0) [1000/T]",
			"ln(I0) [1000/T]",
			"Rs(T) [T]",
			"Rsh(T) [T]",
			"alpha(T) [T]",
			"Rsh2(T) [T]"};

		JFMArrheniusQuantity ToQuantity(int index)
		{
			return static_cast<JFMArrheniusQuantity>(std::clamp(index, 0, 7));
		}

		std::optional<ArrheniusPlotPanel::FitLine> FitLinear(const std::vector<JFMArrheniusPoint> &points, int lower, int upper, std::size_t fitIndex, const char *modelLabel)
		{
			lower = std::clamp(lower, 0, static_cast<int>(points.size()) - 1);
			upper = std::clamp(upper, lower, static_cast<int>(points.size()) - 1);
			const int count = upper - lower + 1;
			if (count < 2)
				return std::nullopt;

			double sx = 0.0;
			double sy = 0.0;
			double sxx = 0.0;
			double sxy = 0.0;
			for (int index = lower; index <= upper; ++index)
			{
				sx += points[index].x;
				sy += points[index].y;
				sxx += points[index].x * points[index].x;
				sxy += points[index].x * points[index].y;
			}
			const double denominator = count * sxx - sx * sx;
			if (denominator == 0.0)
				return std::nullopt;

			ArrheniusPlotPanel::FitLine fit;
			fit.name = std::string(modelLabel) + " " + std::to_string(fitIndex + 1);
			fit.slope = (count * sxy - sx * sy) / denominator;
			fit.intercept = (sy - fit.slope * sx) / count;
			constexpr double kEvPerK = 8.617333262e-5;
			fit.eaEv = -fit.slope * kEvPerK * 1000.0;
			fit.egEv = 2.0 * fit.eaEv;
			return fit;
		}
	}

	ArrheniusPlotPanel::ArrheniusPlotPanel(Ref<JFMWorkspaceStore> store)
		: IPanel("Arrhenius Plots", true), m_Store(std::move(store))
	{
	}

	Ref<IPanel> ArrheniusPlotPanel::Clone() const
	{
		return CreateRef<ArrheniusPlotPanel>(*this);
	}

	std::vector<JFMCharacteristicRecord> ArrheniusPlotPanel::RecordsForSeries(ProjectHandle project, const JFMSeries &series) const
	{
		return m_Store == nullptr ? std::vector<JFMCharacteristicRecord>{} : m_Store->GetSeriesCharacteristicRecords(project, series.id);
	}

	std::vector<JFMCharacteristicRecord> ArrheniusPlotPanel::RecordsForGroup(ProjectHandle projectHandle, const JFMProject &snapshot, const JFMGroup &group) const
	{
		std::set<std::string> groups{group.id};
		bool changed = true;
		while (changed)
		{
			changed = false;
			for (const JFMGroup &candidate : snapshot.groups)
			{
				if (groups.find(candidate.parentId) != groups.end() && groups.insert(candidate.id).second)
					changed = true;
			}
		}
		std::vector<JFMCharacteristicRecord> records;
		for (const JFMSeries &series : snapshot.series)
		{
			if (groups.find(series.groupId) == groups.end())
				continue;
			std::vector<JFMCharacteristicRecord> part = RecordsForSeries(projectHandle, series);
			records.insert(records.end(), part.begin(), part.end());
		}
		return records;
	}

	void ArrheniusPlotPanel::RenderPlot(const std::string &key, const std::string &title, const std::vector<JFMCharacteristicRecord> &records)
	{
		std::vector<JFMArrheniusPoint> points = BuildJFMArrheniusPoints(records, ToQuantity(m_Quantity), m_PreferFit);
		if (points.empty())
			return;

		PlotState &state = m_Plots[key + ":" + std::to_string(m_Quantity)];
		if (state.upper <= state.lower || state.upper >= static_cast<int>(points.size()))
		{
			state.lower = 0;
			state.upper = static_cast<int>(points.size()) - 1;
		}

		if (!ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::SetNextItemWidth(180.0f);
		ImGui::SliderInt(("Lower##" + key).c_str(), &state.lower, 0, static_cast<int>(points.size()) - 1);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(180.0f);
		ImGui::SliderInt(("Upper##" + key).c_str(), &state.upper, 0, static_cast<int>(points.size()) - 1);
		if (state.upper < state.lower)
			state.upper = state.lower;
		ImGui::SameLine();
		if (ImGui::Button(("Add fit##" + key).c_str()))
		{
			const char *fitLabels[] = {"Linear", "E_A", "E_g"};
			if (auto fit = FitLinear(points, state.lower, state.upper, state.fits.size(), fitLabels[std::clamp(m_FitModel, 0, 2)]))
				state.fits.push_back(*fit);
		}

		std::vector<double> x;
		std::vector<double> y;
		x.reserve(points.size());
		y.reserve(points.size());
		for (const JFMArrheniusPoint &point : points)
		{
			x.push_back(point.x);
			y.push_back(point.y);
		}

		if (ImPlot::BeginPlot(("##arrhenius_" + key).c_str(), ImVec2(-1.0f, 280.0f)))
		{
			ImPlot::SetupAxes(JFMArrheniusUsesInvTemperature(ToQuantity(m_Quantity)) ? "1000/T" : "T (K)", JFMArrheniusQuantityName(ToQuantity(m_Quantity)));
			ImPlot::PlotScatter("data", x.data(), y.data(), static_cast<int>(x.size()));
			for (const FitLine &fit : state.fits)
			{
				const double xx[2] = {x.front(), x.back()};
				const double yy[2] = {fit.intercept + fit.slope * xx[0], fit.intercept + fit.slope * xx[1]};
				ImPlot::PlotLine(fit.name.c_str(), xx, yy, 2);
			}
			ImPlot::EndPlot();
		}

		for (const FitLine &fit : state.fits)
			ImGui::Text("%s: y = %.6g*x + %.6g, E_A = %.6g eV, E_g = %.6g eV", fit.name.c_str(), fit.slope, fit.intercept, fit.eaEv, fit.egEv);
	}

	void ArrheniusPlotPanel::Render()
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

		ImGui::SetNextItemWidth(120.0f);
		ImGui::Combo("Source", &m_SourceMode, "Series\0Groups\0");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		ImGui::Combo("Plot", &m_Quantity, QuantityLabels, IM_ARRAYSIZE(QuantityLabels));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(130.0f);
		ImGui::Combo("Fit model", &m_FitModel, "Linear\0E_A\0E_g\0");
		ImGui::SameLine();
		ImGui::Checkbox("Prefer fit", &m_PreferFit);

		if (m_Store != nullptr)
		{
			const ProjectHandle projectHandle = m_Store->GetActiveProjectHandle();
			const JFMProject snapshot = m_Store->GetProjectSnapshot(projectHandle);
			if (m_SourceMode == 0)
			{
				for (const JFMSeries &series : snapshot.series)
					RenderPlot("series:" + series.id, series.name, RecordsForSeries(projectHandle, series));
			}
			else
			{
				for (const JFMGroup &group : snapshot.groups)
					RenderPlot("group:" + group.id, group.name, RecordsForGroup(projectHandle, snapshot, group));
			}
		}

		ImGui::End();
		SetVisible(visible);
	}
} // namespace SST::Prototypes::JFM
