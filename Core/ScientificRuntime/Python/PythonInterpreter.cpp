#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Core/Logging/Logger.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "Core/Utils/FramebufferScreenshot.hpp"
#include "Presentation/Panels/ViewHost.hpp"

namespace SST
{
	std::atomic_bool g_PythonRuntimeInitialized{false};

#if DS_PYTHON_CAPI_AVAILABLE
	PyThreadState *g_PythonRuntimeThreadState = nullptr;

#endif

	PythonInterpreter::~PythonInterpreter()
	{
		Stop();
	}

	Result<void> PythonInterpreter::Start()
	{
		if (m_IsRunning)
			return {};

#if DS_PYTHON_CAPI_AVAILABLE
		if (!Py_IsInitialized())
		{
			ApplicationDetail::RegisterApplicationPythonModule();
			RegisterViewPythonModule();
		}

        if (!Py_IsInitialized())
        {
            const Path executable = Platform::ResolvePreferredPythonExecutable();
            if (executable == Path("python"))
                return MakePythonUnavailableError("Project Python runtime was not found.",
                    "No project .venv or bundled interpreter exists.", "Run uv sync --locked in the project.", "python.embed.runtime_missing");
            PyConfig config;
            PyConfig_InitPythonConfig(&config);
            config.use_environment = 0;
            config.install_signal_handlers = 0;
            const auto path = executable.wstring();
            auto status = PyConfig_SetString(&config, &config.program_name, path.c_str());
            if (!PyStatus_Exception(status)) status = PyConfig_SetString(&config, &config.executable, path.c_str());
            if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
            const std::string error = PyStatus_Exception(status) && status.err_msg ? status.err_msg : "";
            PyConfig_Clear(&config);
            if (PyStatus_Exception(status))
                return MakePythonUnavailableError("Python interpreter could not be initialized.", error,
                    "Run uv sync --locked in the project.", "python.embed.initialize_failed");
            DS_LOG_INFO("PythonInterpreter using project runtime at {}", executable.Utf8());
        }

		if (!Py_IsInitialized())
		{
			return MakePythonUnavailableError(
				"Python interpreter could not be initialized.",
				"Py_InitializeEx() returned but Py_IsInitialized() is false.",
				"Verify install/app/python runtime content and regenerate projects after setup.",
				"python.embed.initialize_failed");
		}

		m_MainThreadState = PyEval_SaveThread();
		g_PythonRuntimeThreadState = m_MainThreadState;
		if (m_MainThreadState == nullptr)
		{
			return MakePythonUnavailableError(
				"Python interpreter lock setup failed.",
				"PyThreadState_Get() returned null thread state.",
				"Ensure the interpreter is initialized on the main thread before background usage.",
				"python.embed.thread_state_failed");
		}

		m_IsRunning = true;
		g_PythonRuntimeInitialized.store(true, std::memory_order_relaxed);
		DS_LOG_INFO("PythonInterpreter started (embedded mode)");
		return {};
#else
		return MakePythonUnavailableError(
			"Embedded Python runtime is not available in this build.",
			"Build-time flag DS_PYTHON_CAPI_AVAILABLE is 0.",
			"Prepare install/app/python runtime and regenerate project files to enable Python embedding.",
			"python.embed.unavailable");
#endif
	}

	void PythonInterpreter::Stop()
	{
		if (!m_IsRunning)
			return;

#if DS_PYTHON_CAPI_AVAILABLE
		// ponytail: numpy/scipy extension modules are not reliably restartable after Py_FinalizeEx().
		g_PythonRuntimeThreadState = nullptr;
		m_MainThreadState = nullptr;
#endif

		m_IsRunning = false;
		g_PythonRuntimeInitialized.store(false, std::memory_order_relaxed);
		DS_LOG_INFO("PythonInterpreter stopped");
	}

	bool PythonInterpreter::IsRunning() const
	{
		return m_IsRunning;
	}

	bool PythonInterpreter::IsEmbeddingAvailable()
	{
#if DS_PYTHON_CAPI_AVAILABLE
		return true;
#else
		return false;
#endif
	}

	bool PythonInterpreter::IsRuntimeInitialized()
	{
		return g_PythonRuntimeInitialized.load(std::memory_order_relaxed);
	}

#if DS_PYTHON_CAPI_AVAILABLE
	PyGILState_STATE PythonInterpreter::AcquireRuntimeGil()
	{
        // Stop() leaves the interpreter alive, so a concurrent shutdown cannot invalidate this GIL acquisition.
		return PyGILState_Ensure();
	}

	void PythonInterpreter::ReleaseRuntimeGil(PyGILState_STATE state)
	{
		PyGILState_Release(state);
	}
#endif
} // namespace SST
