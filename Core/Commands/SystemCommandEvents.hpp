#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"

namespace SST::CoreEvents
{
	struct OpenCommandPaletteRequested final : public BusEvent
	{
	};

	struct ShutdownRequested final : public BusEvent
	{
	};

	struct ProjectSaveRequested final : public BusEvent
	{
	};
} // namespace SST::CoreEvents
