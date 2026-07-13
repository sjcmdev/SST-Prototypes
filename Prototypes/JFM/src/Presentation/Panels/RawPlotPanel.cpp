#include "Core/dspch.hpp"

#include "Presentation/Panels/RawPlotPanel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>
#include <implot.h>

namespace SST::Prototypes::JFM
{
	namespace
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

		ImVec4 colorFor(const JFMCharacteristicInfo &info)
		{
			const float hue = static_cast<float>(std::fmod(static_cast<double>((info.id * 37) % 997) / 997.0, 1.0));
			return ImColor::HSV(hue, 0.78f, 0.96f).Value;
		}

		std::array<float, 4> toArray(const ImVec4 &color)
		{
			return {color.x, color.y, color.z, color.w};
		}

		ImVec4 toVec4(const std::array<float, 4> &color)
		{
			return {color[0], color[1], color[2], color[3]};
		}

		void appendUnique(std::vector<CharacteristicId> &target, const std::vector<CharacteristicId> &ids)
		{
			for (CharacteristicId id : ids)
			{
				if (id != InvalidCharacteristicId && std::find(target.begin(), target.end(), id) == target.end())
					target.push_back(id);
			}
		}

		bool containsId(const std::vector<CharacteristicId> &ids, CharacteristicId id)
		{
			return std::find(ids.begin(), ids.end(), id) != ids.end();
		}

		void setHidden(std::vector<CharacteristicId> &ids, CharacteristicId id, bool hidden)
		{
			ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
			if (hidden)
				ids.push_back(id);
		}

		std::string shortName(const JFMCharacteristicInfo &info)
		{
			if (const auto it = info.metadata.find("short_name"); it != info.metadata.end() && !it->second.empty())
				return it->second;
			const std::size_t separator = info.name.find('_');
			if (separator != std::string::npos && separator > 0 && separator <= 16)
				return info.name.substr(0, separator);
			if (info.name.size() > 32)
				return info.name.substr(0, 29) + "...";
			return info.name.empty() ? std::to_string(info.id) : info.name;
		}

		void includeBounds(double x, double y, bool &hasBounds, double &minX, double &maxX, double &minY, double &maxY)
		{
			if (!std::isfinite(x) || !std::isfinite(y))
				return;
			if (!hasBounds)
			{
				minX = maxX = x;
				minY = maxY = y;
				hasBounds = true;
				return;
			}
			minX = std::min(minX, x);
			maxX = std::max(maxX, x);
			minY = std::min(minY, y);
			maxY = std::max(maxY, y);
		}
	}

	RawPlotPanel::RawPlotPanel(Ref<JFMWorkspaceStore> store, Ref<JFMPlotRequestState> plotRequests, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Store(std::move(store)), m_PlotRequests(std::move(plotRequests))
	{
		m_PlotAreas.push_back(CreatePlotArea());
	}

	Ref<IPanel> RawPlotPanel::Clone() const
	{
		return CreateRef<RawPlotPanel>(*this);
	}

	RawPlotPanel::PlotArea RawPlotPanel::CreatePlotArea()
	{
		PlotArea area;
		area.id = m_NextPlotAreaId++;
		area.title = "Raw Plot " + std::to_string(area.id);
		return area;
	}

	void RawPlotPanel::PlotIds(std::vector<CharacteristicId> ids)
	{
		if (ids.empty())
			return;
		PlotArea area = CreatePlotArea();
		appendUnique(area.characteristicIds, ids);
		m_PlotAreas.push_back(std::move(area));
	}

	void RawPlotPanel::PlotSeries(const std::string &seriesId)
	{
		if (m_Store == nullptr || seriesId.empty())
			return;
		std::vector<CharacteristicId> ids;
		for (const JFMCharacteristic &characteristic : m_Store->GetSeriesCharacteristics(seriesId))
			ids.push_back(characteristic.info.id);
		PlotIds(std::move(ids));
	}

	void RawPlotPanel::PlotSelectedGroup()
	{
		if (m_Store == nullptr)
			return;
		const std::string selectedSeriesId = m_Store->GetSelectedSeriesId();
		const JFMProject project = m_Store->GetProjectSnapshot();
		const auto seriesIt = std::find_if(project.series.begin(), project.series.end(), [&selectedSeriesId](const JFMSeries &series) {
			return series.id == selectedSeriesId;
		});
		if (seriesIt == project.series.end() || seriesIt->groupId.empty())
			return;

		std::vector<CharacteristicId> ids;
		for (const JFMCharacteristic &characteristic : m_Store->GetGroupCharacteristics(seriesIt->groupId))
			ids.push_back(characteristic.info.id);
		PlotIds(std::move(ids));
	}

	void RawPlotPanel::Render()
	{
		if (!IsVisible())
			return;

		bool visible = IsVisible();
		const std::string managerTitle = GetTitle() + " Manager###jfm_raw_plot_manager";
		if (!ImGui::Begin(managerTitle.c_str(), &visible))
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
		if (m_PlotRequests != nullptr)
		{
			for (std::vector<CharacteristicId> ids : m_PlotRequests->ConsumeRequests())
				PlotIds(std::move(ids));
		}

		if (ImGui::Button("+ Empty Plot"))
			m_PlotAreas.push_back(CreatePlotArea());
		ImGui::SameLine();
		if (ImGui::Button("Plot Selection"))
			PlotIds(m_Store->GetSelectedCharacteristicIds());
		ImGui::SameLine();
		if (ImGui::Button("Plot Series"))
			PlotSeries(m_Store->GetSelectedSeriesId());
		ImGui::SameLine();
		if (ImGui::Button("Plot Group"))
			PlotSelectedGroup();
		ImGui::TextDisabled("Plot windows: %d", static_cast<int>(m_PlotAreas.size()));
		ImGui::End();
		SetVisible(visible);
		if (!visible)
			return;

		if (m_PlotAreas.empty())
			m_PlotAreas.push_back(CreatePlotArea());

		for (std::size_t areaIndex = 0; areaIndex < m_PlotAreas.size();)
		{
			PlotArea &area = m_PlotAreas[areaIndex];
			area.characteristicIds.erase(
				std::remove_if(
					area.characteristicIds.begin(),
					area.characteristicIds.end(),
					[this](CharacteristicId id) { return !m_Store->FindCharacteristic(id).has_value(); }),
				area.characteristicIds.end());
			area.hiddenCharacteristicIds.erase(
				std::remove_if(
					area.hiddenCharacteristicIds.begin(),
					area.hiddenCharacteristicIds.end(),
					[&area](CharacteristicId id) { return !containsId(area.characteristicIds, id); }),
				area.hiddenCharacteristicIds.end());

			bool plotVisible = true;
			const std::string windowTitle = area.title + "###jfm_raw_plot_window_" + std::to_string(area.id);
			if (!ImGui::Begin(windowTitle.c_str(), &plotVisible))
			{
				ImGui::End();
				if (!plotVisible)
					m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
				else
					++areaIndex;
				continue;
			}

			ImGui::PushID(static_cast<int>(area.id));
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2))
			{
				area.renaming = true;
				std::snprintf(area.renameBuffer.data(), area.renameBuffer.size(), "%s", area.title.c_str());
			}
			if (area.renaming)
			{
				ImGui::SetNextItemWidth(260.0f);
				if (ImGui::InputText("##plot_rename", area.renameBuffer.data(), area.renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
				{
					if (area.renameBuffer[0] != '\0')
						area.title = area.renameBuffer.data();
					area.renaming = false;
				}
				ImGui::SameLine();
				if (ImGui::Button("OK"))
				{
					if (area.renameBuffer[0] != '\0')
						area.title = area.renameBuffer.data();
					area.renaming = false;
				}
			}
			if (ImGui::Button("Use Selection"))
				appendUnique(area.characteristicIds, m_Store->GetSelectedCharacteristicIds());
			ImGui::SameLine();
			if (ImGui::Button("Clear"))
				area.characteristicIds.clear();
			ImGui::SameLine();
			if (m_PlotAreas.size() > 1 && ImGui::Button("Close Plot"))
			{
				ImGui::PopID();
				ImGui::End();
				m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
				continue;
			}

			int scale = static_cast<int>(area.scale);
			ImGui::RadioButton("Linear", &scale, static_cast<int>(PlotScale::Linear));
			ImGui::SameLine();
			ImGui::RadioButton("Semi-log", &scale, static_cast<int>(PlotScale::SemiLog));
			ImGui::SameLine();
			ImGui::Checkbox("Auto fit", &area.autoFit);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(140.0f);
			ImGui::SliderFloat("Scroll out", &area.fitPadding, 0.0f, 0.5f, "%.2f");
			ImGui::SameLine();
			ImGui::Checkbox("Legend", &area.showLegend);
			area.scale = static_cast<PlotScale>(scale);
			ImGui::SameLine();
			ImGui::ColorEdit4("Start", area.gradientStart.data(), ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine();
			ImGui::ColorEdit4("End", area.gradientEnd.data(), ImGuiColorEditFlags_NoInputs);
			ImGui::SameLine();
			if (ImGui::Button("Apply Colors"))
			{
				const std::size_t count = std::max<std::size_t>(area.characteristicIds.size(), 1);
				for (std::size_t index = 0; index < area.characteristicIds.size(); ++index)
				{
					const float t = count == 1 ? 0.0f : static_cast<float>(index) / static_cast<float>(count - 1);
					std::array<float, 4> color{};
					for (std::size_t component = 0; component < color.size(); ++component)
						color[component] = area.gradientStart[component] + (area.gradientEnd[component] - area.gradientStart[component]) * t;
					m_ColorOverrides[area.characteristicIds[index]] = color;
				}
			}

			ImGui::BeginChild("##curves", ImVec2(360.0f, 0.0f), true);
			ImGui::TextDisabled("Curves");
			for (std::size_t idIndex = 0; idIndex < area.characteristicIds.size();)
			{
				const CharacteristicId id = area.characteristicIds[idIndex];
				const auto characteristic = m_Store->FindCharacteristic(id);
				ImGui::PushID(static_cast<int>(id));
				if (ImGui::SmallButton("x"))
				{
					area.characteristicIds.erase(area.characteristicIds.begin() + static_cast<std::ptrdiff_t>(idIndex));
					ImGui::PopID();
					continue;
				}
				ImGui::SameLine();
				std::array<float, 4> &color = m_ColorOverrides.try_emplace(id, toArray(colorFor(characteristic ? characteristic->info : JFMCharacteristicInfo{id}))).first->second;
				ImGui::ColorEdit4("##color", color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
				ImGui::SameLine();
				bool shown = !containsId(area.hiddenCharacteristicIds, id);
				if (ImGui::Checkbox("##visible", &shown))
					setHidden(area.hiddenCharacteristicIds, id, !shown);
				ImGui::SameLine();
				const std::string label = characteristic ? shortName(characteristic->info) : std::to_string(id);
				ImGui::Selectable(label.c_str(), false);
				if (characteristic && ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", characteristic->info.name.c_str());
				ImGui::PopID();
				++idIndex;
			}

			if (area.characteristicIds.empty())
				ImGui::TextDisabled("Drop characteristics here or use selection.");
			ImGui::EndChild();
			ImGui::SameLine();

			const std::string plotId = "##jfm_raw_plot_" + std::to_string(area.id);
			const float plotHeight = std::max(360.0f, ImGui::GetContentRegionAvail().y);
			const ImPlotFlags plotFlags = area.showLegend ? ImPlotFlags_None : ImPlotFlags_NoLegend;
			if (ImPlot::BeginPlot(plotId.c_str(), ImVec2(-1.0f, plotHeight), plotFlags))
			{
				ImPlot::SetupAxes("Voltage", "Current");
				if (area.showLegend)
					ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside | ImPlotLegendFlags_NoButtons);
				if (area.scale == PlotScale::SemiLog)
					ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

				bool hasBounds = false;
				double minX = 0.0;
				double maxX = 0.0;
				double minY = 0.0;
				double maxY = 0.0;
				for (CharacteristicId id : area.characteristicIds)
				{
					if (containsId(area.hiddenCharacteristicIds, id))
						continue;
					auto characteristic = m_Store->FindCharacteristic(id);
					if (!characteristic)
						continue;
					const std::vector<double> &x = characteristic->data.voltage;
					const std::vector<double> &rawY = selectYValues(*characteristic);
					for (std::size_t index = 0; index < x.size() && index < rawY.size(); ++index)
					{
						const double y = area.scale == PlotScale::SemiLog ? std::abs(rawY[index]) : rawY[index];
						if (area.scale == PlotScale::SemiLog && y <= 0.0)
							continue;
						includeBounds(x[index], y, hasBounds, minX, maxX, minY, maxY);
					}
				}
				if (area.autoFit && hasBounds)
				{
					double dx = maxX - minX;
					double dy = maxY - minY;
					if (dx <= 0.0)
						dx = std::max(1.0, std::abs(maxX) * 0.1);
					if (dy <= 0.0)
						dy = std::max(1.0, std::abs(maxY) * 0.1);
					minX -= dx * area.fitPadding;
					maxX += dx * area.fitPadding;
					if (area.scale == PlotScale::SemiLog)
					{
						minY = std::max(1.0e-30, minY / (1.0 + area.fitPadding * 4.0));
						maxY *= 1.0 + area.fitPadding * 4.0;
					}
					else
					{
						minY -= dy * area.fitPadding;
						maxY += dy * area.fitPadding;
					}
					ImPlot::SetupAxesLimits(minX, maxX, minY, maxY, ImPlotCond_Always);
				}

				for (CharacteristicId id : area.characteristicIds)
				{
					if (containsId(area.hiddenCharacteristicIds, id))
						continue;
					auto characteristic = m_Store->FindCharacteristic(id);
					if (!characteristic)
						continue;
					const std::vector<double> &x = characteristic->data.voltage;
					const std::vector<double> &rawY = selectYValues(*characteristic);
					const std::size_t count = std::min(x.size(), rawY.size());
					if (count == 0)
						continue;

					std::array<float, 4> &color = m_ColorOverrides.try_emplace(id, toArray(colorFor(characteristic->info))).first->second;
					const ImPlotSpec spec(ImPlotProp_LineColor, toVec4(color));
					const std::string curveLabel = shortName(characteristic->info) + "###curve_" + std::to_string(id);
					if (area.scale == PlotScale::Linear)
					{
						ImPlot::PlotLine(curveLabel.c_str(), x.data(), rawY.data(), static_cast<int>(count), spec);
					}
					else
					{
						std::vector<double> filteredX;
						std::vector<double> filteredY = absolutePositive(rawY, x, filteredX);
						if (!filteredX.empty())
							ImPlot::PlotLine(curveLabel.c_str(), filteredX.data(), filteredY.data(), static_cast<int>(filteredX.size()), spec);
					}
				}
				ImPlot::EndPlot();
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("JFM_CHARACTERISTIC_ID"))
				{
					const CharacteristicId id = *static_cast<const CharacteristicId *>(payload->Data);
					std::vector<CharacteristicId> idsToAdd = m_Store->GetSelectedCharacteristicIds();
					if (std::find(idsToAdd.begin(), idsToAdd.end(), id) == idsToAdd.end())
						idsToAdd = {id};
					for (CharacteristicId selectedId : idsToAdd)
					{
						if (std::find(area.characteristicIds.begin(), area.characteristicIds.end(), selectedId) == area.characteristicIds.end())
							area.characteristicIds.push_back(selectedId);
					}
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopID();
			ImGui::End();

			if (!plotVisible)
				m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
			else
				++areaIndex;
		}
	}
} // namespace SST::Prototypes::JFM
