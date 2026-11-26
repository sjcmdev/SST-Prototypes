#pragma once

namespace SST
{
	struct JobsConfig
	{
		bool operator==(const JobsConfig &) const = default;
		int defaultWorkerThreadCount = 1;
		bool reserveUrgentWorker = true;
	};
} // namespace SST
