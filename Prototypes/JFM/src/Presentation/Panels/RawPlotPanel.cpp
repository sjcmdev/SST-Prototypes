#include "Core/dspch.hpp"

#include "Presentation/Panels/RawPlotPanel.hpp"

#include "Events/JFMEvents.hpp"

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
			return characteristic.data.currentDensity.empty() ? characteristic.data.current : characteristic.data.currentDensity;
		}

		std::vector<double> absolutePositive(const std::vector<double> &values, const std::vector<double> &x, std::vector<double> &filteredX)
		{
			const std::size_t count = std::min(values.size(), x.size());
			std::vector<double> filteredY;
			filteredX.reserve(count);
			filteredY.reserve(count);
			for (std::size_t index = 0; index < count; ++index)
			{
				const double value = std::abs(values[index]);
				if (value <= 0.0)
					continue;
				filteredX.emplace_back(x[index]);
				filteredY.emplace_back(value);
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
					target.emplace_back(id);
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
				ids.emplace_back(id);
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

		std::string displayName(const std::string &name, const std::unordered_map<std::string, std::string> &metadata)
		{
			if (const auto it = metadata.find("short_name"); it != metadata.end() && !it->second.empty())
				return it->second;
			return name;
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

		const JFMColorPreset *findPreset(const JFMUserSettings &settings, const std::string &name)
		{
			const auto it = std::find_if(settings.colorPresets.begin(), settings.colorPresets.end(), [&name](const JFMColorPreset &preset) {
				return preset.name == name;
			});
			if (it != settings.colorPresets.end())
				return &*it;
			return settings.colorPresets.empty() ? nullptr : &settings.colorPresets.front();
		}

		std::string fallbackGroupKey(std::uint64_t areaId, std::size_t groupIndex)
		{
			return "plot-" + std::to_string(areaId) + "-group-" + std::to_string(groupIndex + 1);
		}
	}

	RawPlotPanel::RawPlotPanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMPlotRequestState> plotRequests,
		Ref<JFMUserSettingsStore> settings,
		Ref<SST::EventBus> eventBus,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store)),
		  m_PlotRequests(std::move(plotRequests)),
		  m_Settings(std::move(settings)),
		  m_EventBus(std::move(eventBus))
	{
		if (m_EventBus != nullptr)
		{
			AddSubscription(m_EventBus->Subscribe<PlotCharacteristicsRequested>([this](const PlotCharacteristicsRequested &event) {
				PlotIds(event.ids, event.title, event.project);
			}));
		}
		m_PlotAreas.emplace_back(CreatePlotArea());
	}

	Ref<IPanel> RawPlotPanel::Clone() const
	{
		return CreateRef<RawPlotPanel>(m_Store, m_PlotRequests, m_Settings, m_EventBus, GetTitle(), IsVisible());
	}

	RawPlotPanel::PlotArea RawPlotPanel::CreatePlotArea(ProjectHandle project)
	{
		PlotArea area;
		area.id = m_NextPlotAreaId++;
		area.project = project == InvalidProjectHandle && m_Store != nullptr ? m_Store->GetActiveProjectHandle() : project;
		area.title = "Raw Plot " + std::to_string(area.id);
		if (m_Settings != nullptr)
		{
			const JFMUserSettings settings = m_Settings->GetSnapshot();
			area.scale = settings.defaultPlotSemiLog ? PlotScale::SemiLog : PlotScale::Linear;
			area.autoFit = settings.defaultPlotAutoFit;
			area.showLegend = settings.defaultPlotShowLegend;
			area.fitPadding = settings.defaultPlotFitPadding;
		}
		return area;
	}

	RawPlotPanel::PlotCurveGroup RawPlotPanel::CreateCurveGroup(JFMPlotRequest::CurveGroup request) const
	{
		PlotCurveGroup group;
		group.key = std::move(request.key);
		group.title = request.title.empty() ? "Selection" : std::move(request.title);
		group.characteristicIds = std::move(request.ids);
		group.groupingId = std::move(request.groupingId);
		group.bucketKey = std::move(request.bucketKey);
		if (m_Settings != nullptr)
		{
			const JFMUserSettings settings = m_Settings->GetSnapshot();
			if (const JFMColorPreset *preset = findPreset(settings, settings.defaultColorPresetName))
			{
				group.gradientStart = preset->start;
				group.gradientEnd = preset->end;
			}
		}
		return group;
	}

	void RawPlotPanel::PlotRequest(JFMPlotRequest request)
	{
		if (request.groups.empty())
			return;
		PlotArea area = CreatePlotArea(request.project);
		if (!request.title.empty())
			area.title = std::move(request.title);
		area.groups.reserve(request.groups.size());
		for (JFMPlotRequest::CurveGroup &groupRequest : request.groups)
		{
			PlotCurveGroup group = CreateCurveGroup(std::move(groupRequest));
			if (group.key.empty())
				group.key = fallbackGroupKey(area.id, area.groups.size());
			area.groups.emplace_back(std::move(group));
			ApplyColorRange(area.groups.back());
		}
		m_PlotAreas.emplace_back(std::move(area));
	}

	void RawPlotPanel::PlotIds(std::vector<CharacteristicId> ids, std::string title, ProjectHandle project)
	{
		if (ids.empty())
			return;
		JFMPlotRequest request;
		request.project = project;
		request.title = title;
		request.groups.emplace_back(JFMPlotRequest::CurveGroup{"selection", title.empty() ? "Selection" : title, std::move(ids), {}, {}});
		PlotRequest(std::move(request));
	}

	void RawPlotPanel::PlotSeries(const std::string &seriesId)
	{
		if (m_Store == nullptr || seriesId.empty())
			return;
		const ProjectHandle projectHandle = m_Store->GetActiveProjectHandle();
		const JFMProject project = m_Store->GetProjectSnapshot(projectHandle);
		const auto series = std::find_if(project.series.begin(), project.series.end(), [&seriesId](const JFMSeries &candidate) {
			return candidate.id == seriesId;
		});
		if (series == project.series.end())
			return;
		std::vector<CharacteristicId> ids;
		for (const JFMCharacteristic &characteristic : m_Store->GetSeriesCharacteristics(projectHandle, seriesId))
			ids.emplace_back(characteristic.info.id);
		PlotIds(std::move(ids), displayName(series->name, series->metadata), projectHandle);
	}

	void RawPlotPanel::PlotSelectedGroup()
	{
		if (m_Store == nullptr)
			return;
		const ProjectHandle projectHandle = m_Store->GetActiveProjectHandle();
		const std::string selectedSeriesId = m_Store->GetSelectedSeriesId();
		const JFMProject project = m_Store->GetProjectSnapshot(projectHandle);
		const auto series = std::find_if(project.series.begin(), project.series.end(), [&selectedSeriesId](const JFMSeries &candidate) {
			return candidate.id == selectedSeriesId;
		});
		if (series == project.series.end() || series->groupId.empty())
			return;
		const auto group = std::find_if(project.groups.begin(), project.groups.end(), [&series](const JFMGroup &candidate) {
			return candidate.id == series->groupId;
		});
		std::vector<CharacteristicId> ids;
		for (const JFMCharacteristic &characteristic : m_Store->GetGroupCharacteristics(projectHandle, series->groupId))
			ids.emplace_back(characteristic.info.id);
		PlotIds(
			std::move(ids),
			group == project.groups.end() ? "Group" : displayName(group->name, group->metadata),
			projectHandle);
	}

	void RawPlotPanel::ApplyColorRange(PlotCurveGroup &group)
	{
		const std::size_t count = std::max<std::size_t>(group.characteristicIds.size(), 1);
		group.colors.clear();
		for (std::size_t index = 0; index < group.characteristicIds.size(); ++index)
		{
			const float t = count == 1 ? 0.0f : static_cast<float>(index) / static_cast<float>(count - 1);
			std::array<float, 4> color{};
			for (std::size_t component = 0; component < color.size(); ++component)
				color[component] = group.gradientStart[component] + (group.gradientEnd[component] - group.gradientStart[component]) * t;
			group.colors.emplace(group.characteristicIds[index], color);
		}
	}

	void RawPlotPanel::RefreshDynamicGroups(PlotArea &area)
	{
		if (m_Store == nullptr)
			return;
		const std::uint64_t revision = m_Store->GetProjectRevision(area.project);
		for (PlotCurveGroup &group : area.groups)
		{
			if (group.groupingId.empty() || group.resolvedRevision == revision)
				continue;
			const std::vector<JFMGroupingBucket> buckets = m_Store->ResolveGrouping(area.project, group.groupingId);
			const auto bucket = std::find_if(buckets.begin(), buckets.end(), [&group](const JFMGroupingBucket &candidate) {
				return candidate.key == group.bucketKey;
			});
			auto previousColors = std::move(group.colors);
			group.colors.clear();
			group.characteristicIds = bucket == buckets.end() ? std::vector<CharacteristicId>{} : bucket->characteristicIds;
			group.hiddenCharacteristicIds.erase(
				std::remove_if(
					group.hiddenCharacteristicIds.begin(),
					group.hiddenCharacteristicIds.end(),
					[&group](CharacteristicId id) { return !containsId(group.characteristicIds, id); }),
				group.hiddenCharacteristicIds.end());
			const std::size_t count = std::max<std::size_t>(group.characteristicIds.size(), 1);
			for (std::size_t index = 0; index < group.characteristicIds.size(); ++index)
			{
				const CharacteristicId id = group.characteristicIds[index];
				if (const auto color = previousColors.find(id); color != previousColors.end())
				{
					group.colors.emplace(id, color->second);
					continue;
				}
				const float t = count == 1 ? 0.0f : static_cast<float>(index) / static_cast<float>(count - 1);
				std::array<float, 4> color{};
				for (std::size_t component = 0; component < color.size(); ++component)
					color[component] = group.gradientStart[component] + (group.gradientEnd[component] - group.gradientStart[component]) * t;
				group.colors.emplace(id, color);
			}
			group.resolvedRevision = revision;
		}
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
			for (JFMPlotRequest request : m_PlotRequests->ConsumeRequests())
				PlotRequest(std::move(request));
		}

		if (ImGui::Button("+ Empty Plot"))
			m_PlotAreas.emplace_back(CreatePlotArea());
		ImGui::SameLine();
		if (ImGui::Button("Plot Selection"))
			PlotIds(m_Store->GetSelectedCharacteristicIds(), {}, m_Store->GetActiveProjectHandle());
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
			m_PlotAreas.emplace_back(CreatePlotArea());

		for (std::size_t areaIndex = 0; areaIndex < m_PlotAreas.size();)
		{
			PlotArea &area = m_PlotAreas[areaIndex];
			RefreshDynamicGroups(area);
			for (PlotCurveGroup &group : area.groups)
			{
				group.characteristicIds.erase(
					std::remove_if(
						group.characteristicIds.begin(),
						group.characteristicIds.end(),
						[this, &area](CharacteristicId id) {
							return !m_Store->FindCharacteristic({area.project, id}).has_value();
						}),
					group.characteristicIds.end());
				group.hiddenCharacteristicIds.erase(
					std::remove_if(
						group.hiddenCharacteristicIds.begin(),
						group.hiddenCharacteristicIds.end(),
						[&group](CharacteristicId id) { return !containsId(group.characteristicIds, id); }),
					group.hiddenCharacteristicIds.end());
			}
			area.groups.erase(
				std::remove_if(area.groups.begin(), area.groups.end(), [](const PlotCurveGroup &group) {
					return group.characteristicIds.empty() && group.groupingId.empty();
				}),
				area.groups.end());

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
				const bool accepted = ImGui::InputText("##plot_rename", area.renameBuffer.data(), area.renameBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if (accepted || ImGui::Button("OK"))
				{
					if (area.renameBuffer[0] != '\0')
						area.title = area.renameBuffer.data();
					area.renaming = false;
				}
			}

			if (ImGui::Button("Use Selection") && area.project == m_Store->GetActiveProjectHandle())
			{
				auto group = std::find_if(area.groups.begin(), area.groups.end(), [](const PlotCurveGroup &candidate) {
					return candidate.key == "selection";
				});
				if (group == area.groups.end())
				{
					area.groups.emplace_back(CreateCurveGroup({"selection", "Selection", {}, {}, {}}));
					group = std::prev(area.groups.end());
				}
				appendUnique(group->characteristicIds, m_Store->GetSelectedCharacteristicIds());
				ApplyColorRange(*group);
			}
			ImGui::SameLine();
			if (ImGui::Button("Clear"))
				area.groups.clear();
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
			area.scale = static_cast<PlotScale>(scale);
			ImGui::SameLine();
			ImGui::Checkbox("Auto fit", &area.autoFit);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(140.0f);
			ImGui::SliderFloat("Scroll out", &area.fitPadding, 0.0f, 0.5f, "%.2f");
			ImGui::SameLine();
			ImGui::Checkbox("Legend", &area.showLegend);

			const float controlsWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.26f, 320.0f, 460.0f);
			ImGui::BeginChild("##curve_groups", ImVec2(controlsWidth, 0.0f), true);
			ImGui::TextDisabled("Curve groups");
			for (std::size_t groupIndex = 0; groupIndex < area.groups.size();)
			{
				PlotCurveGroup &group = area.groups[groupIndex];
				ImGui::PushID(group.key.c_str());
				if (ImGui::SmallButton("x"))
				{
					area.groups.erase(area.groups.begin() + static_cast<std::ptrdiff_t>(groupIndex));
					ImGui::PopID();
					continue;
				}
				ImGui::SameLine();
				ImGui::Checkbox("##group_visible", &group.visible);
				ImGui::SameLine();
				const std::string groupLabel = group.title + " (" + std::to_string(group.characteristicIds.size()) + ")";
				const bool open = ImGui::TreeNodeEx(groupLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
				if (open)
				{
					ImGui::ColorEdit4("Start", group.gradientStart.data(), ImGuiColorEditFlags_NoInputs);
					ImGui::SameLine();
					ImGui::ColorEdit4("End", group.gradientEnd.data(), ImGuiColorEditFlags_NoInputs);
					if (ImGui::BeginCombo("Preset", "Color preset"))
					{
						const JFMUserSettings settings = m_Settings != nullptr ? m_Settings->GetSnapshot() : JFMUserSettings{};
						for (std::size_t presetIndex = 0; presetIndex < settings.colorPresets.size(); ++presetIndex)
						{
							const JFMColorPreset &preset = settings.colorPresets[presetIndex];
							ImGui::PushID(static_cast<int>(presetIndex));
							ImGui::ColorButton("##start", toVec4(preset.start), ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));
							ImGui::SameLine();
							ImGui::ColorButton("##end", toVec4(preset.end), ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));
							ImGui::SameLine();
							if (ImGui::Selectable(preset.name.c_str()))
							{
								group.gradientStart = preset.start;
								group.gradientEnd = preset.end;
								ApplyColorRange(group);
							}
							ImGui::PopID();
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					if (ImGui::Button("Apply"))
						ApplyColorRange(group);

					for (std::size_t idIndex = 0; idIndex < group.characteristicIds.size();)
					{
						const CharacteristicId id = group.characteristicIds[idIndex];
						const auto characteristic = m_Store->FindCharacteristic({area.project, id});
						ImGui::PushID(static_cast<int>(id));
						if (ImGui::SmallButton("x"))
						{
							group.characteristicIds.erase(group.characteristicIds.begin() + static_cast<std::ptrdiff_t>(idIndex));
							ImGui::PopID();
							continue;
						}
						ImGui::SameLine();
						std::array<float, 4> &color = group.colors.try_emplace(
							id,
							toArray(colorFor(characteristic ? characteristic->info : JFMCharacteristicInfo{id})))
							.first->second;
						ImGui::ColorEdit4("##color", color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
						ImGui::SameLine();
						bool shown = !containsId(group.hiddenCharacteristicIds, id);
						if (ImGui::Checkbox("##visible", &shown))
							setHidden(group.hiddenCharacteristicIds, id, !shown);
						ImGui::SameLine();
						const std::string label = characteristic ? shortName(characteristic->info) : std::to_string(id);
						ImGui::Selectable(label.c_str(), false);
						if (characteristic && ImGui::IsItemHovered())
							ImGui::SetTooltip("%s", characteristic->info.name.c_str());
						ImGui::PopID();
						++idIndex;
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
				++groupIndex;
			}
			if (area.groups.empty())
				ImGui::TextDisabled("Drop characteristics here or use selection.");
			ImGui::EndChild();
			ImGui::SameLine();

			const std::string plotId = "##jfm_raw_plot_" + std::to_string(area.id);
			const float plotHeight = std::max(420.0f, ImGui::GetContentRegionAvail().y);
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
				for (const PlotCurveGroup &group : area.groups)
				{
					if (!group.visible)
						continue;
					for (CharacteristicId id : group.characteristicIds)
					{
						if (containsId(group.hiddenCharacteristicIds, id))
							continue;
						const auto characteristic = m_Store->FindCharacteristic({area.project, id});
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

				for (const PlotCurveGroup &group : area.groups)
				{
					if (!group.visible)
						continue;
					for (CharacteristicId id : group.characteristicIds)
					{
						if (containsId(group.hiddenCharacteristicIds, id))
							continue;
						const auto characteristic = m_Store->FindCharacteristic({area.project, id});
						if (!characteristic)
							continue;
						const std::vector<double> &x = characteristic->data.voltage;
						const std::vector<double> &rawY = selectYValues(*characteristic);
						const std::size_t count = std::min(x.size(), rawY.size());
						if (count == 0)
							continue;
						const auto color = group.colors.find(id);
						const std::array<float, 4> fallbackColor = toArray(colorFor(characteristic->info));
						const ImPlotSpec spec(ImPlotProp_LineColor, toVec4(color == group.colors.end() ? fallbackColor : color->second));
						const std::string curveLabel = group.title + " / " + shortName(characteristic->info)
							+ "###curve_" + std::to_string(area.project) + "_" + group.key + "_" + std::to_string(id);
						if (area.scale == PlotScale::Linear)
							ImPlot::PlotLine(curveLabel.c_str(), x.data(), rawY.data(), static_cast<int>(count), spec);
						else
						{
							std::vector<double> filteredX;
							std::vector<double> filteredY = absolutePositive(rawY, x, filteredX);
							if (!filteredX.empty())
								ImPlot::PlotLine(curveLabel.c_str(), filteredX.data(), filteredY.data(), static_cast<int>(filteredX.size()), spec);
						}
					}
				}
				ImPlot::EndPlot();
			}
			if (area.project == m_Store->GetActiveProjectHandle() && ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("JFM_CHARACTERISTIC_ID"))
				{
					const CharacteristicId id = *static_cast<const CharacteristicId *>(payload->Data);
					std::vector<CharacteristicId> ids = m_Store->GetSelectedCharacteristicIds();
					if (!containsId(ids, id))
						ids = {id};
					auto group = std::find_if(area.groups.begin(), area.groups.end(), [](const PlotCurveGroup &candidate) {
						return candidate.key == "manual";
					});
					if (group == area.groups.end())
					{
						area.groups.emplace_back(CreateCurveGroup({"manual", "Manual", {}, {}, {}}));
						group = std::prev(area.groups.end());
					}
					appendUnique(group->characteristicIds, ids);
					ApplyColorRange(*group);
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
