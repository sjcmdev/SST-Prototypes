#pragma once

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/JobSystem/JobSystemConfig.hpp"

namespace SST
{
	struct JobSystemConfigAppliedEvent final : public BusEvent
	{
		explicit JobSystemConfigAppliedEvent(JobsConfig jobsConfig)
			: jobs(jobsConfig)
		{
		}

		JobsConfig jobs;
	};
} // namespace SST
