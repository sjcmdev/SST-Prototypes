#include "Core/dspch.hpp"

#include "Presentation/Panels/RawPlotPanel.hpp"

#include <algorithm>
#include <cmath>

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
	}

	RawPlotPanel::RawPlotPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Store(std::move(store))
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
			bool plotVisible = true;
			const std::string windowTitle = "Raw Plot " + std::to_string(area.id) + "###jfm_raw_plot_window_" + std::to_string(area.id);
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
			ImGui::RadioButton("Signed-log", &scale, static_cast<int>(PlotScale::SignedLog));
			area.scale = static_cast<PlotScale>(scale);

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
				ImGui::Selectable(characteristic ? characteristic->info.name.c_str() : std::to_string(id).c_str(), false);
				ImGui::PopID();
				++idIndex;
			}

			if (area.characteristicIds.empty())
				ImGui::TextDisabled("Drop characteristics here or use selection.");
			ImGui::EndChild();
			ImGui::SameLine();

			const std::string plotId = "##jfm_raw_plot_" + std::to_string(area.id);
			const float plotHeight = std::max(360.0f, ImGui::GetContentRegionAvail().y);
			if (ImPlot::BeginPlot(plotId.c_str(), ImVec2(-1.0f, plotHeight)))
			{
				ImPlot::SetupAxes("Voltage", area.scale == PlotScale::SignedLog ? "signed log current" : "Current");
				ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Outside | ImPlotLegendFlags_NoButtons);
				if (area.scale == PlotScale::SemiLog)
					ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);

				for (CharacteristicId id : area.characteristicIds)
				{
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
					if (area.scale == PlotScale::Linear)
					{
						ImPlot::PlotLine(characteristic->info.name.c_str(), x.data(), rawY.data(), static_cast<int>(count), spec);
					}
					else if (area.scale == PlotScale::SemiLog)
					{
						std::vector<double> filteredX;
						std::vector<double> filteredY = absolutePositive(rawY, x, filteredX);
						if (!filteredX.empty())
							ImPlot::PlotLine(characteristic->info.name.c_str(), filteredX.data(), filteredY.data(), static_cast<int>(filteredX.size()), spec);
					}
					else
					{
						std::vector<double> transformed = signedLog(rawY);
						ImPlot::PlotLine(characteristic->info.name.c_str(), x.data(), transformed.data(), static_cast<int>(std::min(x.size(), transformed.size())), spec);
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
