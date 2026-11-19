#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace SST::Prototypes::JFM
{
	class MonteCarloCurvesPanel final : public IPanel, public EventReceiver
	{
	public:
		MonteCarloCurvesPanel(Ref<JFMWorkspaceStore> store, Ref<EventBus> eventBus, std::string title = "MC Curves", bool visibleByDefault = false);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		void AddPlot(JFMCharacteristicRef ref);

		Ref<JFMWorkspaceStore> m_Store;
		Ref<EventBus> m_EventBus;
		std::vector<JFMCharacteristicRef> m_Plots;
		bool m_ShowIdeal = true;
		bool m_ShowNoisy = true;
		bool m_ShowFitted = true;
        bool m_ShowRejected = false;
		bool m_SemiLog = false;
		bool m_SingleIteration = false;
		int m_Iteration = 0;
	};
} // namespace SST::Prototypes::JFM
