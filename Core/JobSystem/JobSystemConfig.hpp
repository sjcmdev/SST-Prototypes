#pragma once

namespace SST
{
	struct JobsConfig
	{
		int defaultWorkerThreadCount = 1;
		bool reserveUrgentWorker = true;
	};
} // namespace SST
