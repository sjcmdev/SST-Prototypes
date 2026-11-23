#include "Core/dspch.hpp"

#include "ScientificRuntime/Python/PythonInterpreter.hpp"

#include "Core/Platform/PlatformPythonRuntime.hpp"
#include "Core/Logging/Logger.hpp"
#include "ScientificRuntime/Python/PythonErrors.hpp"
#include "Core/Utils/FramebufferScreenshot.hpp"

namespace SST
{
	std::atomic_bool g_PythonRuntimeInitialized{false};

#if DS_PYTHON_CAPI_AVAILABLE
	PyThreadState *g_PythonRuntimeThreadState = nullptr;
	// Main-thread state whose GIL was released at first Start(). Python stays initialized after Stop()
	// (extensions are not restartable), so a later Start() must reuse it instead of releasing the GIL again.
	PyThreadState *g_ReleasedMainThreadState = nullptr;

	static void configureEmbeddedPythonHome()
	{
		const Path pythonHome = Platform::ResolveEmbeddedPythonHome();
		if (pythonHome.Empty())
			return;

		Platform::SetPythonHomeEnvironment(pythonHome);
		DS_LOG_INFO("PythonInterpreter using app-local runtime at {}", pythonHome.Utf8());
	}
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
			configureEmbeddedPythonHome();
		}

		if (!Py_IsInitialized())
			Py_InitializeEx(0);

		if (Py_IsInitialized() && !g_ReleasedMainThreadState)
		{
			const Path sitePackages = Platform::ResolveEmbeddedPythonSitePackages();
			if (!sitePackages.Empty())
			{
				std::string dir = sitePackages.Utf8();
				std::replace(dir.begin(), dir.end(), '\\', '/');
				PyRun_SimpleString(("import site; site.addsitedir(r'" + dir + "')").c_str());
				DS_LOG_INFO("PythonInterpreter added venv site-packages {}", dir);
			}
		}

		if (!Py_IsInitialized())
		{
			return MakePythonUnavailableError(
				"Python interpreter could not be initialized.",
				"Py_InitializeEx() returned but Py_IsInitialized() is false.",
				"Verify install/app/python runtime content and regenerate projects after setup.",
				"python.embed.initialize_failed");
		}

		m_MainThreadState = g_ReleasedMainThreadState ? g_ReleasedMainThreadState : PyEval_SaveThread();
		g_ReleasedMainThreadState = m_MainThreadState;
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
