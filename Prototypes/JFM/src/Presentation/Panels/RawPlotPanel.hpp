#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/Commands/CommandContext.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Memory.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace SST
{
	class CommandRegistry;
	class UndoStack;
}

namespace SST::Prototypes::JFM
{
	enum class PlotScale
	{
		Linear,
		SemiLog
	};

	class RawPlotPanel final : public IPanel, public EventReceiver
	{
	public:
		explicit RawPlotPanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<SST::EventBus> eventBus,
			WeakRef<CommandRegistry> commandRegistry,
			WeakRef<UndoStack> undoStack,
			std::string title = "Raw Plot",
			bool visibleByDefault = true);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		struct PlotCurveGroup
		{
			std::string key;
			std::string title;
			std::vector<CharacteristicId> characteristicIds;
			std::vector<CharacteristicId> hiddenCharacteristicIds;
			std::unordered_map<CharacteristicId, std::array<float, 4>> colors;
			std::string groupingId;
			std::string bucketKey;
			std::uint64_t resolvedRevision = 0;
			bool visible = true;
			std::array<float, 4> gradientStart = {0.15f, 0.75f, 1.00f, 1.00f};
			std::array<float, 4> gradientEnd = {1.00f, 0.12f, 0.55f, 1.00f};
		};

		struct PlotArea
		{
			std::uint64_t id = 0;
			ProjectHandle project = InvalidProjectHandle;
			std::string title;
			std::vector<PlotCurveGroup> groups;
			PlotScale scale = PlotScale::Linear;
			bool showLegend = false;
			bool autoFit = true;
			float fitPadding = 0.08f;
			bool renaming = false;
			bool curvesWindowVisible = true;
			int groupTreeOpenRequest = 0;
			CharacteristicId hoveredCharacteristicId = InvalidCharacteristicId;
			CharacteristicId rangeCharacteristic = InvalidCharacteristicId;
			double rangeLower = 0.0;
			double rangeUpper = 0.0;
			bool rangeInitialized = false;
			bool rangeUndoGroupOpen = false;
			bool rangeHeldLastFrame = false;
			std::array<char, 128> renameBuffer = {};
		};

		class PlotStateCommand;

		[[nodiscard]] PlotArea CreatePlotArea(ProjectHandle project = InvalidProjectHandle);
		[[nodiscard]] PlotCurveGroup CreateCurveGroup(JFMPlotCurveGroupRequest request) const;
		void Execute(const char *commandId, CommandContext context = {}) const;
		void PlotRequest(PlotRequested request);
		void PlotIds(std::vector<CharacteristicId> ids, std::string title = {}, ProjectHandle project = InvalidProjectHandle);
		void PlotSeries(const std::string &seriesId);
		void PlotSelectedGroup();
		void ApplyColorRange(PlotCurveGroup &group);
		void RefreshDynamicGroups(PlotArea &area);
		void SetGroupVisible(PlotCurveGroup &group, bool visible);
		void SetGroupCharacteristicsVisible(PlotCurveGroup &group, bool visible);
		void FinishRangeUndoGroup(PlotArea &area, bool cancel = false);
		void PushPlotUndo(std::vector<PlotArea> before, std::string description);
		bool HandleUndoShortcuts();
		void SelectCurveInWorkspace(const PlotArea &area, const PlotCurveGroup &group, CharacteristicId id);
		void RenderRangeDragLines(PlotArea &area, const std::unordered_map<CharacteristicId, JFMCharacteristicRecord> &records);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<SST::EventBus> m_EventBus;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<UndoStack> m_UndoStack;
		std::vector<PlotArea> m_PlotAreas;
		std::unordered_map<std::string, std::array<float, 4>> m_ColorEditBefore;
		std::uint64_t m_NextPlotAreaId = 1;
	};
} // namespace SST::Prototypes::JFM
