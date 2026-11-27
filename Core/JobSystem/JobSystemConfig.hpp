#pragma once

#include <thread>

namespace SST
{
	inline int DefaultComputeWorkerCount()
	{
		const auto logicalCpus = std::thread::hardware_concurrency();
		return logicalCpus > 1 ? static_cast<int>(logicalCpus - 1) : 1;
	}

	struct JobsConfig
	{
		bool operator==(const JobsConfig &) const = default;
		int defaultWorkerThreadCount = DefaultComputeWorkerCount();
		bool reserveUrgentWorker = true;
	};
} // namespace SST
