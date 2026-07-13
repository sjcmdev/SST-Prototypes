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
			const double seed = info.temperatureK.value_or(static_cast<double>(info.id % 997));
			const float hue = static_cast<float>(std::fmod(seed * 0.013, 1.0));
			return ImColor::HSV(hue, 0.72f, 0.92f).Value;
		}
	}

	RawPlotPanel::RawPlotPanel(Ref<JFMWorkspaceStore> store, std::string title, bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault), m_Store(std::move(store))
	{
		m_PlotAreas.push_back({});
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

		if (m_Store == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");
			ImGui::End();
			SetVisible(visible);
			return;
		}

		if (ImGui::Button("+ Plot Window"))
			m_PlotAreas.push_back({});
		ImGui::SameLine();
		if (ImGui::Button("Use Selection"))
		{
			if (m_PlotAreas.empty())
				m_PlotAreas.push_back({});
			m_PlotAreas.front().characteristicIds = m_Store->GetSelectedCharacteristicIds();
		}
		ImGui::TextDisabled("Plot windows: %d", static_cast<int>(m_PlotAreas.size()));
		ImGui::End();
		SetVisible(visible);
		if (!visible)
			return;

		if (m_PlotAreas.empty())
			m_PlotAreas.push_back({});

		for (std::size_t areaIndex = 0; areaIndex < m_PlotAreas.size();)
		{
			PlotArea &area = m_PlotAreas[areaIndex];
			bool plotVisible = true;
			const std::string windowTitle = "Raw Plot " + std::to_string(areaIndex + 1) + "###jfm_raw_plot_window_" + std::to_string(areaIndex);
			if (!ImGui::Begin(windowTitle.c_str(), &plotVisible))
			{
				ImGui::End();
				if (!plotVisible)
					m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
				else
					++areaIndex;
				continue;
			}

			ImGui::PushID(static_cast<int>(areaIndex));
			if (ImGui::Button("Use Selection"))
				area.characteristicIds = m_Store->GetSelectedCharacteristicIds();
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
				ImGui::TextUnformatted(characteristic ? characteristic->info.name.c_str() : std::to_string(id).c_str());
				ImGui::PopID();
				++idIndex;
			}

			if (area.characteristicIds.empty())
				ImGui::TextDisabled("Drop characteristics here or use selection.");

			const std::string plotId = "##jfm_raw_plot_" + std::to_string(areaIndex);
			if (ImPlot::BeginPlot(plotId.c_str(), ImVec2(-1.0f, 260.0f)))
			{
				ImPlot::SetupAxes("Voltage", area.scale == PlotScale::SignedLog ? "signed log current" : "Current");
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

					const ImPlotSpec spec(ImPlotProp_LineColor, colorFor(characteristic->info));
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
