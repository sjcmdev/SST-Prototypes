#include "Core/dspch.hpp"

#include "Presentation/Panels/ArrheniusPlotPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <string>

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

		constexpr const char *FitModelLabels[] = {"TE-IR", "TE-SCR", "TE-ISC"};
		constexpr const char *DrawModeLabels[] = {"Scatter", "Line", "Line + scatter"};

		JFMArrheniusQuantity ToQuantity(int index)
		{
			return static_cast<JFMArrheniusQuantity>(std::clamp(index, 0, 7));
		}

		ImVec4 ToVec4(const std::array<float, 4> &color)
		{
			return {color[0], color[1], color[2], color[3]};
		}

		std::array<float, 4> DefaultColor(std::size_t index)
		{
			static constexpr std::array<std::array<float, 4>, 8> colors{{
				{0.26f, 0.47f, 0.86f, 1.0f},
				{0.90f, 0.42f, 0.18f, 1.0f},
				{0.24f, 0.66f, 0.38f, 1.0f},
				{0.83f, 0.30f, 0.30f, 1.0f},
				{0.60f, 0.39f, 0.83f, 1.0f},
				{0.88f, 0.67f, 0.16f, 1.0f},
				{0.20f, 0.68f, 0.75f, 1.0f},
				{0.80f, 0.80f, 0.80f, 1.0f},
			}};
			return colors[index % colors.size()];
		}

		int NearestPointIndex(const std::vector<JFMArrheniusPoint> &points, double x)
		{
			if (points.empty())
				return 0;
			auto found = std::lower_bound(points.begin(), points.end(), x, [](const JFMArrheniusPoint &point, double value) {
				return point.x < value;
			});
			if (found == points.begin())
				return 0;
			if (found == points.end())
				return static_cast<int>(points.size() - 1);
			const auto previous = std::prev(found);
			return std::abs(previous->x - x) <= std::abs(found->x - x)
				? static_cast<int>(std::distance(points.begin(), previous))
				: static_cast<int>(std::distance(points.begin(), found));
		}

		std::optional<ArrheniusPlotPanel::FitLine> BuildFit(
			const std::vector<JFMArrheniusPoint> &points,
			int lower,
			int upper,
			JFMArrheniusQuantity quantity,
			int model,
			std::size_t fitIndex)
		{
			if (points.size() < 2)
				return std::nullopt;
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
			fit.model = std::clamp(model, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1);
			fit.lower = lower;
			fit.upper = upper;
			fit.name = std::string(FitModelLabels[fit.model]) + " " + std::to_string(fitIndex + 1);
			fit.slope = (count * sxy - sx * sy) / denominator;
			fit.intercept = (sy - fit.slope * sx) / count;
			constexpr double kEvPerK = 8.617333262e-5;
			fit.eaEv = -fit.slope * kEvPerK * 1000.0;
			fit.egEv = 2.0 * fit.eaEv;
			if (quantity == JFMArrheniusQuantity::ALnI0)
			{
				fit.j00 = std::exp(fit.intercept);
				fit.hasJ00 = std::isfinite(fit.j00);
			}
			if (quantity == JFMArrheniusQuantity::InvA)
			{
				fit.i00 = std::exp(fit.intercept);
				fit.tStar = fit.slope;
				fit.hasI00 = std::isfinite(fit.i00);
				fit.hasTStar = std::isfinite(fit.tStar);
			}
			return fit;
		}

		bool Refit(ArrheniusPlotPanel::FitLine &fit, const std::vector<JFMArrheniusPoint> &points, JFMArrheniusQuantity quantity)
		{
			if (auto updated = BuildFit(points, fit.lower, fit.upper, quantity, fit.model, 0))
			{
				const std::string name = fit.name;
				fit = *updated;
				fit.name = name;
				return true;
			}
			return false;
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

	std::vector<JFMCharacteristicRecord> ArrheniusPlotPanel::RecordsForEntry(ProjectHandle projectHandle, const JFMProject &snapshot, const SeriesEntry &entry) const
	{
		if (entry.sourceMode == 0)
		{
			const auto found = std::find_if(snapshot.series.begin(), snapshot.series.end(), [&entry](const JFMSeries &series) {
				return series.id == entry.sourceId;
			});
			return found == snapshot.series.end() ? std::vector<JFMCharacteristicRecord>{} : RecordsForSeries(projectHandle, *found);
		}

		const auto found = std::find_if(snapshot.groups.begin(), snapshot.groups.end(), [&entry](const JFMGroup &group) {
			return group.id == entry.sourceId;
		});
		return found == snapshot.groups.end() ? std::vector<JFMCharacteristicRecord>{} : RecordsForGroup(projectHandle, snapshot, *found);
	}

	void ArrheniusPlotPanel::AddSelectedSource(ProjectHandle, const JFMProject &snapshot, PlotWindow *target)
	{
		SeriesEntry entry;
		entry.sourceMode = m_SourceMode;
		if (m_SourceMode == 0)
		{
			if (snapshot.series.empty())
				return;
			m_SelectedSeries = std::clamp(m_SelectedSeries, 0, static_cast<int>(snapshot.series.size()) - 1);
			entry.sourceId = snapshot.series[static_cast<std::size_t>(m_SelectedSeries)].id;
			entry.title = snapshot.series[static_cast<std::size_t>(m_SelectedSeries)].name;
		}
		else
		{
			if (snapshot.groups.empty())
				return;
			m_SelectedGroup = std::clamp(m_SelectedGroup, 0, static_cast<int>(snapshot.groups.size()) - 1);
			entry.sourceId = snapshot.groups[static_cast<std::size_t>(m_SelectedGroup)].id;
			entry.title = snapshot.groups[static_cast<std::size_t>(m_SelectedGroup)].name;
		}

		if (target != nullptr)
		{
			const auto duplicate = std::find_if(target->series.begin(), target->series.end(), [&entry](const SeriesEntry &candidate) {
				return candidate.sourceMode == entry.sourceMode && candidate.sourceId == entry.sourceId;
			});
			if (duplicate != target->series.end())
				return;
			entry.style.color = DefaultColor(target->series.size());
			target->series.push_back(std::move(entry));
			target->activeSeries = static_cast<int>(target->series.size()) - 1;
			m_ActivePlotId = target->id;
			return;
		}

		PlotWindow window;
		window.id = m_NextPlotId++;
		window.quantity = m_Quantity;
		window.fitModel = m_FitModel;
		window.preferFit = m_PreferFit;
		window.title = entry.title + " - " + QuantityLabels[std::clamp(m_Quantity, 0, 7)];
		window.series.push_back(std::move(entry));
		m_ActivePlotId = window.id;
		m_PlotWindows.push_back(std::move(window));
	}

	void ArrheniusPlotPanel::AddAllPlots(ProjectHandle projectHandle, const JFMProject &snapshot)
	{
		std::vector<SeriesEntry> entries;
		if (m_SourceMode == 0)
		{
			for (const JFMSeries &series : snapshot.series)
				entries.push_back(SeriesEntry{0, series.id, series.name});
		}
		else
		{
			for (const JFMGroup &group : snapshot.groups)
				entries.push_back(SeriesEntry{1, group.id, group.name});
		}

		for (int quantityIndex = 0; quantityIndex < static_cast<int>(IM_ARRAYSIZE(QuantityLabels)); ++quantityIndex)
		{
			PlotWindow window;
			window.id = m_NextPlotId++;
			window.quantity = quantityIndex;
			window.fitModel = m_FitModel;
			window.preferFit = m_PreferFit;
			window.title = QuantityLabels[quantityIndex];
			for (SeriesEntry entry : entries)
			{
				if (BuildJFMArrheniusPoints(RecordsForEntry(projectHandle, snapshot, entry), ToQuantity(quantityIndex), window.preferFit).empty())
					continue;
				entry.style.color = DefaultColor(window.series.size());
				window.series.push_back(std::move(entry));
			}
			if (!window.series.empty())
			{
				m_ActivePlotId = window.id;
				m_PlotWindows.push_back(std::move(window));
			}
		}
	}

	void ArrheniusPlotPanel::RenderSelector(ProjectHandle projectHandle, const JFMProject &snapshot)
	{
		ImGui::SetNextItemWidth(105.0f);
		ImGui::Combo("Source", &m_SourceMode, "Series\0Group\0");
		ImGui::SameLine();

		if (m_SourceMode == 0)
		{
			m_SelectedSeries = std::clamp(m_SelectedSeries, 0, std::max(0, static_cast<int>(snapshot.series.size()) - 1));
			const char *preview = snapshot.series.empty() ? "No series" : snapshot.series[static_cast<std::size_t>(m_SelectedSeries)].name.c_str();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("##arrhenius_source_item", preview))
			{
				for (std::size_t index = 0; index < snapshot.series.size(); ++index)
					if (ImGui::Selectable(snapshot.series[index].name.c_str(), static_cast<int>(index) == m_SelectedSeries))
						m_SelectedSeries = static_cast<int>(index);
				ImGui::EndCombo();
			}
		}
		else
		{
			m_SelectedGroup = std::clamp(m_SelectedGroup, 0, std::max(0, static_cast<int>(snapshot.groups.size()) - 1));
			const char *preview = snapshot.groups.empty() ? "No groups" : snapshot.groups[static_cast<std::size_t>(m_SelectedGroup)].name.c_str();
			ImGui::SetNextItemWidth(220.0f);
			if (ImGui::BeginCombo("##arrhenius_source_item", preview))
			{
				for (std::size_t index = 0; index < snapshot.groups.size(); ++index)
					if (ImGui::Selectable(snapshot.groups[index].name.c_str(), static_cast<int>(index) == m_SelectedGroup))
						m_SelectedGroup = static_cast<int>(index);
				ImGui::EndCombo();
			}
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(190.0f);
		ImGui::Combo("Plot", &m_Quantity, QuantityLabels, IM_ARRAYSIZE(QuantityLabels));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(95.0f);
		ImGui::Combo("Fit", &m_FitModel, FitModelLabels, IM_ARRAYSIZE(FitModelLabels));
		ImGui::SameLine();
		ImGui::Checkbox("Prefer fit", &m_PreferFit);

		if (ImGui::Button("Add Plot", ImVec2(92.0f, 0.0f)))
			AddSelectedSource(projectHandle, snapshot, nullptr);
		ImGui::SameLine();
		if (ImGui::Button("Plot all", ImVec2(92.0f, 0.0f)))
			AddAllPlots(projectHandle, snapshot);
		ImGui::SameLine();

		PlotWindow *active = nullptr;
		for (PlotWindow &window : m_PlotWindows)
			if (window.id == m_ActivePlotId)
				active = &window;
		const char *activeTitle = active != nullptr ? active->title.c_str() : "Target plot";
		ImGui::SetNextItemWidth(240.0f);
		if (ImGui::BeginCombo("##arrhenius_target_plot", activeTitle))
		{
			for (PlotWindow &window : m_PlotWindows)
			{
				const bool selected = window.id == m_ActivePlotId;
				if (ImGui::Selectable(window.title.c_str(), selected))
					m_ActivePlotId = window.id;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(active == nullptr);
		if (ImGui::Button("Add to Plot", ImVec2(92.0f, 0.0f)))
			AddSelectedSource(projectHandle, snapshot, active);
		ImGui::EndDisabled();
	}

	void ArrheniusPlotPanel::RenderPlotWindow(ProjectHandle projectHandle, const JFMProject &snapshot, PlotWindow &window)
	{
		if (!window.open)
			return;

		bool open = window.open;
		const std::string title = window.title + "###arrhenius_plot_window_" + std::to_string(window.id);
		if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::End();
			window.open = open;
			return;
		}
		window.open = open;
		m_ActivePlotId = window.id;

		ImGui::SetNextItemWidth(190.0f);
		ImGui::Combo("Plot", &window.quantity, QuantityLabels, IM_ARRAYSIZE(QuantityLabels));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(95.0f);
		ImGui::Combo("Fit", &window.fitModel, FitModelLabels, IM_ARRAYSIZE(FitModelLabels));
		ImGui::SameLine();
		ImGui::Checkbox("Prefer fit", &window.preferFit);
		ImGui::SameLine();
		ImGui::Checkbox("Auto refit", &window.autoRefit);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(105.0f);
		ImGui::Combo("Source", &m_SourceMode, "Series\0Group\0");
		ImGui::SameLine();
		if (m_SourceMode == 0)
		{
			m_SelectedSeries = std::clamp(m_SelectedSeries, 0, std::max(0, static_cast<int>(snapshot.series.size()) - 1));
			const char *preview = snapshot.series.empty() ? "No series" : snapshot.series[static_cast<std::size_t>(m_SelectedSeries)].name.c_str();
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::BeginCombo("##plot_source_series", preview))
			{
				for (std::size_t index = 0; index < snapshot.series.size(); ++index)
					if (ImGui::Selectable(snapshot.series[index].name.c_str(), static_cast<int>(index) == m_SelectedSeries))
						m_SelectedSeries = static_cast<int>(index);
				ImGui::EndCombo();
			}
		}
		else
		{
			m_SelectedGroup = std::clamp(m_SelectedGroup, 0, std::max(0, static_cast<int>(snapshot.groups.size()) - 1));
			const char *preview = snapshot.groups.empty() ? "No groups" : snapshot.groups[static_cast<std::size_t>(m_SelectedGroup)].name.c_str();
			ImGui::SetNextItemWidth(180.0f);
			if (ImGui::BeginCombo("##plot_source_group", preview))
			{
				for (std::size_t index = 0; index < snapshot.groups.size(); ++index)
					if (ImGui::Selectable(snapshot.groups[index].name.c_str(), static_cast<int>(index) == m_SelectedGroup))
						m_SelectedGroup = static_cast<int>(index);
				ImGui::EndCombo();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Add source", ImVec2(92.0f, 0.0f)))
			AddSelectedSource(projectHandle, snapshot, &window);
		const JFMArrheniusQuantity quantity = ToQuantity(window.quantity);

		if (ImGui::BeginTable(("##arrhenius_series_table_" + std::to_string(window.id)).c_str(), 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 45.0f);
			ImGui::TableSetupColumn("Source");
			ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableSetupColumn("Style", ImGuiTableColumnFlags_WidthFixed, 115.0f);
			ImGui::TableSetupColumn("Lower", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Upper", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Fit", ImGuiTableColumnFlags_WidthFixed, 72.0f);
			ImGui::TableHeadersRow();

			for (std::size_t index = 0; index < window.series.size(); ++index)
			{
				SeriesEntry &series = window.series[index];
				const std::vector<JFMArrheniusPoint> points = BuildJFMArrheniusPoints(RecordsForEntry(projectHandle, snapshot, series), quantity, window.preferFit);
				if (!points.empty())
				{
					series.lower = std::clamp(series.lower, 0, static_cast<int>(points.size()) - 1);
					if (series.upper <= series.lower || series.upper >= static_cast<int>(points.size()))
						series.upper = static_cast<int>(points.size()) - 1;
				}

				bool rangeChanged = false;
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::PushID(static_cast<int>(index));
				ImGui::Checkbox("##show", &series.style.visible);
				ImGui::TableSetColumnIndex(1);
				if (ImGui::Selectable(series.title.c_str(), window.activeSeries == static_cast<int>(index)))
					window.activeSeries = static_cast<int>(index);
				ImGui::TableSetColumnIndex(2);
				ImGui::ColorEdit4("##color", series.style.color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
				ImGui::TableSetColumnIndex(3);
				ImGui::SetNextItemWidth(-1.0f);
				ImGui::Combo("##style", &series.style.drawMode, DrawModeLabels, IM_ARRAYSIZE(DrawModeLabels));
				ImGui::TableSetColumnIndex(4);
				ImGui::SetNextItemWidth(88.0f);
				rangeChanged |= !points.empty() && ImGui::InputInt("##lower", &series.lower, 1, 5);
				ImGui::TableSetColumnIndex(5);
				ImGui::SetNextItemWidth(88.0f);
				rangeChanged |= !points.empty() && ImGui::InputInt("##upper", &series.upper, 1, 5);
				if (!points.empty())
				{
					series.lower = std::clamp(series.lower, 0, static_cast<int>(points.size()) - 1);
					series.upper = std::clamp(series.upper, 0, static_cast<int>(points.size()) - 1);
				}
				if (series.upper < series.lower)
					series.upper = series.lower;
				ImGui::TableSetColumnIndex(6);
				ImGui::BeginDisabled(points.size() < 2);
				if (ImGui::Button("Add"))
				{
					if (auto fit = BuildFit(points, series.lower, series.upper, quantity, window.fitModel, series.fits.size()))
						series.fits.push_back(*fit);
				}
				ImGui::EndDisabled();

				if (rangeChanged && window.autoRefit)
				{
					for (FitLine &fit : series.fits)
					{
						fit.lower = series.lower;
						fit.upper = series.upper;
						(void)Refit(fit, points, quantity);
					}
				}
				else if (window.autoRefit)
				{
					for (FitLine &fit : series.fits)
						(void)Refit(fit, points, quantity);
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		if (ImPlot::BeginPlot(("##arrhenius_plot_" + std::to_string(window.id)).c_str(), ImVec2(-1.0f, 360.0f)))
		{
			ImPlot::SetupAxes(JFMArrheniusUsesInvTemperature(quantity) ? "1000/T" : "T (K)", JFMArrheniusQuantityName(quantity));

			for (std::size_t seriesIndex = 0; seriesIndex < window.series.size(); ++seriesIndex)
			{
				SeriesEntry &series = window.series[seriesIndex];
				const std::vector<JFMArrheniusPoint> points = BuildJFMArrheniusPoints(RecordsForEntry(projectHandle, snapshot, series), quantity, window.preferFit);
				if (points.empty())
					continue;

				series.lower = std::clamp(series.lower, 0, static_cast<int>(points.size()) - 1);
				if (series.upper <= series.lower || series.upper >= static_cast<int>(points.size()))
					series.upper = static_cast<int>(points.size()) - 1;

				std::vector<double> includedX;
				std::vector<double> includedY;
				std::vector<double> includedErr;
				std::vector<double> excludedX;
				std::vector<double> excludedY;
				bool hasUncertainty = false;
				for (std::size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
				{
					const bool included = static_cast<int>(pointIndex) >= series.lower && static_cast<int>(pointIndex) <= series.upper;
					if (included)
					{
						includedX.push_back(points[pointIndex].x);
						includedY.push_back(points[pointIndex].y);
						includedErr.push_back(points[pointIndex].uncertainty.value_or(0.0));
						hasUncertainty |= points[pointIndex].uncertainty.has_value();
					}
					else
					{
						excludedX.push_back(points[pointIndex].x);
						excludedY.push_back(points[pointIndex].y);
					}
				}

				const std::string label = series.title + "##arrhenius_data_" + std::to_string(window.id) + "_" + std::to_string(seriesIndex);
				if (series.style.visible && !excludedX.empty())
				{
					ImPlotSpec excludedSpec;
					excludedSpec.LineColor = ImVec4(0.42f, 0.42f, 0.42f, 0.65f);
					excludedSpec.MarkerFillColor = ImVec4(0.42f, 0.42f, 0.42f, 0.65f);
					excludedSpec.MarkerLineColor = ImVec4(0.42f, 0.42f, 0.42f, 0.65f);
					excludedSpec.Marker = ImPlotMarker_Circle;
					excludedSpec.MarkerSize = std::max(2.0f, series.style.pointSize - 1.0f);
					excludedSpec.LineWeight = 0.0f;
					excludedSpec.Flags = ImPlotItemFlags_NoLegend;
					ImPlot::PlotScatter(("##excluded_" + label).c_str(), excludedX.data(), excludedY.data(), static_cast<int>(excludedX.size()), excludedSpec);
				}

				if (series.style.visible && !includedX.empty())
				{
					ImPlotSpec spec;
					spec.LineColor = ToVec4(series.style.color);
					spec.MarkerFillColor = ToVec4(series.style.color);
					spec.MarkerLineColor = ToVec4(series.style.color);
					spec.Marker = ImPlotMarker_Circle;
					spec.MarkerSize = series.style.pointSize;
					spec.LineWeight = series.style.lineWeight;
					if (series.style.drawMode == 0)
						ImPlot::PlotScatter(label.c_str(), includedX.data(), includedY.data(), static_cast<int>(includedX.size()), spec);
					else if (series.style.drawMode == 1)
						ImPlot::PlotLine(label.c_str(), includedX.data(), includedY.data(), static_cast<int>(includedX.size()), spec);
					else
					{
						ImPlot::PlotScatter(label.c_str(), includedX.data(), includedY.data(), static_cast<int>(includedX.size()), spec);
						ImPlotSpec lineSpec = spec;
						lineSpec.Flags = ImPlotItemFlags_NoLegend;
						ImPlot::PlotLine(("##arrhenius_line_" + std::to_string(window.id) + "_" + std::to_string(seriesIndex)).c_str(), includedX.data(), includedY.data(), static_cast<int>(includedX.size()), lineSpec);
					}
					if (hasUncertainty)
					{
						ImPlotSpec errorSpec;
						errorSpec.LineColor = ToVec4(series.style.color);
						errorSpec.Size = 7.0f;
						errorSpec.LineWeight = 2.0f;
						errorSpec.Flags = ImPlotItemFlags_NoLegend;
						ImPlot::PlotErrorBars(("##arrhenius_err_" + std::to_string(window.id) + "_" + std::to_string(seriesIndex)).c_str(), includedX.data(), includedY.data(), includedErr.data(), static_cast<int>(includedX.size()), errorSpec);
					}
				}

				double lowerX = points[static_cast<std::size_t>(series.lower)].x;
				double upperX = points[static_cast<std::size_t>(series.upper)].x;
				bool lowerHeld = false;
				bool upperHeld = false;
				const ImVec4 rangeColor(1.0f, 0.55f, 0.0f, 1.0f);
				const int dragBase = static_cast<int>((window.id % 100000) * 100 + seriesIndex * 2);
				const bool lowerChanged = ImPlot::DragLineX(dragBase + 1, &lowerX, rangeColor, 2.0f, ImPlotDragToolFlags_None, nullptr, nullptr, &lowerHeld);
				const bool upperChanged = ImPlot::DragLineX(dragBase + 2, &upperX, rangeColor, 2.0f, ImPlotDragToolFlags_None, nullptr, nullptr, &upperHeld);
				if (lowerChanged || upperChanged)
				{
					if (lowerChanged)
						series.lower = std::min(NearestPointIndex(points, lowerX), series.upper);
					if (upperChanged)
						series.upper = std::max(NearestPointIndex(points, upperX), series.lower);
					if (window.autoRefit)
					{
						for (FitLine &fit : series.fits)
						{
							fit.lower = series.lower;
							fit.upper = series.upper;
							(void)Refit(fit, points, quantity);
						}
					}
				}

				for (FitLine &fit : series.fits)
				{
					if (window.autoRefit)
						(void)Refit(fit, points, quantity);
					const int lower = std::clamp(fit.lower, 0, static_cast<int>(points.size()) - 1);
					const int upper = std::clamp(fit.upper, lower, static_cast<int>(points.size()) - 1);
					const double xx[2] = {points[static_cast<std::size_t>(lower)].x, points[static_cast<std::size_t>(upper)].x};
					const double yy[2] = {fit.intercept + fit.slope * xx[0], fit.intercept + fit.slope * xx[1]};
					ImPlotSpec fitSpec;
					fitSpec.LineColor = ToVec4(series.style.color);
					fitSpec.LineWeight = std::max(2.0f, series.style.lineWeight + 1.0f);
					ImPlot::PlotLine((series.title + " " + fit.name + "##arrhenius_fit_" + std::to_string(window.id) + "_" + std::to_string(seriesIndex) + "_" + fit.name).c_str(), xx, yy, 2, fitSpec);
				}

				if (ImPlot::BeginLegendPopup(label.c_str()))
				{
					ImGui::Checkbox("Show", &series.style.visible);
					ImGui::ColorEdit4("Color", series.style.color.data(), ImGuiColorEditFlags_AlphaBar);
					ImGui::SliderFloat("Point size", &series.style.pointSize, 1.0f, 12.0f, "%.1f");
					ImGui::SliderFloat("Line", &series.style.lineWeight, 0.5f, 6.0f, "%.1f");
					ImGui::Combo("Type", &series.style.drawMode, DrawModeLabels, IM_ARRAYSIZE(DrawModeLabels));
					ImPlot::EndLegendPopup();
				}
			}
			ImPlot::EndPlot();
		}

		if (ImGui::BeginTable(("##arrhenius_fit_table_" + std::to_string(window.id)).c_str(), 12, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Series");
			ImGui::TableSetupColumn("Fit");
			ImGui::TableSetupColumn("Model");
			ImGui::TableSetupColumn("Range");
			ImGui::TableSetupColumn("Slope");
			ImGui::TableSetupColumn("Intercept");
			ImGui::TableSetupColumn("E_A [eV]");
			ImGui::TableSetupColumn("E_g [eV]");
			ImGui::TableSetupColumn("J_00");
			ImGui::TableSetupColumn("I_00");
			ImGui::TableSetupColumn("T*");
			ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableHeadersRow();
			for (std::size_t seriesIndex = 0; seriesIndex < window.series.size(); ++seriesIndex)
			{
				SeriesEntry &series = window.series[seriesIndex];
				for (std::size_t fitIndex = 0; fitIndex < series.fits.size();)
				{
					FitLine &fit = series.fits[fitIndex];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(series.title.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(fit.name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(FitModelLabels[std::clamp(fit.model, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1)]);
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%d-%d", fit.lower, fit.upper);
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%.8g", fit.slope);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%.8g", fit.intercept);
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%.8g", fit.eaEv);
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%.8g", fit.egEv);
					ImGui::TableSetColumnIndex(8);
					if (fit.hasJ00)
						ImGui::Text("%.8g", fit.j00);
					ImGui::TableSetColumnIndex(9);
					if (fit.hasI00)
						ImGui::Text("%.8g", fit.i00);
					ImGui::TableSetColumnIndex(10);
					if (fit.hasTStar)
						ImGui::Text("%.8g", fit.tStar);
					ImGui::TableSetColumnIndex(11);
					ImGui::PushID(static_cast<int>(seriesIndex * 1000 + fitIndex));
					if (ImGui::Button("Remove"))
					{
						series.fits.erase(series.fits.begin() + static_cast<std::ptrdiff_t>(fitIndex));
						ImGui::PopID();
						continue;
					}
					ImGui::PopID();
					++fitIndex;
				}
			}
			ImGui::EndTable();
		}

		ImGui::End();
	}

	void ArrheniusPlotPanel::Render()
	{
		if (m_Store == nullptr)
		{
			if (!IsVisible())
				return;
			bool visible = IsVisible();
			if (ImGui::Begin(GetTitle().c_str(), &visible))
				ImGui::TextDisabled("Workspace unavailable");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		const ProjectHandle projectHandle = m_Store->GetActiveProjectHandle();
		const JFMProject snapshot = m_Store->GetProjectSnapshot(projectHandle);
		if (IsVisible())
		{
			bool visible = IsVisible();
			if (ImGui::Begin(GetTitle().c_str(), &visible, ImGuiWindowFlags_HorizontalScrollbar))
			{
				RenderSelector(projectHandle, snapshot);
				ImGui::Separator();
				if (m_PlotWindows.empty())
					ImGui::TextDisabled("Choose a source and plot type, then Add Plot.");
				else if (ImGui::BeginTable("##arrhenius_windows", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Plot");
					ImGui::TableSetupColumn("Series", ImGuiTableColumnFlags_WidthFixed, 70.0f);
					ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 100.0f);
					ImGui::TableHeadersRow();
					for (PlotWindow &window : m_PlotWindows)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(window.title.c_str());
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%zu", window.series.size());
						ImGui::TableSetColumnIndex(2);
						if (ImGui::Button(("Focus##arrhenius_focus_" + std::to_string(window.id)).c_str()))
						{
							window.open = true;
							m_ActivePlotId = window.id;
						}
					}
					ImGui::EndTable();
				}
			}
			ImGui::End();
			SetVisible(visible);
		}

		for (PlotWindow &window : m_PlotWindows)
			RenderPlotWindow(projectHandle, snapshot, window);
		m_PlotWindows.erase(std::remove_if(m_PlotWindows.begin(), m_PlotWindows.end(), [](const PlotWindow &window) {
			return !window.open;
		}), m_PlotWindows.end());
	}
} // namespace SST::Prototypes::JFM
