#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonGilScope.hpp"

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

namespace SST
{
	PythonGilAcquireScope::PythonGilAcquireScope()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		if (PythonInterpreter::IsRuntimeInitialized())
		{
			m_State = PythonInterpreter::AcquireRuntimeGil();
			m_IsActive = true;
		}
#endif
	}

	PythonGilAcquireScope::~PythonGilAcquireScope()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		if (m_IsActive)
			PythonInterpreter::ReleaseRuntimeGil(m_State);
#endif
	}

	bool PythonGilAcquireScope::IsActive() const
	{
		return m_IsActive;
	}
} // namespace SST
