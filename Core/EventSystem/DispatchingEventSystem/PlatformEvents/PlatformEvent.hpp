#pragma once

#include <variant>

#include "Core/dspch.hpp"

// Base event type interface and macros
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"

// Concrete event implementations
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/WindowEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/KeyboardEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/MouseEvents.hpp"
#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/TouchpadEvents.hpp"

namespace SST
{
	// Compile-time closed event type set for EventQueue variant storage
	using EventVariant = std::variant<
		WindowCloseEvent,
		WindowResizeEvent,
		FilesDroppedEvent,
		KeyPressedEvent,
		KeyReleasedEvent,
		KeyRepeatedEvent,
		MouseButtonPressedEvent,
		MouseButtonReleasedEvent,
		MouseMovedEvent,
		MouseScrolledEvent,
		TouchpadGestureEvent
	>;

	// Compile-time guard — aktualizuj gdy dodajesz nowy typ eventu
	static_assert(std::variant_size_v<EventVariant> == 11,
		"EventVariant: dodano nowy typ eventu — zaktualizuj alias i ten assert");

} // namespace SST
