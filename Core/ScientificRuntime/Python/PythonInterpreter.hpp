#pragma once

#include "Core/Diagnostics/StructuredError.hpp"
#include "ScientificRuntime/Python/PythonApi.hpp"
#include "ScientificRuntime/Python/PythonBridgeBuildConfig.hpp"

namespace SST
{
	class PythonInterpreter final
	{
	public:
		PythonInterpreter() = default;
		~PythonInterpreter();

		[[nodiscard]] Result<void> Start();
		void Stop();

		[[nodiscard]] bool IsRunning() const;
		[[nodiscard]] static bool IsEmbeddingAvailable();
		[[nodiscard]] static bool IsRuntimeInitialized();

#if DS_PYTHON_CAPI_AVAILABLE
		[[nodiscard]] static PyThreadState *AcquireRuntimeThreadState();
		static void ReleaseRuntimeThreadState(PyThreadState *threadState);
#endif

	private:
#if DS_PYTHON_CAPI_AVAILABLE
		PyThreadState *m_MainThreadState = nullptr;
#endif
		bool m_IsRunning = false;
	};
} // namespace SST
