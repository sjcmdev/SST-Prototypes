#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace SST::Prototypes::JFM
{
	class MonteCarloScatterPanel final : public IPanel, public EventReceiver
	{
	public:
		MonteCarloScatterPanel(
			Ref<JFMWorkspaceStore> store,
			Ref<JFMUserSettingsStore> settings,
			Ref<EventBus> eventBus,
			std::string title = "MC Scatter",
			bool visibleByDefault = false);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		struct PlotState
		{
			std::uint64_t id = 0;
			JFMCharacteristicRef ref;
			JFMMonteCarloScatterPlotOptions options;
			bool axesInitialized = false;
		};

		struct PlotGroup
		{
			std::uint64_t id = 0;
			std::string title;
			std::vector<PlotState> plots;
		};

		void AddGroup();
		void AddPlot(JFMCharacteristicRef ref);
		void AddPlots(ProjectHandle project, const std::vector<CharacteristicId> &ids);
		void InitializeAxes(PlotState &plot, const JFMMonteCarloResult &result) const;
		[[nodiscard]] bool ExportPlot(const PlotState &plot, bool uncertaintyOnly) const;
		[[nodiscard]] Path OutputDirectory(JFMCharacteristicRef ref) const;
		void SetStatus(std::string action, int saved, int total);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<EventBus> m_EventBus;
		std::vector<PlotGroup> m_Groups;
		std::size_t m_ActiveGroup = 0;
		std::uint64_t m_ActivePlot = 0;
		std::uint64_t m_NextGroupId = 1;
		std::uint64_t m_NextPlotId = 1;
		std::string m_Status;
	};
} // namespace SST::Prototypes::JFM
