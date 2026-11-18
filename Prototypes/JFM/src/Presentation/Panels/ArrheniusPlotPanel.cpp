#include "Core/dspch.hpp"

#include "Presentation/Panels/ArrheniusPlotPanel.hpp"
#include "Presentation/JFMPlotWidgets.hpp"

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Core/Platform/ProcessRunner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>
#include <sst/ui/Theme/ScientificPlotPreset.hpp>

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

		constexpr const char *FitModelLabels[] = {"TE-IR", "TE-SCR"};
		constexpr const char *DrawModeLabels[] = {"Scatter", "Line", "Line + scatter"};
		constexpr const char *MarkerLabels[] = {"Circle", "Square", "Diamond", "Cross", "Plus", "Asterisk"};

		JFMArrheniusQuantity ToQuantity(int index)
		{
			return static_cast<JFMArrheniusQuantity>(std::clamp(index, 0, 7));
		}

		ImVec4 ToVec4(const std::array<float, 4> &color)
		{
			return {color[0], color[1], color[2], color[3]};
		}

		std::array<float, 4> TunedColor(std::size_t index)
		{
			static constexpr std::array<std::array<float, 4>, 4> colors{{
				{1.00f, 0.52f, 0.10f, 1.0f},
				{0.95f, 0.72f, 0.18f, 1.0f},
				{0.80f, 0.40f, 0.85f, 1.0f},
				{0.20f, 0.86f, 0.76f, 1.0f},
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

		std::string QuantityToken(JFMArrheniusQuantity quantity)
		{
			switch (quantity)
			{
			case JFMArrheniusQuantity::A: return "A";
			case JFMArrheniusQuantity::InvA: return "InvA";
			case JFMArrheniusQuantity::ALnI0: return "ALnI0";
			case JFMArrheniusQuantity::LnI0: return "LnI0";
			case JFMArrheniusQuantity::Rs: return "Rs";
			case JFMArrheniusQuantity::Rsh: return "Rsh";
			case JFMArrheniusQuantity::Alpha: return "Alpha";
			case JFMArrheniusQuantity::Rsh2: return "Rsh2";
			}
			return "Linear";
		}

		std::string FitModeFor(JFMArrheniusQuantity quantity, int model)
		{
			if (quantity == JFMArrheniusQuantity::A || quantity == JFMArrheniusQuantity::InvA)
				return std::clamp(model, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1) == 0 ? "te_ir" : "te_scr";
			if (quantity == JFMArrheniusQuantity::ALnI0 || quantity == JFMArrheniusQuantity::LnI0)
				return "ea_j00";
			return "linear";
		}

		Path ResolveArrheniusFitScript()
		{
			const std::array<Path, 4> candidates = {
				Path("python") / "jfm_arrhenius" / "fit_arrhenius.py",
				Path("Prototypes") / "JFM" / "python" / "jfm_arrhenius" / "fit_arrhenius.py",
				Path("install") / "app" / "python" / "jfm_arrhenius" / "fit_arrhenius.py",
				[] {
					const auto executable = Platform::CurrentExecutablePath();
					if (!executable)
						return Path{};
					return executable->parent_path() / "python" / "jfm_arrhenius" / "fit_arrhenius.py";
				}()};
			for (const Path &candidate : candidates)
				if (!candidate.Empty() && FileSystem::Exists(candidate.Native()))
					return candidate;
			return {};
		}

		bool WriteArrheniusFitInput(
			const Path &path,
			const std::vector<JFMArrheniusPoint> &points,
			int lower,
			int upper,
			JFMArrheniusQuantity quantity,
			int model)
		{
			std::ofstream out(path.Native());
			if (!out)
				return false;
			out << std::setprecision(17);
			out << "mode\t" << FitModeFor(quantity, model) << "\n";
			out << "quantity\t" << QuantityToken(quantity) << "\n";
			out << "name\tT\tx\ty\tsigma_y\tA\tA_std\tI0\tI0_std\n";
			for (int index = lower; index <= upper; ++index)
			{
				const JFMArrheniusPoint &point = points[static_cast<std::size_t>(index)];
				out << point.name << '\t'
					<< point.temperatureK << '\t'
					<< point.x << '\t'
					<< point.y << '\t'
					<< point.uncertainty.value_or(0.0) << '\t'
					<< point.a.value_or(0.0) << '\t'
					<< point.aStd.value_or(0.0) << '\t'
					<< point.i0.value_or(0.0) << '\t'
					<< point.i0Std.value_or(0.0) << '\n';
			}
			return true;
		}

		std::vector<double> ParseDoubleList(const std::string &text)
		{
			std::vector<double> values;
			std::stringstream stream(text);
			std::string token;
			while (std::getline(stream, token, ','))
			{
				try
				{
					values.push_back(std::stod(token));
				}
				catch (const std::exception &)
				{
				}
			}
			return values;
		}

		double ReadDouble(const std::map<std::string, std::string> &values, const char *key, bool *has = nullptr)
		{
			const auto found = values.find(key);
			if (found == values.end() || found->second.empty())
			{
				if (has != nullptr)
					*has = false;
				return 0.0;
			}
			try
			{
				const double value = std::stod(found->second);
				const bool ok = std::isfinite(value);
				if (has != nullptr)
					*has = ok;
				return ok ? value : 0.0;
			}
			catch (const std::exception &)
			{
				if (has != nullptr)
					*has = false;
				return 0.0;
			}
		}

		std::optional<std::map<std::string, std::string>> ReadArrheniusFitOutput(const Path &path)
		{
			std::ifstream in(path.Native());
			if (!in)
				return std::nullopt;
			std::map<std::string, std::string> values;
			std::string line;
			while (std::getline(in, line))
			{
				const std::size_t tab = line.find('\t');
				if (tab == std::string::npos)
					continue;
				values[line.substr(0, tab)] = line.substr(tab + 1);
			}
			if (values["ok"] != "1")
				return std::nullopt;
			return values;
		}

		std::optional<std::map<std::string, std::string>> RunArrheniusFitScript(
			const std::vector<JFMArrheniusPoint> &points,
			int lower,
			int upper,
			JFMArrheniusQuantity quantity,
			int model)
		{
			const Path script = ResolveArrheniusFitScript();
			if (script.Empty())
				return std::nullopt;
			std::error_code error;
			const auto directory = FileSystem::TempDirectoryPath(error) / "jfm-arrhenius";
			if (error)
				return std::nullopt;
			std::filesystem::create_directories(directory, error);
			if (error)
				return std::nullopt;
			const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			const Path input = Path::FromResolved(directory / ("fit-" + std::to_string(stamp) + ".tsv"));
			const Path output = Path::FromResolved(directory / ("fit-" + std::to_string(stamp) + ".out"));
			if (!WriteArrheniusFitInput(input, points, lower, upper, quantity, model))
				return std::nullopt;
			const std::array<Platform::ProcessRunOptions, 3> options = {{
				{Path("python"), {script.String(), input.String(), output.String()}, Path::FromResolved(FileSystem::CurrentPath()), std::chrono::seconds(20)},
				{Path("py"), {"-3", script.String(), input.String(), output.String()}, Path::FromResolved(FileSystem::CurrentPath()), std::chrono::seconds(20)},
				{Platform::ResolvePreferredPythonExecutable(), {script.String(), input.String(), output.String()}, Path::FromResolved(FileSystem::CurrentPath()), std::chrono::seconds(20)}}};
			for (const Platform::ProcessRunOptions &option : options)
			{
				const auto result = Platform::RunProcess(option);
				if (result && result->exitCode == 0)
				{
					auto parsed = ReadArrheniusFitOutput(output);
					std::filesystem::remove(input.Native(), error);
					std::filesystem::remove(output.Native(), error);
					return parsed;
				}
			}
			std::filesystem::remove(input.Native(), error);
			std::filesystem::remove(output.Native(), error);
			return std::nullopt;
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

			const auto values = RunArrheniusFitScript(points, lower, upper, quantity, model);
			if (!values)
				return std::nullopt;

			ArrheniusPlotPanel::FitLine fit;
			fit.model = std::clamp(model, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1);
			fit.lower = lower;
			fit.upper = upper;
			const auto mode = values->find("mode");
			fit.modelName = mode != values->end() ? mode->second : FitModeFor(quantity, model);
			fit.name = fit.modelName + " " + std::to_string(fitIndex + 1);
			bool hasValue = false;
			fit.slope = ReadDouble(*values, "slope", &hasValue);
			fit.slopeStd = ReadDouble(*values, "slope_std", &fit.hasSlopeStd);
			fit.intercept = ReadDouble(*values, "intercept", &hasValue);
			fit.interceptStd = ReadDouble(*values, "intercept_std", &fit.hasInterceptStd);
			fit.eaEv = ReadDouble(*values, "E_a", &fit.hasEa);
			fit.eaStd = ReadDouble(*values, "E_a_std", &fit.hasEaStd);
			fit.j00 = ReadDouble(*values, "J_00", &fit.hasJ00);
			fit.j00Std = ReadDouble(*values, "J_00_std", &fit.hasJ00Std);
			fit.e00 = ReadDouble(*values, "E_00", &fit.hasE00);
			fit.e00Std = ReadDouble(*values, "E_00_std", &fit.hasE00Std);
			fit.eStar = ReadDouble(*values, "E_star", &fit.hasEStar);
			fit.eStarStd = ReadDouble(*values, "E_star_std", &fit.hasEStarStd);
			fit.tStar = ReadDouble(*values, "T_star", &fit.hasTStar);
			fit.tStarStd = ReadDouble(*values, "T_star_std", &fit.hasTStarStd);
			if (const auto fitX = values->find("fit_x"); fitX != values->end())
				fit.fitX = ParseDoubleList(fitX->second);
			if (const auto fitY = values->find("fit_y"); fitY != values->end())
				fit.fitY = ParseDoubleList(fitY->second);
			return fit;
		}

		bool Refit(ArrheniusPlotPanel::FitLine &fit, const std::vector<JFMArrheniusPoint> &points, JFMArrheniusQuantity quantity)
		{
			if (auto updated = BuildFit(points, fit.lower, fit.upper, quantity, fit.model, 0))
			{
				const std::string name = fit.name;
				const auto color = fit.color;
				const float lineWeight = fit.lineWeight;
				const bool visible = fit.visible;
				const auto plotKey = fit.plotKey;
				fit = *updated;
				fit.plotKey = plotKey;
				fit.name = name;
				fit.color = color;
				fit.lineWeight = lineWeight;
				fit.visible = visible;
				return true;
			}
			return false;
		}

		std::string ValueText(double value, bool hasStd, double std)
		{
			std::ostringstream out;
			if (hasStd)
				out << std::setprecision(8) << value << " +/- " << std::setprecision(3) << std;
			else
				out << std::setprecision(8) << value;
			return out.str();
		}

		void TextValue(double value, bool hasStd, double std)
		{
			const std::string text = ValueText(value, hasStd, std);
			ImGui::TextUnformatted(text.c_str());
		}

		template <std::size_t Size>
		void GrowColumn(std::array<float, Size> &widths, std::size_t column, const std::string &text)
		{
			if (column >= widths.size())
				return;
			const float padding = ImGui::GetStyle().CellPadding.x * 2.0f + 28.0f;
			widths[column] = std::max(widths[column], ImGui::CalcTextSize(text.c_str()).x + padding);
		}

		template <std::size_t Size>
		void ApplyColumnWidths(const std::array<float, Size> &widths)
		{
			for (std::size_t index = 0; index < widths.size(); ++index)
				ImGui::TableSetColumnWidth(static_cast<int>(index), widths[index]);
		}

		float TableHeightForRows(std::size_t rows)
		{
			return ImGui::GetFrameHeightWithSpacing() * static_cast<float>(std::max<std::size_t>(1, rows) + 1) + ImGui::GetStyle().CellPadding.y * 2.0f;
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

	std::vector<JFMArrheniusPoint> ArrheniusPlotPanel::PointsForEntry(
		ProjectHandle projectHandle,
		const JFMProject &snapshot,
		const SeriesEntry &entry,
		JFMArrheniusQuantity quantity,
		bool preferFit,
		bool useTuned) const
	{
		const JFMArrheniusParameterSource parameterSource = useTuned
			? JFMArrheniusParameterSource::Tuned
			: preferFit ? JFMArrheniusParameterSource::Fit : JFMArrheniusParameterSource::Prefit;
		std::vector<JFMCharacteristicRecord> records = RecordsForEntry(projectHandle, snapshot, entry);
		std::vector<JFMArrheniusPoint> points;
		for (const JFMCharacteristicRecord &record : records)
		{
			if (!record.info.temperatureK || *record.info.temperatureK <= 0.0)
				continue;
			const auto value = JFMArrheniusValue(quantity, record.info, parameterSource);
			if (!value || !std::isfinite(*value))
				continue;

			std::string source = useTuned ? "tuned" : "";
			if (!useTuned)
				(void)JFMFindParameter(record.info, quantity == JFMArrheniusQuantity::LnI0 ? "I0" : "A", preferFit, &source);
			std::optional<double> uncertainty;
			if (!useTuned && m_Store != nullptr)
			{
				const Ref<const JFMMonteCarloResult> result = m_Store->GetMonteCarloResult({projectHandle, record.info.id});
				if (result != nullptr)
					uncertainty = JFMArrheniusMonteCarloUncertainty(quantity, *result);
			}
			if (!useTuned && !uncertainty)
				uncertainty = JFMArrheniusUncertainty(quantity, record.info, preferFit);

			auto parameter = [&](const char *name) -> std::pair<std::optional<double>, std::optional<double>> {
				if (useTuned)
					return {JFMTunedParameterValue(record.info, name), std::nullopt};
				const JFMParameterEstimate *estimate = JFMFindParameter(record.info, name, preferFit);
				if (estimate == nullptr)
					return {std::nullopt, std::nullopt};
				return {estimate->value, estimate->standardError};
			};
			const auto [a, aStd] = parameter("A");
			const auto [i0, i0Std] = parameter("I0");

			const double temperature = *record.info.temperatureK;
			points.push_back(JFMArrheniusPoint{
				record.info.name,
				temperature,
				JFMArrheniusUsesInvTemperature(quantity) ? 1000.0 / temperature : temperature,
				*value,
				uncertainty,
				source,
				a,
				aStd,
				i0,
				i0Std});
		}
		std::sort(points.begin(), points.end(), [](const JFMArrheniusPoint &lhs, const JFMArrheniusPoint &rhs) {
			return lhs.x < rhs.x;
		});
		return points;
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
			entry.style.color = sst::ui::plot::ScientificColor(target->series.size());
			entry.tunedStyle.color = TunedColor(target->series.size());
			entry.tunedStyle.visible = m_UseTuned;
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
		entry.style.color = sst::ui::plot::ScientificColor(0);
		entry.tunedStyle.color = TunedColor(0);
		entry.tunedStyle.visible = m_UseTuned;
		window.series.push_back(std::move(entry));
		window.needsDock = true;
		window.dockSlot = 0;
		m_RebuildDockLayout = true;
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
				if (PointsForEntry(projectHandle, snapshot, entry, ToQuantity(quantityIndex), window.preferFit, false).empty()
					&& PointsForEntry(projectHandle, snapshot, entry, ToQuantity(quantityIndex), window.preferFit, true).empty())
					continue;
				entry.style.color = sst::ui::plot::ScientificColor(window.series.size());
				entry.tunedStyle.color = TunedColor(window.series.size());
				entry.tunedStyle.visible = m_UseTuned;
				window.series.push_back(std::move(entry));
			}
			if (!window.series.empty())
			{
				window.needsInitialPlacement = true;
				window.initialPlacement = static_cast<int>(m_PlotWindows.size());
				window.needsDock = true;
				window.dockSlot = window.initialPlacement % static_cast<int>(m_DockIds.size());
				m_RebuildDockLayout = true;
				m_ActivePlotId = window.id;
				m_PlotWindows.push_back(std::move(window));
			}
		}
	}

	JFMArrheniusLayoutDocument ArrheniusPlotPanel::CaptureLayoutDocument() const
	{
		JFMArrheniusLayoutDocument document;
		document.plots.reserve(m_PlotWindows.size());
		for (const PlotWindow &window : m_PlotWindows)
		{
			JFMArrheniusPlotLayout plot;
			plot.id = window.id;
			plot.title = window.title;
			plot.quantity = window.quantity;
			plot.fitModel = window.fitModel;
			plot.preferFit = window.preferFit;
			plot.autoRefit = window.autoRefit;
			plot.open = window.open;
			plot.dockSlot = window.dockSlot;
			plot.activeSeries = window.activeSeries;
			plot.series.reserve(window.series.size());
			for (const SeriesEntry &series : window.series)
			{
				JFMArrheniusSeriesLayout entry;
				entry.sourceMode = series.sourceMode;
				entry.sourceId = series.sourceId;
				entry.title = series.title;
				entry.lower = series.lower;
				entry.upper = series.upper;
				entry.style = JFMArrheniusLayoutStyle{series.style.color, series.style.pointSize, series.style.lineWeight, series.style.drawMode, series.style.marker, series.style.visible};
				entry.tunedStyle = JFMArrheniusLayoutStyle{series.tunedStyle.color, series.tunedStyle.pointSize, series.tunedStyle.lineWeight, series.tunedStyle.drawMode, series.tunedStyle.marker, series.tunedStyle.visible};
				entry.fits.reserve(series.fits.size());
				for (const FitLine &fit : series.fits)
				{
					JFMArrheniusFitLayout fitLayout;
					fitLayout.name = fit.name;
					fitLayout.modelName = fit.modelName;
					fitLayout.model = fit.model;
					fitLayout.lower = fit.lower;
					fitLayout.upper = fit.upper;
					fitLayout.color = fit.color;
					fitLayout.lineWeight = fit.lineWeight;
					fitLayout.visible = fit.visible;
					entry.fits.push_back(std::move(fitLayout));
				}
				plot.series.push_back(std::move(entry));
			}
			document.plots.push_back(std::move(plot));
		}
		return document;
	}

	void ArrheniusPlotPanel::RestoreLayoutDocument(const JFMArrheniusLayoutDocument &layout)
	{
		m_PlotWindows.clear();
		m_NextPlotId = 1;
		m_ActivePlotId = 0;
		for (const JFMArrheniusPlotLayout &plotLayout : layout.plots)
		{
			PlotWindow window;
			window.id = plotLayout.id == 0 ? m_NextPlotId++ : plotLayout.id;
			window.title = plotLayout.title.empty()
				? std::string{QuantityLabels[std::clamp(plotLayout.quantity, 0, static_cast<int>(IM_ARRAYSIZE(QuantityLabels)) - 1)]}
				: plotLayout.title;
			window.quantity = plotLayout.quantity;
			window.fitModel = plotLayout.fitModel;
			window.preferFit = plotLayout.preferFit;
			window.autoRefit = plotLayout.autoRefit;
			window.open = plotLayout.open;
			window.dockSlot = plotLayout.dockSlot;
			window.activeSeries = plotLayout.activeSeries;
			window.needsDock = window.open;
			for (const JFMArrheniusSeriesLayout &seriesLayout : plotLayout.series)
			{
				SeriesEntry series;
				series.sourceMode = seriesLayout.sourceMode;
				series.sourceId = seriesLayout.sourceId;
				series.title = seriesLayout.title;
				series.lower = seriesLayout.lower;
				series.upper = seriesLayout.upper;
				series.style = SeriesStyle{seriesLayout.style.color, seriesLayout.style.pointSize, seriesLayout.style.lineWeight, seriesLayout.style.drawMode, seriesLayout.style.marker, seriesLayout.style.visible};
				series.tunedStyle = SeriesStyle{seriesLayout.tunedStyle.color, seriesLayout.tunedStyle.pointSize, seriesLayout.tunedStyle.lineWeight, seriesLayout.tunedStyle.drawMode, seriesLayout.tunedStyle.marker, seriesLayout.tunedStyle.visible};
				for (const JFMArrheniusFitLayout &fitLayout : seriesLayout.fits)
				{
					FitLine fit;
					fit.name = fitLayout.name;
					fit.modelName = fitLayout.modelName;
					fit.model = fitLayout.model;
					fit.lower = fitLayout.lower;
					fit.upper = fitLayout.upper;
					fit.color = fitLayout.color;
					fit.lineWeight = fitLayout.lineWeight;
					fit.visible = fitLayout.visible;
					series.fits.push_back(std::move(fit));
				}
				window.series.push_back(std::move(series));
			}
			window.activeSeries = std::clamp(window.activeSeries, 0, std::max(0, static_cast<int>(window.series.size()) - 1));
			m_NextPlotId = std::max(m_NextPlotId, window.id + 1);
			if (window.open)
				m_ActivePlotId = window.id;
			m_PlotWindows.push_back(std::move(window));
		}
		m_RebuildDockLayout = true;
	}

	void ArrheniusPlotPanel::PersistLayout(ProjectHandle projectHandle) const
	{
		if (m_Store != nullptr && projectHandle != InvalidProjectHandle)
			m_Store->SetArrheniusLayout(projectHandle, CaptureLayoutDocument());
	}

	void ArrheniusPlotPanel::RenderDockspace()
	{
		ImVec2 available = ImGui::GetContentRegionAvail();
		available.y = std::max(420.0f, available.y);
		m_DockspaceId = ImGui::GetID("##arrhenius_dockspace");
		if (m_RebuildDockLayout && ImGui::DockBuilderGetNode(m_DockspaceId) == nullptr)
		{
			ImGui::DockBuilderAddNode(m_DockspaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(m_DockspaceId, available);

			ImGuiID top = m_DockspaceId;
			ImGuiID bottom = 0;
			ImGui::DockBuilderSplitNode(top, ImGuiDir_Down, 0.5f, &bottom, &top);

			ImGuiID topLeft = top;
			ImGuiID topMiddle = 0;
			ImGuiID topRight = 0;
			ImGui::DockBuilderSplitNode(topLeft, ImGuiDir_Right, 0.333f, &topRight, &topLeft);
			ImGui::DockBuilderSplitNode(topLeft, ImGuiDir_Right, 0.5f, &topMiddle, &topLeft);

			ImGuiID bottomLeft = bottom;
			ImGuiID bottomMiddle = 0;
			ImGuiID bottomRight = 0;
			ImGui::DockBuilderSplitNode(bottomLeft, ImGuiDir_Right, 0.333f, &bottomRight, &bottomLeft);
			ImGui::DockBuilderSplitNode(bottomLeft, ImGuiDir_Right, 0.5f, &bottomMiddle, &bottomLeft);

			m_DockIds = {topLeft, topMiddle, topRight, bottomLeft, bottomMiddle, bottomRight};
			ImGui::DockBuilderFinish(m_DockspaceId);
		}
		m_RebuildDockLayout = false;
		if (m_DockIds[0] == 0)
			m_DockIds.fill(m_DockspaceId);
		ImGui::DockSpace(m_DockspaceId, available, ImGuiDockNodeFlags_None);
	}

	void ArrheniusPlotPanel::RenderDockspaceHost()
	{
		if (m_PlotWindows.empty())
			return;
		ImGui::SetNextWindowSize(ImVec2(1200.0f, 760.0f), ImGuiCond_FirstUseEver);
		const bool contentVisible = ImGui::Begin("Arrhenius Plot Dock###arrhenius_plot_dock", nullptr, ImGuiWindowFlags_HorizontalScrollbar);
		m_DockspaceId = ImGui::GetID("##arrhenius_dockspace");
		if (contentVisible)
			RenderDockspace();
		else
			ImGui::DockSpace(m_DockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_KeepAliveOnly);
		ImGui::End();
	}

	void ArrheniusPlotPanel::RenderSelector(ProjectHandle projectHandle, const JFMProject &snapshot)
	{
		m_FitModel = std::clamp(m_FitModel, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1);
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
		ImGui::SameLine();
		ImGui::Checkbox("Tuned", &m_UseTuned);

		if (ImGui::Button("Add Plot", ImVec2(92.0f, 0.0f)))
			AddSelectedSource(projectHandle, snapshot, nullptr);
		ImGui::SameLine();
		if (ImGui::Button("Plot all", ImVec2(92.0f, 0.0f)))
			AddAllPlots(projectHandle, snapshot);
		ImGui::SameLine();
		if (ImGui::Button("Plot tuned", ImVec2(96.0f, 0.0f)))
		{
			const bool previous = m_UseTuned;
			m_UseTuned = true;
			AddAllPlots(projectHandle, snapshot);
			m_UseTuned = previous;
		}
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
		{
			for (auto &series : window.series) (void)series.rangeInteraction.Finish();
			return;
		}
		window.fitModel = std::clamp(window.fitModel, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1);

		bool open = window.open;
		const std::string title = window.title + "###arrhenius_plot_window_" + std::to_string(window.id);
		if (window.needsDock && m_DockspaceId != 0)
		{
			const std::size_t slot = static_cast<std::size_t>(std::clamp(window.dockSlot, 0, static_cast<int>(m_DockIds.size()) - 1));
			const ImGuiID target = m_DockIds[slot] != 0 ? m_DockIds[slot] : m_DockspaceId;
			ImGui::SetNextWindowDockID(target, ImGuiCond_Always);
			window.needsDock = false;
			window.needsInitialPlacement = false;
		}
		else if (window.needsInitialPlacement)
		{
			const ImGuiViewport *viewport = ImGui::GetMainViewport();
			const float width = std::max(420.0f, viewport->WorkSize.x / 3.0f - 10.0f);
			const float height = std::max(300.0f, viewport->WorkSize.y * 0.42f);
			const int column = window.initialPlacement % 3;
			const int row = window.initialPlacement / 3;
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + column * (width + 8.0f), viewport->WorkPos.y + row * (height + 28.0f)), ImGuiCond_Once);
			ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Once);
			window.needsInitialPlacement = false;
		}
		if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_HorizontalScrollbar))
		{
			for (auto &series : window.series) (void)series.rangeInteraction.Finish();
			ImGui::End();
			window.open = open;
			return;
		}
		window.open = open;
		m_ActivePlotId = window.id;

		auto addFitToSeries = [&](std::size_t index) {
			if (index >= window.series.size())
				return false;
			SeriesEntry &series = window.series[index];
			const JFMArrheniusQuantity activeQuantity = ToQuantity(window.quantity);
			const std::vector<JFMArrheniusPoint> points = PointsForEntry(projectHandle, snapshot, series, activeQuantity, window.preferFit, false);
			if (points.size() < 2)
				return false;
			if (auto fit = BuildFit(points, series.lower, series.upper, activeQuantity, window.fitModel, series.fits.size()))
			{
				fit->color = series.style.color;
				fit->lineWeight = std::max(2.0f, series.style.lineWeight + 1.0f);
				series.fits.push_back(*fit);
				return true;
			}
			return false;
		};

		if (ImGui::BeginChild(("##arrhenius_controls_" + std::to_string(window.id)).c_str(), ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() * 1.8f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::SetNextItemWidth(190.0f);
			ImGui::Combo("Plot", &window.quantity, QuantityLabels, IM_ARRAYSIZE(QuantityLabels));
			ImGui::SameLine();
			ImGui::SetNextItemWidth(95.0f);
			ImGui::Combo("Fit", &window.fitModel, FitModelLabels, IM_ARRAYSIZE(FitModelLabels));
			ImGui::SameLine();
			ImGui::Checkbox("Prefer fit", &window.preferFit);
			ImGui::SameLine();
			bool showTuned = !window.series.empty() && std::all_of(window.series.begin(), window.series.end(), [](const SeriesEntry &series) {
				return series.tunedStyle.visible;
			});
			if (ImGui::Checkbox("Show tuned", &showTuned))
			{
				for (SeriesEntry &series : window.series)
					series.tunedStyle.visible = showTuned;
			}
			ImGui::SameLine();
			ImGui::Checkbox("Auto refit", &window.autoRefit);
			ImGui::SameLine();
			ImGui::BeginDisabled(window.series.empty());
			if (ImGui::Button("Fit active", ImVec2(82.0f, 0.0f)))
				(void)addFitToSeries(static_cast<std::size_t>(std::clamp(window.activeSeries, 0, std::max(0, static_cast<int>(window.series.size()) - 1))));
			ImGui::EndDisabled();
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
			if (ImGui::Button("Add source", ImVec2(150.0f, 0.0f)))
				AddSelectedSource(projectHandle, snapshot, &window);
			ImGui::SameLine();
			if (ImGui::Button("Fit columns", ImVec2(110.0f, 0.0f)))
				window.fitColumnsNextFrame = true;
		}
		ImGui::EndChild();
		const JFMArrheniusQuantity quantity = ToQuantity(window.quantity);
		const bool fitColumns = window.fitColumnsNextFrame;

		int removeSeries = -1;
		auto renderSeriesTable = [&]() {
			std::array<float, 9> seriesWidths{64.0f, 58.0f, 70.0f, 160.0f, 78.0f, 145.0f, 165.0f, 165.0f, 95.0f};
			if (fitColumns)
			{
				for (const SeriesEntry &series : window.series)
					GrowColumn(seriesWidths, 3, series.title);
			}
			if (ImGui::BeginTable(("##arrhenius_series_table_" + std::to_string(window.id)).c_str(), 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX, ImVec2(-1.0f, TableHeightForRows(window.series.size()))))
			{
				ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 58.0f);
				ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 45.0f);
				ImGui::TableSetupColumn("Tuned", ImGuiTableColumnFlags_WidthFixed, 58.0f);
				ImGui::TableSetupColumn("Source");
				ImGui::TableSetupColumn("Color", ImGuiTableColumnFlags_WidthFixed, 72.0f);
				ImGui::TableSetupColumn("Style", ImGuiTableColumnFlags_WidthFixed, 115.0f);
				ImGui::TableSetupColumn("Lower", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableSetupColumn("Upper", ImGuiTableColumnFlags_WidthFixed, 150.0f);
				ImGui::TableSetupColumn("Fit", ImGuiTableColumnFlags_WidthFixed, 110.0f);
				if (fitColumns)
					ApplyColumnWidths(seriesWidths);
				ImGui::TableHeadersRow();

				for (std::size_t index = 0; index < window.series.size(); ++index)
				{
					SeriesEntry &series = window.series[index];
					const std::vector<JFMArrheniusPoint> points = PointsForEntry(projectHandle, snapshot, series, quantity, window.preferFit, false);
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
					if (ImGui::RadioButton("##active", window.activeSeries == static_cast<int>(index)))
						window.activeSeries = static_cast<int>(index);
					ImGui::TableSetColumnIndex(1);
					ImGui::Checkbox("##show", &series.style.visible);
					ImGui::TableSetColumnIndex(2);
					ImGui::Checkbox("##tuned", &series.tunedStyle.visible);
					ImGui::TableSetColumnIndex(3);
					if (ImGui::Selectable(series.title.c_str(), window.activeSeries == static_cast<int>(index)))
						window.activeSeries = static_cast<int>(index);
					ImGui::TableSetColumnIndex(4);
					ImGui::ColorEdit4("##color", series.style.color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
					ImGui::TableSetColumnIndex(5);
					ImGui::SetNextItemWidth(-1.0f);
					ImGui::Combo("##style", &series.style.drawMode, DrawModeLabels, IM_ARRAYSIZE(DrawModeLabels));
					ImGui::TableSetColumnIndex(6);
					ImGui::SetNextItemWidth(-1.0f);
					rangeChanged |= !points.empty() && ImGui::DragInt("##lower", &series.lower, 0.05f, 0, std::max(0, static_cast<int>(points.size()) - 1), "%d", ImGuiSliderFlags_AlwaysClamp);
					ImGui::TableSetColumnIndex(7);
					ImGui::SetNextItemWidth(-1.0f);
					rangeChanged |= !points.empty() && ImGui::DragInt("##upper", &series.upper, 0.05f, 0, std::max(0, static_cast<int>(points.size()) - 1), "%d", ImGuiSliderFlags_AlwaysClamp);
					if (!points.empty())
					{
						series.lower = std::clamp(series.lower, 0, static_cast<int>(points.size()) - 1);
						series.upper = std::clamp(series.upper, 0, static_cast<int>(points.size()) - 1);
					}
					if (series.upper < series.lower)
						series.upper = series.lower;
					ImGui::TableSetColumnIndex(8);
					ImGui::BeginDisabled(points.size() < 2);
					if (ImGui::Button("Add"))
					{
						window.activeSeries = static_cast<int>(index);
						(void)addFitToSeries(index);
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
						removeSeries = static_cast<int>(index);

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
		};
		renderSeriesTable();
		JFMPlotExportControls(*window.document);
		const bool plotFont = BeginJFMPlotFrame(*window.document);
		if (plotFont && ImPlot::BeginPlot(("##arrhenius_plot_" + std::to_string(window.id)).c_str(), ImVec2(-1.0f, 360.0f)))
		{
			ImPlot::SetupAxes(JFMArrheniusUsesInvTemperature(quantity) ? "1000/T" : "T (K)", JFMArrheniusQuantityName(quantity));
			bool rangeCursor = false;

			for (std::size_t seriesIndex = 0; seriesIndex < window.series.size(); ++seriesIndex)
			{
				SeriesEntry &series = window.series[seriesIndex];
				const std::vector<JFMArrheniusPoint> points = PointsForEntry(projectHandle, snapshot, series, quantity, window.preferFit, false);
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

				const std::string sourceKey = std::to_string(series.sourceMode) + "_" + series.sourceId;
				const std::string label = series.title + "###arrhenius_data_" + sourceKey;
				auto plotPoints = [&](const std::string &plotLabel, const std::vector<double> &x, const std::vector<double> &y, SeriesStyle &style) {
					ImPlotSpec spec;
					spec.LineColor = ToVec4(style.color);
					spec.MarkerFillColor = ToVec4(style.color);
					spec.MarkerLineColor = ToVec4(style.color);
					spec.Marker = sst::ui::plot::ImPlotMarkerFor(sst::ui::plot::MarkerForIndex(style.marker));
					spec.MarkerSize = style.pointSize;
					spec.LineWeight = style.lineWeight;
					const auto kind = style.drawMode == 1 ? sst::ui::plot::SeriesKind::Line : sst::ui::plot::SeriesKind::Scatter;
					AddJFMPlotSeries(*window.document, {plotLabel, {x.data(), y.data(), x.size()}, kind, sst::ui::plot::FromImPlotStyle(spec)});
					if (style.drawMode != 0 && style.drawMode != 1)
					{
						// Preserve JFM's overlay ID and draw order for existing legends and markers.
						spec.Flags = ImPlotItemFlags_NoLegend;
						AddJFMPlotSeries(*window.document, {"##line_" + plotLabel.substr(plotLabel.find("###") + 3), {x.data(), y.data(), x.size()}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(spec)});
					}
				};
				if (series.style.visible && !excludedX.empty())
				{
					ImPlotSpec excludedSpec;
					excludedSpec.LineColor = ImVec4(0.42f, 0.42f, 0.42f, 0.65f);
					excludedSpec.MarkerFillColor = ImVec4(0.42f, 0.42f, 0.42f, 0.65f);
					excludedSpec.MarkerLineColor = ImVec4(0.42f, 0.42f, 0.42f, 0.65f);
					excludedSpec.Marker = sst::ui::plot::ImPlotMarkerFor(sst::ui::plot::MarkerForIndex(series.style.marker));
					excludedSpec.MarkerSize = std::max(2.0f, series.style.pointSize - 1.0f);
					excludedSpec.LineWeight = 0.0f;
					excludedSpec.Flags = ImPlotItemFlags_NoLegend;
					AddJFMPlotSeries(*window.document, {("##excluded_" + sourceKey).c_str(), {excludedX.data(), excludedY.data(), static_cast<std::size_t>(excludedX.size())}, sst::ui::plot::SeriesKind::Scatter, sst::ui::plot::FromImPlotStyle(excludedSpec)});
				}

				if (series.style.visible && !includedX.empty())
				{
					if (hasUncertainty)
					{
						ImPlotSpec errorSpec;
						errorSpec.LineColor = ToVec4(series.style.color);
						errorSpec.Size = 10.0f;
						errorSpec.LineWeight = 2.4f;
						errorSpec.Flags = ImPlotItemFlags_NoLegend;
						AddJFMPlotSeries(*window.document, {("##arrhenius_err_" + sourceKey).c_str(), {includedX.data(), includedY.data(), static_cast<std::size_t>(includedX.size()), includedErr.data()}, sst::ui::plot::SeriesKind::ErrorBars, sst::ui::plot::FromImPlotStyle(errorSpec)});
					}
					plotPoints(label, includedX, includedY, series.style);
				}

				const std::vector<JFMArrheniusPoint> tunedPoints = series.tunedStyle.visible ? PointsForEntry(projectHandle, snapshot, series, quantity, window.preferFit, true) : std::vector<JFMArrheniusPoint>{};
				if (!tunedPoints.empty())
				{
					std::vector<double> tunedX;
					std::vector<double> tunedY;
					tunedX.reserve(tunedPoints.size());
					tunedY.reserve(tunedPoints.size());
					for (const JFMArrheniusPoint &point : tunedPoints)
					{
						tunedX.push_back(point.x);
						tunedY.push_back(point.y);
					}
					const std::string tunedLabel = series.title + " tuned###arrhenius_tuned_" + sourceKey;
					plotPoints(tunedLabel, tunedX, tunedY, series.tunedStyle);
					if (ImPlot::BeginLegendPopup(tunedLabel.c_str()))
					{
						ImGui::Checkbox("Show", &series.tunedStyle.visible);
						ImGui::ColorEdit4("Color", series.tunedStyle.color.data(), ImGuiColorEditFlags_AlphaBar);
						ImGui::SliderFloat("Point size", &series.tunedStyle.pointSize, 1.0f, 12.0f, "%.1f");
						ImGui::SliderFloat("Line", &series.tunedStyle.lineWeight, 0.5f, 6.0f, "%.1f");
						ImGui::Combo("Type", &series.tunedStyle.drawMode, DrawModeLabels, IM_ARRAYSIZE(DrawModeLabels));
						ImGui::Combo("Marker", &series.tunedStyle.marker, MarkerLabels, IM_ARRAYSIZE(MarkerLabels));
						ImPlot::EndLegendPopup();
					}
				}

				double lowerX = points[static_cast<std::size_t>(series.lower)].x;
				double upperX = points[static_cast<std::size_t>(series.upper)].x;
				const bool activeRange = window.activeSeries == static_cast<int>(seriesIndex);
				sst::ui::plot::RangeStyle rangeStyle;
				rangeStyle.color = {1.0f, 0.55f, 0.0f, activeRange ? 1.0f : 0.35f};
				rangeStyle.widthPt = activeRange ? 2.2f : 1.1f;
				// Point indices follow the domain ordering, including descending 1000/T.
				rangeStyle.orderEndpoints = false;
				const int dragBase = static_cast<int>((window.id % 100000) * 100 + seriesIndex * 2);
				const auto interaction = sst::ui::plot::DragRangeX(dragBase + 1, dragBase + 2,
					lowerX, upperX, series.rangeInteraction, rangeStyle);
				rangeCursor |= interaction.hovered;
				const bool lowerChanged = interaction.lowerChanged;
				const bool upperChanged = interaction.upperChanged;
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
					if (!fit.visible)
						continue;
					if (window.autoRefit || fit.fitX.empty())
						(void)Refit(fit, points, quantity);
					const int lower = std::clamp(fit.lower, 0, static_cast<int>(points.size()) - 1);
					const int upper = std::clamp(fit.upper, lower, static_cast<int>(points.size()) - 1);
					const double xx[2] = {points[static_cast<std::size_t>(lower)].x, points[static_cast<std::size_t>(upper)].x};
					const double yy[2] = {fit.intercept + fit.slope * xx[0], fit.intercept + fit.slope * xx[1]};
					ImPlotSpec fitSpec;
					fitSpec.LineColor = ToVec4(fit.color);
					fitSpec.LineWeight = fit.lineWeight;
					if (!fit.plotKey) fit.plotKey = window.document->project.reserveId();
					const std::string fitLabel = series.title + " " + fit.name + "###arrhenius_fit_" + std::to_string(fit.plotKey);
					if (fit.fitX.size() == fit.fitY.size() && fit.fitX.size() >= 2)
						AddJFMPlotSeries(*window.document, {fitLabel.c_str(), {fit.fitX.data(), fit.fitY.data(), static_cast<std::size_t>(fit.fitX.size())}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(fitSpec)});
					else
						AddJFMPlotSeries(*window.document, {fitLabel.c_str(), {xx, yy, static_cast<std::size_t>(2)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(fitSpec)});
					if (ImPlot::BeginLegendPopup(fitLabel.c_str()))
					{
						ImGui::Checkbox("Show", &fit.visible);
						ImGui::ColorEdit4("Color", fit.color.data(), ImGuiColorEditFlags_AlphaBar);
						ImGui::SliderFloat("Line", &fit.lineWeight, 0.5f, 8.0f, "%.1f");
						ImPlot::EndLegendPopup();
					}
				}

				if (ImPlot::BeginLegendPopup(label.c_str()))
				{
					ImGui::Checkbox("Show", &series.style.visible);
					ImGui::ColorEdit4("Color", series.style.color.data(), ImGuiColorEditFlags_AlphaBar);
					ImGui::SliderFloat("Point size", &series.style.pointSize, 1.0f, 12.0f, "%.1f");
					ImGui::SliderFloat("Line", &series.style.lineWeight, 0.5f, 6.0f, "%.1f");
					ImGui::Combo("Type", &series.style.drawMode, DrawModeLabels, IM_ARRAYSIZE(DrawModeLabels));
					ImGui::Combo("Marker", &series.style.marker, MarkerLabels, IM_ARRAYSIZE(MarkerLabels));
					ImPlot::EndLegendPopup();
				}
			}
			sst::ui::plot::PlotSpec figureSpec;
			figureSpec.x.label = JFMArrheniusUsesInvTemperature(quantity) ? "1000/T" : "T (K)";
			figureSpec.y.label = JFMArrheniusQuantityName(quantity);
			EndJFMPlot(*window.document, figureSpec);
			if (rangeCursor)
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}
		else
		{
			for (auto &series : window.series) (void)series.rangeInteraction.Finish();
		}
		if (plotFont) ImGui::PopFont();

		if (removeSeries >= 0 && removeSeries < static_cast<int>(window.series.size()))
		{
			window.series.erase(window.series.begin() + removeSeries);
			if (window.series.empty())
				window.open = false;
			else
				window.activeSeries = std::clamp(window.activeSeries, 0, static_cast<int>(window.series.size()) - 1);
		}

		constexpr const char *FitHeaders[] = {"Series", "Fit", "Model", "Range", "Slope", "Intercept", "E_A [eV]", "E_g [eV]", "E_00 [eV]", "E* [eV]", "J_00", "I_00", "T*", "Action"};
		std::array<float, 14> fitWidths{};
		for (std::size_t index = 0; index < fitWidths.size(); ++index)
			GrowColumn(fitWidths, index, FitHeaders[index]);
		if (fitColumns)
		{
			for (const SeriesEntry &series : window.series)
			{
				for (const FitLine &fit : series.fits)
				{
					GrowColumn(fitWidths, 0, series.title);
					GrowColumn(fitWidths, 1, fit.name);
					GrowColumn(fitWidths, 2, fit.modelName.empty() ? FitModelLabels[std::clamp(fit.model, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1)] : fit.modelName);
					GrowColumn(fitWidths, 3, std::to_string(fit.lower) + "-" + std::to_string(fit.upper));
					GrowColumn(fitWidths, 4, ValueText(fit.slope, fit.hasSlopeStd, fit.slopeStd));
					GrowColumn(fitWidths, 5, ValueText(fit.intercept, fit.hasInterceptStd, fit.interceptStd));
					if (fit.hasEa)
						GrowColumn(fitWidths, 6, ValueText(fit.eaEv, fit.hasEaStd, fit.eaStd));
					if (fit.hasEg)
						GrowColumn(fitWidths, 7, ValueText(fit.egEv, fit.hasEgStd, fit.egStd));
					if (fit.hasE00)
						GrowColumn(fitWidths, 8, ValueText(fit.e00, fit.hasE00Std, fit.e00Std));
					if (fit.hasEStar)
						GrowColumn(fitWidths, 9, ValueText(fit.eStar, fit.hasEStarStd, fit.eStarStd));
					if (fit.hasJ00)
						GrowColumn(fitWidths, 10, ValueText(fit.j00, fit.hasJ00Std, fit.j00Std));
					if (fit.hasI00)
						GrowColumn(fitWidths, 11, ValueText(fit.i00, fit.hasI00Std, fit.i00Std));
					if (fit.hasTStar)
						GrowColumn(fitWidths, 12, ValueText(fit.tStar, fit.hasTStarStd, fit.tStarStd));
					GrowColumn(fitWidths, 13, "Remove");
				}
			}
		}
		std::size_t fitRows = 0;
		for (const SeriesEntry &series : window.series)
			fitRows += series.fits.size();
		if (ImGui::BeginTable(("##arrhenius_fit_table_" + std::to_string(window.id)).c_str(), 14, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX, ImVec2(-1.0f, TableHeightForRows(fitRows))))
		{
			for (const char *header : FitHeaders)
				ImGui::TableSetupColumn(header);
			if (fitColumns)
				ApplyColumnWidths(fitWidths);
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
					ImGui::TextUnformatted(fit.modelName.empty() ? FitModelLabels[std::clamp(fit.model, 0, static_cast<int>(IM_ARRAYSIZE(FitModelLabels)) - 1)] : fit.modelName.c_str());
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%d-%d", fit.lower, fit.upper);
					ImGui::TableSetColumnIndex(4);
					TextValue(fit.slope, fit.hasSlopeStd, fit.slopeStd);
					ImGui::TableSetColumnIndex(5);
					TextValue(fit.intercept, fit.hasInterceptStd, fit.interceptStd);
					ImGui::TableSetColumnIndex(6);
					if (fit.hasEa)
						TextValue(fit.eaEv, fit.hasEaStd, fit.eaStd);
					ImGui::TableSetColumnIndex(7);
					if (fit.hasEg)
						TextValue(fit.egEv, fit.hasEgStd, fit.egStd);
					ImGui::TableSetColumnIndex(8);
					if (fit.hasE00)
						TextValue(fit.e00, fit.hasE00Std, fit.e00Std);
					ImGui::TableSetColumnIndex(9);
					if (fit.hasEStar)
						TextValue(fit.eStar, fit.hasEStarStd, fit.eStarStd);
					ImGui::TableSetColumnIndex(10);
					if (fit.hasJ00)
						TextValue(fit.j00, fit.hasJ00Std, fit.j00Std);
					ImGui::TableSetColumnIndex(11);
					if (fit.hasI00)
						TextValue(fit.i00, fit.hasI00Std, fit.i00Std);
					ImGui::TableSetColumnIndex(12);
					if (fit.hasTStar)
						TextValue(fit.tStar, fit.hasTStarStd, fit.tStarStd);
					ImGui::TableSetColumnIndex(13);
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

		window.fitColumnsNextFrame = false;
		ImGui::End();
	}

	void ArrheniusPlotPanel::Render()
	{
        if (m_Store == nullptr) {
            if (!IsVisible()) return;
            bool visible = IsVisible();
            if (ImGui::Begin(GetTitle().c_str(), &visible)) ImGui::TextDisabled("Workspace unavailable");
            ImGui::End(); SetVisible(visible); return;
        }

		const ProjectHandle projectHandle = m_Store->GetActiveProjectHandle();
		if (projectHandle != m_LayoutProject)
		{
			m_PlotWindows.clear();
			m_NextPlotId = 1;
			m_ActivePlotId = 0;
			m_RebuildDockLayout = true;
			if (projectHandle != InvalidProjectHandle)
			{
				if (auto layout = m_Store->GetArrheniusLayout(projectHandle))
					RestoreLayoutDocument(*layout);
			}
			m_LayoutProject = projectHandle;
		}
		const JFMProject snapshot = m_Store->GetProjectSnapshot(projectHandle);
        if (IsVisible()) {
            bool visible = IsVisible();
            if (ImGui::Begin(GetTitle().c_str(), &visible, ImGuiWindowFlags_HorizontalScrollbar)) {
				RenderSelector(projectHandle, snapshot);
				ImGui::Separator();
				if (m_PlotWindows.empty())
					ImGui::TextDisabled("Choose a source and plot type, then Add Plot.");
				else if (ImGui::BeginTable("##arrhenius_windows", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
				{
					ImGui::TableSetupColumn("Plot");
					ImGui::TableSetupColumn("Series", ImGuiTableColumnFlags_WidthFixed, 70.0f);
					ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 145.0f);
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
						ImGui::SameLine();
						if (ImGui::Button(("Remove##arrhenius_remove_" + std::to_string(window.id)).c_str()))
							window.open = false;
					}
					ImGui::EndTable();
				}
            }
            ImGui::End(); SetVisible(visible);
        }

		RenderDockspaceHost();
		for (PlotWindow &window : m_PlotWindows)
			RenderPlotWindow(projectHandle, snapshot, window);
		m_PlotWindows.erase(std::remove_if(m_PlotWindows.begin(), m_PlotWindows.end(), [](const PlotWindow &window) {
			return !window.open;
		}), m_PlotWindows.end());
		PersistLayout(projectHandle);
	}
} // namespace SST::Prototypes::JFM
