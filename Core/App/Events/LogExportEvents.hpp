#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"

namespace SST::AppEvents::Logs
{
	struct ExportRequested final : public BusEvent
	{
		std::vector<LogEntry> entries;
		Path targetPath;
	};

	struct ExportCompleted final : public BusEvent
	{
		Path targetPath;
		std::size_t bytes = 0;
	};

	struct ExportFailed final : public BusEvent
	{
		Path targetPath;
		std::string error;
	};
} // namespace SST::AppEvents::Logs
