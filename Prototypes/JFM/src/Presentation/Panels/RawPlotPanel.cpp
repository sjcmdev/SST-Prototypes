#include "Core/dspch.hpp"

#include "Presentation/Panels/RawPlotPanel.hpp"

#include "App/JFMCommands.hpp"
#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>

#include <imgui.h>
#include <implot.h>

namespace SST::Prototypes::JFM
{
	namespace RawPlotDetail
	{
		const std::vector<double> &selectYValues(const JFMCharacteristicData &data)
		{
			return data.currentDensity.empty() ? data.current : data.currentDensity;
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
			if (const auto it = info.metadata.find("plot_color"); it != info.metadata.end())
			{
				float r = 0.0f;
				float g = 0.0f;
				float b = 0.0f;
				float a = 1.0f;
				if (std::sscanf(it->second.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a) >= 3)
					return {r, g, b, a};
			}
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

		std::string seriesIdForCharacteristic(const JFMProject &project, CharacteristicId id)
		{
			for (const JFMSeries &series : project.series)
			{
				if (containsId(series.characteristicIds, id))
					return series.id;
			}
			return {};
		}

		std::string colorToMetadata(const std::array<float, 4> &color)
		{
			char buffer[96]{};
			std::snprintf(buffer, sizeof(buffer), "%.6g,%.6g,%.6g,%.6g", color[0], color[1], color[2], color[3]);
			return buffer;
		}

		void copyPrefitFitRange(const JFMPrefitCurve &curve, const JFMPrefitState &state, std::vector<double> &x, std::vector<double> &y)
		{
			const std::size_t count = std::min(curve.voltage.size(), curve.current.size());
			const double lower = std::min(state.fitRange.lowerVoltage, state.fitRange.upperVoltage);
			const double upper = std::max(state.fitRange.lowerVoltage, state.fitRange.upperVoltage);
			x.reserve(count);
			y.reserve(count);
			for (std::size_t index = 0; index < count; ++index)
			{
				const double voltage = curve.voltage[index];
				if (voltage < lower || voltage > upper)
					continue;
				x.push_back(voltage);
				y.push_back(curve.current[index]);
			}
		}

	} // namespace RawPlotDetail
	using namespace RawPlotDetail;

	class RawPlotPanel::PlotStateCommand final : public ICommand
	{
	public:
		PlotStateCommand(RawPlotPanel *panel, std::vector<PlotArea> before, std::vector<PlotArea> after, std::string description)
			: m_Panel(panel), m_Before(std::move(before)), m_After(std::move(after)), m_Description(std::move(description))
		{
		}

		Result<void> Execute(CommandContext &) override
		{
			return {};
		}

		Result<void> Undo(CommandContext &) override
		{
			if (m_Panel != nullptr)
				m_Panel->m_PlotAreas = m_Before;
			return {};
		}

		Result<void> Redo(CommandContext &) override
		{
			if (m_Panel != nullptr)
				m_Panel->m_PlotAreas = m_After;
			return {};
		}

		std::string Description() const override
		{
			return m_Description;
		}

		bool IsUndoable() const noexcept override
		{
			return true;
		}

	private:
		RawPlotPanel *m_Panel = nullptr;
		std::vector<PlotArea> m_Before;
		std::vector<PlotArea> m_After;
		std::string m_Description;
	};

	RawPlotPanel::RawPlotPanel(
		Ref<JFMWorkspaceStore> store,
		Ref<JFMUserSettingsStore> settings,
		Ref<SST::EventBus> eventBus,
		WeakRef<CommandRegistry> commandRegistry,
		WeakRef<UndoStack> undoStack,
		std::string title,
		bool visibleByDefault)
		: IPanel(std::move(title), visibleByDefault),
		  m_Store(std::move(store)),
		  m_Settings(std::move(settings)),
		  m_EventBus(std::move(eventBus)),
		  m_CommandRegistry(std::move(commandRegistry)),
		  m_UndoStack(std::move(undoStack))
	{
		if (m_EventBus != nullptr)
		{
			AddSubscription(m_EventBus->Subscribe<PlotRequested>([this](const PlotRequested &event) {
				PlotRequest(event);
			}));
		}
		m_PlotAreas.emplace_back(CreatePlotArea());
	}

	Ref<IPanel> RawPlotPanel::Clone() const
	{
		return CreateRef<RawPlotPanel>(m_Store, m_Settings, m_EventBus, m_CommandRegistry, m_UndoStack, GetTitle(), IsVisible());
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

	RawPlotPanel::PlotCurveGroup RawPlotPanel::CreateCurveGroup(JFMPlotCurveGroupRequest request) const
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

	void RawPlotPanel::PlotRequest(PlotRequested request)
	{
		if (request.groups.empty())
			return;
		PlotArea area = CreatePlotArea(request.project);
		if (!request.title.empty())
			area.title = std::move(request.title);
		area.groups.reserve(request.groups.size());
		for (JFMPlotCurveGroupRequest &groupRequest : request.groups)
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
		PlotRequested request;
		request.project = project;
		request.title = title;
		request.groups.emplace_back(JFMPlotCurveGroupRequest{"selection", title.empty() ? "Selection" : title, std::move(ids), {}, {}});
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
		PlotIds(series->characteristicIds, displayName(series->name, series->metadata), projectHandle);
	}

	void RawPlotPanel::PlotSelectedGroup()
	{
		if (m_Store == nullptr)
			return;
		const ProjectHandle projectHandle = m_Store->GetActiveProjectHandle();
		const std::string groupId = m_Store->GetSelectedGroupId();
		if (groupId.empty())
			return;
		const JFMProject project = m_Store->GetProjectSnapshot(projectHandle);
		const auto group = std::find_if(project.groups.begin(), project.groups.end(), [&groupId](const JFMGroup &candidate) {
			return candidate.id == groupId;
		});
		PlotIds(
			m_Store->GetGroupCharacteristicIds(projectHandle, groupId),
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
		std::unordered_map<std::string, std::vector<JFMGroupingBucket>> resolvedGroupings;
		for (PlotCurveGroup &group : area.groups)
		{
			if (group.groupingId.empty() || group.resolvedRevision == revision)
				continue;
			auto [resolved, inserted] = resolvedGroupings.try_emplace(group.groupingId);
			if (inserted)
				resolved->second = m_Store->ResolveGrouping(area.project, group.groupingId);
			const std::vector<JFMGroupingBucket> &buckets = resolved->second;
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

	void RawPlotPanel::SetGroupVisible(PlotCurveGroup &group, bool visible)
	{
		group.visible = visible;
		if (visible)
			group.hiddenCharacteristicIds.clear();
		else
			group.hiddenCharacteristicIds = group.characteristicIds;
	}

	void RawPlotPanel::SetGroupCharacteristicsVisible(PlotCurveGroup &group, bool visible)
	{
		group.visible = true;
		if (visible)
			group.hiddenCharacteristicIds.clear();
		else
			group.hiddenCharacteristicIds = group.characteristicIds;
	}

	void RawPlotPanel::Execute(const char *commandId, CommandContext context) const
	{
		if (auto registry = m_CommandRegistry.lock())
		{
			context.SetContextId(ContextID{"jfm.raw_plot"});
			context.SetSource("Raw Plot");
			(void)registry->Execute(CommandID{commandId}, std::move(context));
		}
	}

	void RawPlotPanel::FinishRangeUndoGroup(PlotArea &area, bool cancel)
	{
		if (!area.rangeUndoGroupOpen)
			return;
		if (auto undoStack = m_UndoStack.lock())
		{
			if (cancel)
				(void)undoStack->CancelGroup();
			else
				(void)undoStack->EndGroup();
		}
		area.rangeUndoGroupOpen = false;
		area.rangeHeldLastFrame = false;
	}

	void RawPlotPanel::PushPlotUndo(std::vector<PlotArea> before, std::string description)
	{
		if (auto undoStack = m_UndoStack.lock())
			(void)undoStack->PushExecuted(CreateUnique<PlotStateCommand>(this, std::move(before), m_PlotAreas, std::move(description)));
	}

	bool RawPlotPanel::HandleUndoShortcuts()
	{
		const ImGuiIO &io = ImGui::GetIO();
		if (!io.KeyCtrl || io.WantTextInput || ImGui::IsAnyItemActive())
			return false;
		const bool redo = (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) || ImGui::IsKeyPressed(ImGuiKey_Y, false);
		const bool undo = !redo && ImGui::IsKeyPressed(ImGuiKey_Z, false);
		if (!undo && !redo)
			return false;
		for (PlotArea &area : m_PlotAreas)
			FinishRangeUndoGroup(area);
		if (auto registry = m_CommandRegistry.lock())
		{
			(void)registry->Execute(CommandID{redo ? "edit.redo" : "edit.undo"});
			return true;
		}
		if (auto undoStack = m_UndoStack.lock())
		{
			if (redo)
				(void)undoStack->Redo();
			else
				(void)undoStack->Undo();
			return true;
		}
		return false;
	}

	void RawPlotPanel::SelectCurveInWorkspace(const PlotArea &area, const PlotCurveGroup &group, CharacteristicId id)
	{
		if (m_Store == nullptr)
			return;
		const auto snapshot = m_Store->CaptureProjectState(area.project);
		std::vector<CharacteristicId> selected = snapshot ? snapshot->selectedCharacteristicIds : std::vector<CharacteristicId>{};
		const ImGuiIO &io = ImGui::GetIO();
		if (io.KeyShift && snapshot && snapshot->selectedCharacteristicId != InvalidCharacteristicId)
		{
			const auto first = std::find(group.characteristicIds.begin(), group.characteristicIds.end(), snapshot->selectedCharacteristicId);
			const auto last = std::find(group.characteristicIds.begin(), group.characteristicIds.end(), id);
			if (first != group.characteristicIds.end() && last != group.characteristicIds.end())
			{
				const auto begin = std::min(first, last);
				const auto end = std::max(first, last);
				selected.assign(begin, end + 1);
			}
			else
			{
				selected = {id};
			}
		}
		else if (io.KeyCtrl)
		{
			if (containsId(selected, id))
				selected.erase(std::remove(selected.begin(), selected.end(), id), selected.end());
			else
				selected.push_back(id);
			if (selected.empty())
				selected.push_back(id);
		}
		else
		{
			selected = {id};
		}
		const JFMProject project = m_Store->GetProjectSnapshot(area.project);
		m_Store->SetSelectedCharacteristics(area.project, std::move(selected), id, seriesIdForCharacteristic(project, id));
	}

	void RawPlotPanel::RenderRangeDragLines(PlotArea &area, const std::unordered_map<CharacteristicId, JFMCharacteristicRecord> &records)
	{
		if (m_Store == nullptr)
			return;
		const auto snapshot = m_Store->CaptureProjectState(area.project);
		const CharacteristicId active = snapshot ? snapshot->selectedCharacteristicId : InvalidCharacteristicId;
		if (active == InvalidCharacteristicId || !records.contains(active))
		{
			FinishRangeUndoGroup(area);
			area.rangeCharacteristic = InvalidCharacteristicId;
			area.rangeInitialized = false;
			return;
		}
		const auto activeRecord = records.find(active);
		if (activeRecord == records.end() || activeRecord->second.data == nullptr || activeRecord->second.data->voltage.empty())
			return;

		const std::vector<double> &voltage = activeRecord->second.data->voltage;
		double minimumVoltage = std::numeric_limits<double>::infinity();
		double maximumVoltage = -std::numeric_limits<double>::infinity();
		for (double value : voltage)
		{
			if (!std::isfinite(value))
				continue;
			minimumVoltage = std::min(minimumVoltage, value);
			maximumVoltage = std::max(maximumVoltage, value);
		}
		if (!std::isfinite(minimumVoltage) || !std::isfinite(maximumVoltage) || minimumVoltage >= maximumVoltage)
			return;

		if (area.rangeCharacteristic != active)
		{
			FinishRangeUndoGroup(area);
			area.rangeCharacteristic = active;
			area.rangeInitialized = false;
		}
		const auto prefit = m_Store->GetPrefitState({area.project, active});
		if (!area.rangeInitialized || (!area.rangeUndoGroupOpen && prefit && prefit->status == "completed"))
		{
			area.rangeLower = minimumVoltage;
			area.rangeUpper = maximumVoltage;
			if (prefit && prefit->fitRange.upperVoltage > prefit->fitRange.lowerVoltage)
			{
				area.rangeLower = prefit->fitRange.lowerVoltage;
				area.rangeUpper = prefit->fitRange.upperVoltage;
			}
			if (prefit && prefit->manualLowerVoltage && prefit->manualUpperVoltage)
			{
				area.rangeLower = *prefit->manualLowerVoltage;
				area.rangeUpper = *prefit->manualUpperVoltage;
			}
			area.rangeInitialized = true;
		}

		bool lowerHeld = false;
		bool upperHeld = false;
		const ImVec4 rangeColor{0.96f, 0.60f, 0.16f, 1.0f};
		const bool lowerChanged = ImPlot::DragLineX(static_cast<int>(area.id * 10 + 1), &area.rangeLower, rangeColor, 2.0f, ImPlotDragToolFlags_None, nullptr, nullptr, &lowerHeld);
		const bool upperChanged = ImPlot::DragLineX(static_cast<int>(area.id * 10 + 2), &area.rangeUpper, rangeColor, 2.0f, ImPlotDragToolFlags_None, nullptr, nullptr, &upperHeld);
		const bool changed = lowerChanged || upperChanged;
		const bool held = lowerHeld || upperHeld;
		if (changed)
		{
			if (!area.rangeUndoGroupOpen)
			{
				if (auto undoStack = m_UndoStack.lock())
				{
					if (undoStack->BeginGroup("Change fit range"))
						area.rangeUndoGroupOpen = true;
				}
			}
			area.rangeLower = std::clamp(area.rangeLower, minimumVoltage, maximumVoltage);
			area.rangeUpper = std::clamp(area.rangeUpper, minimumVoltage, maximumVoltage);
			if (area.rangeLower > area.rangeUpper)
				std::swap(area.rangeLower, area.rangeUpper);
			if (area.rangeLower != area.rangeUpper)
			{
				CommandContext context;
				context.Set("project", area.project);
				context.Set("characteristic_id", active);
				context.Set("lower_voltage", area.rangeLower);
				context.Set("upper_voltage", area.rangeUpper);
				Execute(JFMSetFitRangeCommandId, std::move(context));
			}
		}
		if (area.rangeUndoGroupOpen && !held && (area.rangeHeldLastFrame || changed))
			FinishRangeUndoGroup(area);
		area.rangeHeldLastFrame = held;
	}

	void RawPlotPanel::Render()
	{
		if (!IsVisible())
		{
			for (PlotArea &area : m_PlotAreas)
				FinishRangeUndoGroup(area);
			return;
		}

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
			std::unordered_map<CharacteristicId, JFMCharacteristicRecord> records;
			for (const PlotCurveGroup &group : area.groups)
			{
				for (CharacteristicId id : group.characteristicIds)
				{
					if (records.contains(id))
						continue;
					if (auto record = m_Store->FindCharacteristicRecord({area.project, id}))
						records.emplace(id, std::move(*record));
				}
			}
			for (PlotCurveGroup &group : area.groups)
			{
				group.characteristicIds.erase(
					std::remove_if(
						group.characteristicIds.begin(),
						group.characteristicIds.end(),
						[&records](CharacteristicId id) {
							return !records.contains(id);
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
				{
					FinishRangeUndoGroup(area);
					m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
				}
				else
					++areaIndex;
				continue;
			}

			ImGui::PushID(static_cast<int>(area.id));
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
				(void)HandleUndoShortcuts();
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
			{
				std::vector<PlotArea> before = m_PlotAreas;
				area.groups.clear();
				PushPlotUndo(std::move(before), "Clear raw plot");
			}
			ImGui::SameLine();
			if (m_PlotAreas.size() > 1 && ImGui::Button("Close Plot"))
			{
				ImGui::PopID();
				ImGui::End();
				FinishRangeUndoGroup(area);
				m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
				continue;
			}

			int scale = static_cast<int>(area.scale);
			ImGui::SameLine();
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
			ImGui::SameLine();
			if (ImGui::SmallButton("Expand all"))
				area.groupTreeOpenRequest = 1;
			ImGui::SameLine();
			if (ImGui::SmallButton("Collapse all"))
				area.groupTreeOpenRequest = -1;
			ImGui::SameLine();
			if (ImGui::SmallButton("Hide all"))
			{
				std::vector<PlotArea> before = m_PlotAreas;
				for (PlotCurveGroup &group : area.groups)
					SetGroupVisible(group, false);
				PushPlotUndo(std::move(before), "Hide raw plot curves");
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Unhide all"))
			{
				std::vector<PlotArea> before = m_PlotAreas;
				for (PlotCurveGroup &group : area.groups)
					SetGroupVisible(group, true);
				PushPlotUndo(std::move(before), "Unhide raw plot curves");
			}
			ImGui::SameLine();
			ImGui::Checkbox("Curves", &area.curvesWindowVisible);

			area.hoveredCharacteristicId = InvalidCharacteristicId;
			const auto areaSelection = m_Store->CaptureProjectState(area.project);
			const std::vector<CharacteristicId> areaSelectedIds = areaSelection ? areaSelection->selectedCharacteristicIds : std::vector<CharacteristicId>{};
			const std::string curvesTitle = area.title + " Curves###jfm_raw_plot_curves_" + std::to_string(area.id);
			if (area.curvesWindowVisible)
			{
				if (ImGui::Begin(curvesTitle.c_str(), &area.curvesWindowVisible, ImGuiWindowFlags_AlwaysAutoResize))
				{
					if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
						(void)HandleUndoShortcuts();
					for (std::size_t groupIndex = 0; groupIndex < area.groups.size();)
					{
						PlotCurveGroup &group = area.groups[groupIndex];
						ImGui::PushID(group.key.c_str());
						if (ImGui::SmallButton("x"))
						{
							std::vector<PlotArea> before = m_PlotAreas;
							area.groups.erase(area.groups.begin() + static_cast<std::ptrdiff_t>(groupIndex));
							PushPlotUndo(std::move(before), "Remove raw plot group");
							ImGui::PopID();
							continue;
						}
						ImGui::SameLine();
						bool groupVisible = group.visible;
						if (ImGui::Checkbox("##group_visible", &groupVisible))
						{
							std::vector<PlotArea> before = m_PlotAreas;
							SetGroupVisible(group, groupVisible);
							PushPlotUndo(std::move(before), "Toggle raw plot group");
						}
						ImGui::SameLine();
						const std::string groupLabel = group.title + " (" + std::to_string(group.characteristicIds.size()) + ")";
						if (area.groupTreeOpenRequest != 0)
							ImGui::SetNextItemOpen(area.groupTreeOpenRequest > 0, ImGuiCond_Always);
						const bool open = ImGui::TreeNodeEx(groupLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
						if (open)
						{
							ImGui::SameLine();
							ImGui::ColorEdit4("Start", group.gradientStart.data(), ImGuiColorEditFlags_NoInputs);
							ImGui::SameLine();
							ImGui::ColorEdit4("End", group.gradientEnd.data(), ImGuiColorEditFlags_NoInputs);
							ImGui::SetNextItemWidth(250.0f);
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
										std::vector<PlotArea> before = m_PlotAreas;
										group.gradientStart = preset.start;
										group.gradientEnd = preset.end;
										ApplyColorRange(group);
										PushPlotUndo(std::move(before), "Apply raw plot color preset");
									}
									ImGui::PopID();
								}
								ImGui::EndCombo();
							}
							ImGui::SameLine();
							if (ImGui::Button("Apply"))
							{
								std::vector<PlotArea> before = m_PlotAreas;
								ApplyColorRange(group);
								PushPlotUndo(std::move(before), "Apply raw plot colors");
							}
							ImGui::SameLine();
							if (ImGui::SmallButton("Hide curves"))
							{
								std::vector<PlotArea> before = m_PlotAreas;
								SetGroupCharacteristicsVisible(group, false);
								PushPlotUndo(std::move(before), "Hide raw plot group curves");
							}
							ImGui::SameLine();
							if (ImGui::SmallButton("Show curves"))
							{
								std::vector<PlotArea> before = m_PlotAreas;
								SetGroupCharacteristicsVisible(group, true);
								PushPlotUndo(std::move(before), "Show raw plot group curves");
							}

							for (std::size_t idIndex = 0; idIndex < group.characteristicIds.size();)
							{
								const CharacteristicId id = group.characteristicIds[idIndex];
								const auto characteristic = records.find(id);
								ImGui::PushID(static_cast<int>(id));
								if (ImGui::SmallButton("x"))
								{
									std::vector<PlotArea> before = m_PlotAreas;
									group.characteristicIds.erase(group.characteristicIds.begin() + static_cast<std::ptrdiff_t>(idIndex));
									PushPlotUndo(std::move(before), "Remove raw plot curve");
									ImGui::PopID();
									continue;
								}
								ImGui::SameLine();
								JFMCharacteristicInfo fallbackInfo;
								fallbackInfo.id = id;
								std::array<float, 4> &color = group.colors.try_emplace(
									id,
									toArray(colorFor(characteristic != records.end() ? characteristic->second.info : fallbackInfo)))
									.first->second;
								if (characteristic != records.end() && characteristic->second.info.metadata.find("plot_color") != characteristic->second.info.metadata.end())
									color = toArray(colorFor(characteristic->second.info));
								const std::string colorEditKey = std::to_string(area.id) + ":" + group.key + ":" + std::to_string(id);
								const std::array<float, 4> colorBeforeItem = color;
								(void)ImGui::ColorEdit4("##color", color.data(), ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
								if (ImGui::IsItemActivated())
									m_ColorEditBefore[colorEditKey] = colorBeforeItem;
								if (ImGui::IsItemDeactivatedAfterEdit())
								{
									const std::array<float, 4> editedColor = color;
									CommandContext context;
									context.Set("project", area.project);
									context.Set("characteristic_ids", std::vector<CharacteristicId>{id});
									context.Set("plot_color", colorToMetadata(editedColor));
									Execute(JFMSetPlotColorCommandId, std::move(context));
									if (const auto before = m_ColorEditBefore.find(colorEditKey); before != m_ColorEditBefore.end())
									{
										color = before->second;
										m_ColorEditBefore.erase(before);
									}
								}
								ImGui::SameLine();
								bool shown = !containsId(group.hiddenCharacteristicIds, id);
								if (ImGui::Checkbox("##visible", &shown))
								{
									std::vector<PlotArea> before = m_PlotAreas;
									setHidden(group.hiddenCharacteristicIds, id, !shown);
									PushPlotUndo(std::move(before), "Toggle raw plot curve");
								}
								ImGui::SameLine();
								const std::string label = characteristic != records.end() ? shortName(characteristic->second.info) : std::to_string(id);
								const bool selectedInWorkspace = containsId(areaSelectedIds, id);
								if (selectedInWorkspace)
								{
									ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.70f, 0.48f, 0.26f, 0.95f));
									ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.82f, 0.58f, 0.30f, 1.00f));
									ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.95f, 0.64f, 0.32f, 1.00f));
								}
								if (ImGui::Selectable(label.c_str(), selectedInWorkspace))
									SelectCurveInWorkspace(area, group, id);
								if (selectedInWorkspace)
									ImGui::PopStyleColor(3);
								if (ImGui::IsItemHovered())
								{
									area.hoveredCharacteristicId = id;
									if (characteristic != records.end())
										ImGui::SetTooltip("%s", characteristic->second.info.name.c_str());
								}
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
				}
				ImGui::End();
			}
			area.groupTreeOpenRequest = 0;

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
						const auto characteristic = records.find(id);
						if (characteristic == records.end() || !characteristic->second.data)
							continue;
						const JFMCharacteristicData &data = *characteristic->second.data;
						const std::vector<double> &x = data.voltage;
						const std::vector<double> &rawY = selectYValues(data);
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

				const JFMUserSettings plotSettings = m_Settings != nullptr ? m_Settings->GetSnapshot() : JFMUserSettings{};
				for (const PlotCurveGroup &group : area.groups)
				{
					if (!group.visible)
						continue;
					for (CharacteristicId id : group.characteristicIds)
					{
						if (containsId(group.hiddenCharacteristicIds, id))
							continue;
						const auto characteristic = records.find(id);
						if (characteristic == records.end() || !characteristic->second.data)
							continue;
						const JFMCharacteristicData &data = *characteristic->second.data;
						const std::vector<double> &x = data.voltage;
						const std::vector<double> &rawY = selectYValues(data);
						const std::size_t count = std::min(x.size(), rawY.size());
						if (count == 0)
							continue;
						const auto color = group.colors.find(id);
						const std::array<float, 4> fallbackColor = toArray(colorFor(characteristic->second.info));
						const bool hasMetadataColor = characteristic->second.info.metadata.find("plot_color") != characteristic->second.info.metadata.end();
						const ImPlotSpec spec(ImPlotProp_LineColor, toVec4(hasMetadataColor || color == group.colors.end() ? fallbackColor : color->second));
						ImPlotSpec rawSpec = spec;
						if (area.hoveredCharacteristicId == id)
							rawSpec.LineWeight = plotSettings.hoveredCurveLineWeight;
						const std::string curveLabel = group.title + " / " + shortName(characteristic->second.info)
							+ "###curve_" + std::to_string(area.project) + "_" + group.key + "_" + std::to_string(id);
						if (area.scale == PlotScale::Linear)
							ImPlot::PlotLine(curveLabel.c_str(), x.data(), rawY.data(), static_cast<int>(count), rawSpec);
						else
						{
							std::vector<double> filteredX;
							std::vector<double> filteredY = absolutePositive(rawY, x, filteredX);
							if (!filteredX.empty())
								ImPlot::PlotLine(curveLabel.c_str(), filteredX.data(), filteredY.data(), static_cast<int>(filteredX.size()), rawSpec);
						}
						const auto curve = m_Store->GetPrefitCurve({area.project, id});
						if (plotSettings.showPrefitCurves && curve)
						{
							std::optional<JFMPrefitState> prefitState;
							if (plotSettings.prefitCurvesFitRangeOnly)
								prefitState = m_Store->GetPrefitState({area.project, id});
							std::vector<double> fitRangeX;
							std::vector<double> fitRangeY;
							const std::vector<double> *prefitX = &curve->voltage;
							const std::vector<double> *prefitY = &curve->current;
							if (prefitState && prefitState->fitRange.upperVoltage > prefitState->fitRange.lowerVoltage)
							{
								copyPrefitFitRange(*curve, *prefitState, fitRangeX, fitRangeY);
								prefitX = &fitRangeX;
								prefitY = &fitRangeY;
							}
							const std::size_t curveCount = std::min(prefitX->size(), prefitY->size());
							if (curveCount > 0)
							{
								ImPlotSpec prefitSpec;
								prefitSpec.LineColor = toVec4(plotSettings.prefitCurveColor);
								prefitSpec.LineWeight = area.hoveredCharacteristicId == id ? plotSettings.hoveredCurveLineWeight : plotSettings.prefitCurveLineWeight;
								prefitSpec.Flags = ImPlotItemFlags_NoLegend;
								const std::string prefitLabel = "##jfm_raw_prefit_" + std::to_string(area.project) + "_" + group.key + "_" + std::to_string(id);
								if (area.scale == PlotScale::Linear)
									ImPlot::PlotLine(prefitLabel.c_str(), prefitX->data(), prefitY->data(), static_cast<int>(curveCount), prefitSpec);
								else
								{
									std::vector<double> filteredX;
									std::vector<double> filteredY = absolutePositive(*prefitY, *prefitX, filteredX);
									if (!filteredX.empty())
										ImPlot::PlotLine(prefitLabel.c_str(), filteredX.data(), filteredY.data(), static_cast<int>(filteredX.size()), prefitSpec);
								}
							}
						}
						const auto fitCurve = m_Store->GetFitCurve({area.project, id});
						if (plotSettings.showFitCurves && fitCurve)
						{
							const std::size_t fitCount = std::min(fitCurve->voltage.size(), fitCurve->current.size());
							if (fitCount > 0)
							{
								ImPlotSpec fitSpec;
								fitSpec.LineColor = toVec4(plotSettings.fitCurveColor);
								fitSpec.LineWeight = area.hoveredCharacteristicId == id ? plotSettings.hoveredCurveLineWeight : plotSettings.fitCurveLineWeight;
								fitSpec.Flags = ImPlotItemFlags_NoLegend;
								const std::string fitLabel = "##jfm_raw_fit_" + std::to_string(area.project) + "_" + group.key + "_" + std::to_string(id);
								if (area.scale == PlotScale::Linear)
									ImPlot::PlotLine(fitLabel.c_str(), fitCurve->voltage.data(), fitCurve->current.data(), static_cast<int>(fitCount), fitSpec);
								else
								{
									std::vector<double> filteredX;
									std::vector<double> filteredY = absolutePositive(fitCurve->current, fitCurve->voltage, filteredX);
									if (!filteredX.empty())
										ImPlot::PlotLine(fitLabel.c_str(), filteredX.data(), filteredY.data(), static_cast<int>(filteredX.size()), fitSpec);
								}
							}
						}
					}
				}
				RenderRangeDragLines(area, records);
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
			{
				FinishRangeUndoGroup(area);
				m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
			}
			else
				++areaIndex;
		}
	}
} // namespace SST::Prototypes::JFM
