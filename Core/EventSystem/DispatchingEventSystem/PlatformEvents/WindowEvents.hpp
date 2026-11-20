#pragma once

#include "Core/EventSystem/DispatchingEventSystem/PlatformEvents/PlatformEventBase.hpp"
#include "Core/Utils/Path.hpp"
#include <vector>

namespace SST
{
    class FilesDroppedEvent final : public Event
    {
    public:
        explicit FilesDroppedEvent(std::vector<Path> paths) : paths(std::move(paths)) {}
        std::vector<Path> paths;
        DS_EVENT_CLASS_TYPE(FilesDropped)
        DS_EVENT_CLASS_CATEGORY(EventCategoryApplication)
    };
	class WindowCloseEvent final : public Event
	{
	public:
		WindowCloseEvent() = default;

		DS_EVENT_CLASS_TYPE(WindowClose)
		DS_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class WindowResizeEvent final : public Event
	{
	public:
		explicit WindowResizeEvent(int width, int height) : m_Width(width), m_Height(height)
		{
		}

		[[nodiscard]] int GetWidth() const
		{
			return m_Width;
		}
		[[nodiscard]] int GetHeight() const
		{
			return m_Height;
		}

		DS_EVENT_CLASS_TYPE(WindowResize)
		DS_EVENT_CLASS_CATEGORY(EventCategoryApplication)

	private:
		int m_Width;
		int m_Height;
	};
} // namespace SST
