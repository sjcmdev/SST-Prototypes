#pragma once

#include "Core/Utils/Memory.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Presentation/Panels/EditorWindow.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	class ImportStatusPanel final : public EditorWindow, public EventReceiver
	{
	public:
		explicit ImportStatusPanel(Ref<JFMWorkspaceStore> store, std::string title = "Import Status", bool visibleByDefault = true, Ref<EventBus> eventBus = {});

		[[nodiscard]] WindowDuplicate Duplicate() const override;
		void RenderContent() override;

	private:
		Ref<JFMWorkspaceStore> m_Store;
        Ref<EventBus> m_EventBus;
	};
} // namespace SST::Prototypes::JFM
