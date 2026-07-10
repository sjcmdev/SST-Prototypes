#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/ScientificPythonRuntime.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "ScientificRuntime/Python/PymatgenConversion.hpp"

namespace SST
{
	Result<void> ScientificPythonRuntime::Initialize()
	{
		Result<void> startResult = m_Interpreter.Start();
		if (!startResult)
			return startResult.Error();

		Result<void> warmUpResult = m_PymatgenBridge.WarmUp();
		if (!warmUpResult)
		{
			DS_LOG_WARN(
				"ScientificPythonRuntime: PymatgenBridge warm-up skipped — subprocess fallback active. [{}] {}",
				warmUpResult.Error().code,
				warmUpResult.Error().technicalDetails);
		}

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

	Result<ScriptRunResult> ScientificPythonRuntime::RunBridgeRoundtripDemo() const
	{
		if (!m_IsReady)
		{
			return StructuredError{
				ErrorCategory::Python,
				Severity::Error,
				"Python runtime is not initialized.",
				"ScientificPythonRuntime::RunBridgeRoundtripDemo called before Initialize().",
				"Initialize ScientificPythonRuntime in ScientificRuntimeLayer before running bridge scripts.",
				"ScientificRuntime/Python",
				"python.runtime.not_initialized"};
		}

		ScriptRunOptions options;
		options.scriptPath = Path("scripts") / "python" / "examples" / "bridge_roundtrip_demo.py";
		options.workingDirectory = FileSystem::CurrentPath();
		return m_ScriptRunner.RunFile(options);
	}

} // namespace SST
