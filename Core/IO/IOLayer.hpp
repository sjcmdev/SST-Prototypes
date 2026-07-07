#pragma once

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"

namespace SST
{
	class EventBus;

	namespace AppEvents::Config
	{
		struct PersistRequested;
	}

	namespace AppEvents::Keymap
	{
		struct BindingsLoadRequested;
		struct BindingsSaveRequested;
	}

	namespace EditorUiEvents
	{
		struct LayoutListRequested;
		struct LayoutLoadRequested;
		struct PersistRequested;
		struct ThemeLoadRequested;
		struct ThemeSaveRequested;
	}

	class IOLayer final : public Layer, public EventReceiver
	{
	public:
		explicit IOLayer(Ref<EventBus> eventBus);

		void OnDetach() override;

	private:
		void bindEvents();
		void onConfigPersistRequested(const AppEvents::Config::PersistRequested &event);
		void onUiPersistRequested(const EditorUiEvents::PersistRequested &event);
		void onThemeSaveRequested(const EditorUiEvents::ThemeSaveRequested &event);
		void onThemeLoadRequested(const EditorUiEvents::ThemeLoadRequested &event);
		void onLayoutLoadRequested(const EditorUiEvents::LayoutLoadRequested &event);
		void onLayoutListRequested(const EditorUiEvents::LayoutListRequested &event);
		void onBindingsSaveRequested(const AppEvents::Keymap::BindingsSaveRequested &event);
		void onBindingsLoadRequested(const AppEvents::Keymap::BindingsLoadRequested &event);

	private:
		Ref<EventBus> m_EventBus;
	};
} // namespace SST
