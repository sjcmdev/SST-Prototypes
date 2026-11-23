#include "Core/dspch.hpp"

#include "Presentation/Panels/RawPlotPanel.hpp"
#include "Presentation/JFMPlotWidgets.hpp"

#include "Analysis/JFMFitEngine.hpp"
#include "App/JFMCommands.hpp"
#include "Core/Commands/Command.hpp"
#include "Core/Commands/CommandRegistry.hpp"
#include "Core/Undo/UndoStack.hpp"
#include "Events/JFMEvents.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>

#include <imgui.h>
#include <implot.h>
#include <sst/ui/Plot/ImPlotRenderer.hpp>

namespace SST::Prototypes::JFM
{
	namespace RawPlotDetail
	{
		const std::vector<double> &selectYValues(const JFMCharacteristicData &data)
		{
			return data.current.empty() ? data.currentDensity : data.current;
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

		std::optional<double> metadataDouble(const JFMCharacteristicInfo &info, const std::string &key)
		{
			const auto found = info.metadata.find(key);
			if (found == info.metadata.end())
				return std::nullopt;
			char *end = nullptr;
			const double value = std::strtod(found->second.c_str(), &end);
			return end != found->second.c_str() && std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
		}

		std::unordered_map<std::string, double> tunedParameters(const JFMCharacteristicInfo &info)
		{
			std::unordered_map<std::string, double> result;
			const JFMFitState *state = info.fit ? &*info.fit : nullptr;
			if (state != nullptr)
			{
				for (const auto &[name, estimate] : state->parameters)
					result[name] = estimate.value;
			}
			else if (info.prefit)
			{
				for (const auto &[name, estimate] : info.prefit->parameters)
					result[name] = estimate.value;
			}
			const std::string model = state != nullptr ? state->modelId : info.prefit ? info.prefit->modelId : std::string{};
			for (std::string_view nameView : JFMModelParameterNames(model))
			{
				const std::string name(nameView);
				if (const std::optional<double> tuned = metadataDouble(info, "tuned." + name))
					result[name] = *tuned;
			}
			return result;
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

		template <typename Curve>
		void copyFitRange(const Curve &curve, const JFMFitRange &range, std::vector<double> &x, std::vector<double> &y)
		{
			const std::size_t count = std::min(curve.voltage.size(), curve.current.size());
			const double lower = std::min(range.lowerVoltage, range.upperVoltage);
			const double upper = std::max(range.lowerVoltage, range.upperVoltage);
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
		: EditorWindow("jfm.raw_plots", std::move(title), visibleByDefault, {.duplicable = true}),
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

	WindowDuplicate RawPlotPanel::Duplicate() const
	{
		auto copy = CreateRef<RawPlotPanel>(m_Store, m_Settings, m_EventBus, m_CommandRegistry, m_UndoStack, GetTitle(), IsVisible());
        copy->m_Windows.UseInstanceIdentity();
        return {copy, {}};
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
        if (m_PlotAreas.size() == 1 && m_PlotAreas.front().groups.empty()) {
            area.id = m_PlotAreas.front().id;
            area.title = m_PlotAreas.front().title;
            m_PlotAreas.front() = std::move(area);
        } else m_PlotAreas.emplace_back(std::move(area));
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
		(void)area.rangeInteraction.Finish();
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

	void RawPlotPanel::SelectAdjacentCurveInWorkspace(const PlotArea &area, CharacteristicId active, int direction)
	{
		if (m_Store == nullptr || direction == 0)
			return;
		std::vector<CharacteristicId> ids;
		for (const PlotCurveGroup &group : area.groups)
		{
			if (!group.visible)
				continue;
			for (CharacteristicId id : group.characteristicIds)
			{
				if (!containsId(group.hiddenCharacteristicIds, id))
					ids.push_back(id);
			}
		}
		if (ids.empty())
			return;
		auto current = std::find(ids.begin(), ids.end(), active);
		std::ptrdiff_t index = current == ids.end()
			? (direction < 0 ? static_cast<std::ptrdiff_t>(ids.size() - 1) : 0)
			: std::clamp<std::ptrdiff_t>(std::distance(ids.begin(), current) + direction, 0, static_cast<std::ptrdiff_t>(ids.size() - 1));
		const CharacteristicId next = ids[static_cast<std::size_t>(index)];
		const JFMProject project = m_Store->GetProjectSnapshot(area.project);
		m_Store->SetSelectedCharacteristics(area.project, {next}, next, seriesIdForCharacteristic(project, next));
	}

	bool RawPlotPanel::RenderRangeDragLines(PlotArea &area, const std::unordered_map<CharacteristicId, JFMCharacteristicRecord> &records)
	{
		if (m_Store == nullptr)
			return false;
		const auto snapshot = m_Store->CaptureProjectState(area.project);
		const CharacteristicId active = snapshot ? snapshot->selectedCharacteristicId : InvalidCharacteristicId;
		if (active == InvalidCharacteristicId || !records.contains(active))
		{
			FinishRangeUndoGroup(area);
			area.rangeCharacteristic = InvalidCharacteristicId;
			area.rangeInitialized = false;
			return false;
		}
		const auto activeRecord = records.find(active);
		if (activeRecord == records.end() || activeRecord->second.data == nullptr || activeRecord->second.data->voltage.empty())
			return false;

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
			return false;

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

		sst::ui::plot::RangeStyle rangeStyle;
		rangeStyle.minimum = minimumVoltage;
		rangeStyle.maximum = maximumVoltage;
		const auto interaction = sst::ui::plot::DragRangeX(static_cast<int>(area.id * 10 + 1), static_cast<int>(area.id * 10 + 2),
			area.rangeLower, area.rangeUpper, area.rangeInteraction, rangeStyle);
		const bool wantsResizeCursor = interaction.hovered;
		const bool changed = interaction.changed;
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
			if (area.rangeLower != area.rangeUpper)
			{
				CommandContext context;
				context.Set("project", area.project);
				context.Set("characteristic_id", active);
				context.Set("lower_voltage", area.rangeLower);
				context.Set("upper_voltage", area.rangeUpper);
				Execute(JFMSetFitRangeCommandId, std::move(context));
				if (m_EventBus != nullptr && m_Settings != nullptr)
				{
					const JFMUserSettings settings = m_Settings->GetSnapshot();
					if (settings.autoPrefitOnRangeChange)
						m_EventBus->Queue(PrefitRequested{area.project, {active}});
					if (settings.autoFitOnRangeChange)
						m_EventBus->Queue(FitRequested{area.project, {active}});
				}
			}
		}
		if (area.rangeUndoGroupOpen && interaction.ended)
			FinishRangeUndoGroup(area);
		return wantsResizeCursor;
	}

	void RawPlotPanel::RenderContent()
	{
		if (m_Store == nullptr)
		{
			ImGui::TextDisabled("Workspace unavailable");
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
        RenderPlots();
    }

    void RawPlotPanel::OnContentSkipped()
    {
        // Closing the controls must not stop rendering independent plot windows.
        RenderPlots();
    }

    void RawPlotPanel::RenderPlots()
    {
        m_Windows.BeginFrame();
        if (m_Store == nullptr) { m_Windows.EndFrame(); return; }


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
			const auto *viewport = ImGui::GetMainViewport();
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const float listWidth = viewport->WorkSize.x * 0.28f;
			const float top = viewport->WorkPos.y + ImGui::GetFrameHeightWithSpacing();
			const float height = viewport->WorkSize.y - 2.0f * ImGui::GetFrameHeightWithSpacing();
			ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + listWidth + gap, top), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x - listWidth - gap, height), ImGuiCond_FirstUseEver);
			const std::string windowTitle = area.title + "###jfm_raw_plot_window_" + std::to_string(area.id);
			const bool rendered = m_Windows.Render(windowTitle, plotVisible, 0, [&] {

			ImGui::PushID(static_cast<int>(area.id));
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
				(void)HandleUndoShortcuts();
			if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && area.document->editor.selected.empty() && ImGui::IsKeyPressed(ImGuiKey_F2))
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
				plotVisible = false;
				return;
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
			const CharacteristicId areaActiveId = areaSelection ? areaSelection->selectedCharacteristicId : InvalidCharacteristicId;
			if (area.showOnlyActive && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
			{
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
					SelectAdjacentCurveInWorkspace(area, areaActiveId, 1);
				else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
					SelectAdjacentCurveInWorkspace(area, areaActiveId, -1);
			}
			const std::string curvesTitle = area.title + " Curves###jfm_raw_plot_curves_" + std::to_string(area.id);
			if (area.curvesWindowVisible)
			{
				ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, top), ImGuiCond_FirstUseEver);
				ImGui::SetNextWindowSize(ImVec2(listWidth, height), ImGuiCond_FirstUseEver);
				m_Windows.Render(curvesTitle, area.curvesWindowVisible, 0, [&] {
					if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
						(void)HandleUndoShortcuts();
					if (area.showOnlyActive && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive())
					{
						if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
							SelectAdjacentCurveInWorkspace(area, areaActiveId, 1);
						else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
							SelectAdjacentCurveInWorkspace(area, areaActiveId, -1);
					}
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
							ImGui::SameLine();
							ImGui::Checkbox("Show only active", &area.showOnlyActive);
							ImGui::SameLine();
							if (ImGui::ArrowButton("##active_up", ImGuiDir_Up))
								SelectAdjacentCurveInWorkspace(area, areaActiveId, -1);
							ImGui::SameLine();
							if (ImGui::ArrowButton("##active_down", ImGuiDir_Down))
								SelectAdjacentCurveInWorkspace(area, areaActiveId, 1);
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
				});
			}
			area.groupTreeOpenRequest = 0;

			const std::string plotId = "##jfm_raw_plot_" + std::to_string(area.id);

			const ImPlotFlags plotFlags = area.showLegend ? ImPlotFlags_None : ImPlotFlags_NoLegend;
			JFMPlotExportControls(*area.document);
            sst::ui::plot::PlotSpec figureSpec;
            figureSpec.x.label = "Voltage (V)"; figureSpec.y.label = "Current (A)";
            figureSpec.y.scale = area.scale == PlotScale::SemiLog ? sst::ui::plot::AxisScale::Log10 : sst::ui::plot::AxisScale::Linear;
            figureSpec.legend = {area.showLegend, true, true}; figureSpec.padding=area.fitPadding;
            area.document->SetSpec(figureSpec);
			const bool plotFont = BeginJFMPlotFrame(*area.document);
			if (plotFont && ImPlot::BeginPlot(plotId.c_str(), area.document->preview.size, plotFlags))
			{
				ImPlot::SetupAxes("Voltage (V)", "Current (A)");
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
						if (area.showOnlyActive && id != areaActiveId)
							continue;
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
							sst::ui::plot::IncludeBounds(x[index], y, hasBounds, minX, maxX, minY, maxY);
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

				SetupJFMPlotAxes(*area.document);
				const JFMUserSettings plotSettings = m_Settings != nullptr ? m_Settings->GetSnapshot() : JFMUserSettings{};
				for (const PlotCurveGroup &group : area.groups)
				{
                    area.document->currentGroup=group.key;
					if (!group.visible)
						continue;
					for (CharacteristicId id : group.characteristicIds)
					{
						if (area.showOnlyActive && id != areaActiveId)
							continue;
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
						AddJFMPlotSeries(*area.document, {curveLabel.c_str(), {x.data(), rawY.data(), static_cast<std::size_t>(count)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(rawSpec)}, area.scale == PlotScale::SemiLog);
						const auto fitCurve = m_Store->GetFitCurve({area.project, id});
						if (plotSettings.showFitCurves && fitCurve)
						{
							std::vector<double> fitRangeX;
							std::vector<double> fitRangeY;
							const std::vector<double> *fitX = &fitCurve->voltage;
							const std::vector<double> *fitY = &fitCurve->current;
							if (plotSettings.fitCurvesFitRangeOnly && characteristic->second.info.fit && characteristic->second.info.fit->fitRange.upperVoltage > characteristic->second.info.fit->fitRange.lowerVoltage)
							{
								copyFitRange(*fitCurve, characteristic->second.info.fit->fitRange, fitRangeX, fitRangeY);
								fitX = &fitRangeX;
								fitY = &fitRangeY;
							}
							const std::size_t fitCount = std::min(fitX->size(), fitY->size());
							if (fitCount > 0)
							{
								ImPlotSpec fitSpec;
								fitSpec.LineColor = toVec4(plotSettings.fitCurveColor);
								fitSpec.LineWeight = area.hoveredCharacteristicId == id ? plotSettings.hoveredCurveLineWeight : plotSettings.fitCurveLineWeight;
								fitSpec.Flags = ImPlotItemFlags_NoLegend;
								const std::string fitLabel = "##jfm_raw_fit_" + std::to_string(area.project) + "_" + group.key + "_" + std::to_string(id);
								AddJFMPlotSeries(*area.document, {fitLabel.c_str(), {fitX->data(), fitY->data(), static_cast<std::size_t>(fitCount)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(fitSpec)}, area.scale == PlotScale::SemiLog);
							}
						}
						if (plotSettings.showTunedCurves && characteristic->second.info.temperatureK && (characteristic->second.info.fit || characteristic->second.info.prefit))
						{
							const std::string model = characteristic->second.info.fit ? characteristic->second.info.fit->modelId : characteristic->second.info.prefit->modelId;
							std::vector<double> tunedX = x;
							std::vector<double> tunedY;
							tunedY.reserve(tunedX.size());
							const auto parameters = tunedParameters(characteristic->second.info);
							for (double voltage : tunedX)
								tunedY.push_back(EvaluateJFMCurrent(model, voltage, *characteristic->second.info.temperatureK, parameters));
							if (plotSettings.tunedCurvesFitRangeOnly)
							{
								const JFMFitRange *range = characteristic->second.info.fit ? &characteristic->second.info.fit->fitRange : &characteristic->second.info.prefit->fitRange;
								if (range->upperVoltage > range->lowerVoltage)
								{
									JFMFitCurve tunedCurve{tunedX, tunedY};
									std::vector<double> rangeX;
									std::vector<double> rangeY;
									copyFitRange(tunedCurve, *range, rangeX, rangeY);
									tunedX = std::move(rangeX);
									tunedY = std::move(rangeY);
								}
							}
							const std::size_t tunedCount = std::min(tunedX.size(), tunedY.size());
							if (tunedCount > 0)
							{
								ImPlotSpec tunedSpec;
								tunedSpec.LineColor = toVec4(plotSettings.tunedCurveColor);
								tunedSpec.LineWeight = area.hoveredCharacteristicId == id ? plotSettings.hoveredCurveLineWeight : plotSettings.tunedCurveLineWeight;
								tunedSpec.Flags = ImPlotItemFlags_NoLegend;
								const std::string tunedLabel = "##jfm_raw_tuned_" + std::to_string(area.project) + "_" + group.key + "_" + std::to_string(id);
								AddJFMPlotSeries(*area.document, {tunedLabel.c_str(), {tunedX.data(), tunedY.data(), static_cast<std::size_t>(tunedCount)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(tunedSpec)}, area.scale == PlotScale::SemiLog);
							}
						}
                        // Keep the thinner prefit visible when fit and prefit coincide.
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
								copyFitRange(*curve, prefitState->fitRange, fitRangeX, fitRangeY);
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
								AddJFMPlotSeries(*area.document, {prefitLabel.c_str(), {prefitX->data(), prefitY->data(), static_cast<std::size_t>(curveCount)}, sst::ui::plot::SeriesKind::Line, sst::ui::plot::FromImPlotStyle(prefitSpec)}, area.scale == PlotScale::SemiLog);
							}
						}
					}
				}
				const bool rangeCursor = RenderRangeDragLines(area, records);
				EndJFMPlot(*area.document, figureSpec, plotId.c_str());
				if (rangeCursor)
					ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			}
			else
				FinishRangeUndoGroup(area);
			if (plotFont) ImGui::PopFont();
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
            });
            if (!rendered) FinishRangeUndoGroup(area);

			if (!plotVisible)
			{
				FinishRangeUndoGroup(area);
				m_PlotAreas.erase(m_PlotAreas.begin() + static_cast<std::ptrdiff_t>(areaIndex));
			}
			else
				++areaIndex;
		}
        m_Windows.EndFrame();
	}
} // namespace SST::Prototypes::JFM
