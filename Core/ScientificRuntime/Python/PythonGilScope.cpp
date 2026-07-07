#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonGilScope.hpp"

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

namespace SST
{
	namespace
	{
		std::mutex g_PythonGilScopeMutex;
	}

	PythonGilAcquireScope::PythonGilAcquireScope()
		: m_Lock(g_PythonGilScopeMutex)
	{
#if DS_PYTHON_CAPI_AVAILABLE
		if (PythonInterpreter::IsRuntimeInitialized())
		{
			m_ThreadState = PythonInterpreter::AcquireRuntimeThreadState();
			m_IsActive = m_ThreadState != nullptr;
		}
#endif
		if (!m_IsActive && m_Lock.owns_lock())
			m_Lock.unlock();
	}

	PythonGilAcquireScope::~PythonGilAcquireScope()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		if (m_IsActive)
			PythonInterpreter::ReleaseRuntimeThreadState(m_ThreadState);
#endif
	}

	bool PythonGilAcquireScope::IsActive() const
	{
		return m_IsActive;
	}
} // namespace SST
