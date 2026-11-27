#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/Panels/EditorWindow.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace SST::Prototypes::JFM
{
	class MonteCarloCurvesPanel final : public EditorWindow, public EventReceiver
	{
	public:
		MonteCarloCurvesPanel(Ref<JFMWorkspaceStore> store, Ref<EventBus> eventBus, std::string title = "MC Curves", bool visibleByDefault = false);

		[[nodiscard]] WindowDuplicate Duplicate() const override;
		void RenderContent() override;
        int WindowFlags() const override;

	private:
		void AddPlot(JFMCharacteristicRef ref);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<EventBus> m_EventBus;
		struct PlotPreview
		{
			JFMCharacteristicRef ref;
			std::weak_ptr<const JFMMonteCarloResult> result;
			std::string modelId;
			double temperatureK = 0.0;
			int iteration = -1;
			std::vector<std::vector<double>> fitted, noisy;
		};
		std::vector<PlotPreview> m_Plots;
		bool m_ShowIdeal = true;
		bool m_ShowNoisy = true;
		bool m_ShowFitted = true;
        bool m_ShowRejected = false;
		bool m_SemiLog = false;
		bool m_SingleIteration = false;
		int m_Iteration = 0;
	};
} // namespace SST::Prototypes::JFM
