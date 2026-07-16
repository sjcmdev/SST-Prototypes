#pragma once

#include "App/JFMUserSettings.hpp"
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
			int groupTreeOpenRequest = 0;
			std::array<char, 128> renameBuffer = {};
		};

		[[nodiscard]] PlotArea CreatePlotArea(ProjectHandle project = InvalidProjectHandle);
		[[nodiscard]] PlotCurveGroup CreateCurveGroup(JFMPlotCurveGroupRequest request) const;
		void PlotRequest(PlotRequested request);
		void PlotIds(std::vector<CharacteristicId> ids, std::string title = {}, ProjectHandle project = InvalidProjectHandle);
		void PlotSeries(const std::string &seriesId);
		void PlotSelectedGroup();
		void ApplyColorRange(PlotCurveGroup &group);
		void RefreshDynamicGroups(PlotArea &area);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<SST::EventBus> m_EventBus;
		std::vector<PlotArea> m_PlotAreas;
		std::uint64_t m_NextPlotAreaId = 1;
	};
} // namespace SST::Prototypes::JFM
