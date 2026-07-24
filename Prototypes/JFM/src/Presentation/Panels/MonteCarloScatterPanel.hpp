#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/JFMFitMonteCarloWidgets.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <cstdint>
#include <optional>
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
			bool open = true;
			bool remove = false;
			bool needsInitialPlacement = false;
			int initialPlacement = 0;
			bool needsDock = false;
			int dockSlot = -1;
		};

		struct PlotGroup
		{
			std::uint64_t id = 0;
			std::string title;
			std::vector<PlotState> plots;
		};

		void AddGroup(std::string title = {});
		void AddPlot(JFMCharacteristicRef ref);
		void AddPlot(JFMCharacteristicRef ref, int xParameter, int yParameter, int initialPlacement = -1, int dockSlot = -1, bool open = true);
		void AddPlots(ProjectHandle project, const std::vector<CharacteristicId> &ids);
		void AddDefaultPlotsForCharacteristic(ProjectHandle project, const JFMCharacteristicInfo &info, int &placement);
		void InitializeAxes(PlotState &plot, const JFMMonteCarloResult &result) const;
		[[nodiscard]] std::string PlotWindowTitle(const PlotState &plot, const JFMMonteCarloResult &result) const;
		[[nodiscard]] std::string PlotDataFileName(const PlotState &plot, const JFMMonteCarloResult &result, bool uncertaintyOnly) const;
		[[nodiscard]] bool ExportPlot(const PlotState &plot, bool uncertaintyOnly, std::optional<Path> directory = std::nullopt) const;
		[[nodiscard]] bool ExportPlotToFile(const PlotState &plot, bool uncertaintyOnly, const Path &path) const;
		[[nodiscard]] bool SavePlotImage(const PlotState &plot, std::optional<Path> outputPath = std::nullopt) const;
		[[nodiscard]] bool SaveAllPlotImages(const Path &directory);
		[[nodiscard]] Path OutputDirectory(JFMCharacteristicRef ref) const;
		[[nodiscard]] std::string LastSavePlotError() const;
		void RenderAddPlotControls(ProjectHandle project);
		void RenderDockspace();
		void RenderPlotWindow(PlotState &plot);
		void SetStatus(std::string action, int saved, int total);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMUserSettingsStore> m_Settings;
		Ref<EventBus> m_EventBus;
		std::vector<PlotGroup> m_Groups;
		std::size_t m_ActiveGroup = 0;
		std::uint64_t m_ActivePlot = 0;
		std::uint64_t m_NextGroupId = 1;
		std::uint64_t m_NextPlotId = 1;
		unsigned int m_DockspaceId = 0;
		unsigned int m_LeftDockId = 0;
		unsigned int m_RightDockId = 0;
		bool m_RebuildDockLayout = false;
		CharacteristicId m_AddPlotCharacteristic = InvalidCharacteristicId;
		int m_AddPlotPair = 0;
		mutable std::string m_LastSavePlotError;
		std::string m_Status;
	};
} // namespace SST::Prototypes::JFM
