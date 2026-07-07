#pragma once

#include "Core/JobSystem/JobSystemTypes.hpp"
#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

#include <mutex>

namespace SST
{
	class PythonGilAcquireScope final : public IJobExecutionGuard
	{
	public:
		PythonGilAcquireScope();
		~PythonGilAcquireScope() override;

		[[nodiscard]] bool IsActive() const;

	private:
		std::unique_lock<std::mutex> m_Lock;
#if DS_PYTHON_CAPI_AVAILABLE
		PyGILState_STATE m_State = PyGILState_UNLOCKED;
#endif
		bool m_IsActive = false;
	};
} // namespace SST
