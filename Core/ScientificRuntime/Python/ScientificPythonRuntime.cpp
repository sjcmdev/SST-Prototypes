#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScientificPythonRuntime.hpp"

namespace SST
{
	Result<void> ScientificPythonRuntime::Initialize()
	{
		Result<void> startResult = m_Interpreter.Start();
		if (!startResult)
			return startResult.Error();

		m_IsReady = true;
		return {};
	}

	void ScientificPythonRuntime::Shutdown()
	{
		m_Interpreter.Stop();
		m_IsReady = false;
	}

	bool ScientificPythonRuntime::IsReady() const
	{
		return m_IsReady;
	}
} // namespace SST
