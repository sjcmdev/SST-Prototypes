#include "Core/dspch.hpp"

#include <functional>
#include <map>
#include <chrono>
#include <algorithm>
#include <optional>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
#endif

#include "App/Application.hpp"
#include "App/Controllers/ApplicationEventController.hpp"
#include "App/ApplicationInternals.hpp"
#include "Core/CoreLayer.hpp"
#include "IO/IOLayer.hpp"
#include "Core/Logging/LogRegistry.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Threading/ThreadAffinity.hpp"
#include "Core/Utils/Assert.hpp"
#include "Core/Utils/RuntimeTuning.hpp"
#include "Core/Utils/FramebufferScreenshot.hpp"
#include "Presentation/EditorLayer.hpp"
#include "Presentation/ImGuiLayer.hpp"

#include <GLFW/glfw3.h>
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace SST
{
	std::optional<std::reference_wrapper<Application>> Application::s_Instance = std::nullopt;

	// ===== Singleton lifecycle API =====

	Application Application::Create(int argc, char **argv)
	{
		return Application(argc, argv);
	}


	Application Application::Create(int argc, char **argv, ApplicationSetup setup)
	{
		return Application(argc, argv, std::move(setup));
	}


	int Application::Run()
	{
		if (!m_Runtime.lifecycle.IsRunning())
			return 1;

		ApplicationDetail::SetCrashStage("main loop");
		try
		{
			mainLoop();
		}
		catch (const std::exception &exception)
		{
			DS_LOG_CRITICAL("Unhandled exception in main loop: {}", exception.what());
			Logger::Flush();
			ApplicationDetail::AppendCrashMarker("[CRASH] unhandled C++ exception in main loop");
			shutdownInternal();
			return 1;
		}
		catch (...)
		{
			DS_LOG_CRITICAL("Unhandled non-standard exception in main loop");
			Logger::Flush();
			ApplicationDetail::AppendCrashMarker("[CRASH] unhandled unknown exception in main loop");
			shutdownInternal();
			return 1;
		}

		ApplicationDetail::SetCrashStage("shutdown");
		shutdownInternal();
		return 0;
	}


	void Application::Shutdown()
	{
		shutdownInternal();
	}


	void Application::EmitEvent(EventVariant event)
	{
		DS_ASSERT(s_Instance.has_value(), "Application not created");
		s_Instance->get().queueEvent(std::move(event));
	}


	Application &Application::Get()
	{
		DS_ASSERT(s_Instance.has_value(), "Application not created");
		return s_Instance->get();
	}


	EventBus &Application::GetEventBus()
	{
		DS_ASSERT(m_EventBus != nullptr, "EventBus not initialized");
		return *m_EventBus;
	}


	Ref<EventBus> Application::GetEventBusRef() const
	{
		DS_ASSERT(m_EventBus != nullptr, "EventBus not initialized");
		return m_EventBus;
	}


	Notifier &Application::GetNotifier()
	{
		DS_ASSERT(m_Notifier != nullptr, "Notifier not initialized");
		return *m_Notifier;
	}


	Ref<Notifier> Application::GetNotifierRef() const
	{
		DS_ASSERT(m_Notifier != nullptr, "Notifier not initialized");
		return m_Notifier;
	}


	CapabilityService &Application::GetCapabilityService()
	{
		DS_ASSERT(m_CapabilityService != nullptr, "CapabilityService not initialized");
		return *m_CapabilityService;
	}


	AssetManager &Application::GetAssetManager()
	{
		DS_ASSERT(m_AssetManager != nullptr, "AssetManager not initialized");
		return *m_AssetManager;
	}


	Ref<LogRegistry> Application::GetLogRegistry() const
	{
		return m_LogRegistry;
	}


	void Application::ShowBlockingError(const StructuredError &error)
	{
		DS_LOG_CRITICAL("Blocking error: {} [{}]", error.userMessage, error.code);
		if (auto *imguiLayer = m_LayerStack.FindLayer<ImGuiLayer>())
			imguiLayer->SetBlockingError(error);
	}

	IOLayer &Application::GetIOLayer()
	{
		auto ioLayer = m_LayerStack.FindLayerAs<IOLayer>(LayerId::IO).lock();
		DS_ASSERT(ioLayer != nullptr, "IOLayer not initialized");
		return *ioLayer;
	}


	EditorLayer &Application::GetEditorLayer()
	{
		auto editorLayer = m_LayerStack.FindLayerAs<EditorLayer>(LayerId::Editor).lock();
		DS_ASSERT(editorLayer != nullptr, "EditorLayer not initialized");
		return *editorLayer;
	}


	const EditorLayer &Application::GetEditorLayer() const
	{
		auto editorLayer = m_LayerStack.FindLayerAs<EditorLayer>(LayerId::Editor).lock();
		DS_ASSERT(editorLayer != nullptr, "EditorLayer not initialized");
		return *editorLayer;
	}

	JobSystem &Application::GetJobSystem()
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		DS_ASSERT(coreLayer != nullptr, "CoreLayer not initialized");
		return coreLayer->GetJobSystem();
	}


	WeakRef<JobSystem> Application::GetJobSystemHandle() const
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		return coreLayer != nullptr ? coreLayer->GetJobSystemHandle() : WeakRef<JobSystem>{};
	}


	ProgressTracker &Application::GetProgressTracker()
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		DS_ASSERT(coreLayer != nullptr, "CoreLayer not initialized");
		return coreLayer->GetProgressTracker();
	}


	WeakRef<ProgressTracker> Application::GetProgressTrackerHandle() const
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		return coreLayer != nullptr ? coreLayer->GetProgressTrackerHandle() : WeakRef<ProgressTracker>{};
	}


	WeakRef<KeymapResolver> Application::GetKeymapResolverHandle() const
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		return coreLayer != nullptr ? coreLayer->GetKeymapResolverHandle() : WeakRef<KeymapResolver>{};
	}


	WeakRef<ContextManager> Application::GetContextManagerHandle() const
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		return coreLayer != nullptr ? coreLayer->GetContextManagerHandle() : WeakRef<ContextManager>{};
	}


	WeakRef<CommandRegistry> Application::GetCommandRegistryHandle() const
	{
		auto coreLayer = m_LayerStack.FindLayerAs<CoreLayer>(LayerId::Core).lock();
		return coreLayer != nullptr ? coreLayer->GetCommandRegistryHandle() : WeakRef<CommandRegistry>{};
	}


	WeakRef<ConfigManager> Application::GetConfigManager() const
	{
		if (m_ConfigManager == nullptr)
			return {};
		return CreateWeakRef(m_ConfigManager);
	}


	const ApplicationConfig &Application::GetConfig() const
	{
		return m_Config;
	}


	const ApplicationRuntimeServices &Application::GetRuntimeServices() const
	{
		DS_ASSERT(!m_RuntimeServices.eventBus.expired(), "ApplicationRuntimeServices not initialized");
		DS_ASSERT(m_RuntimeServices.config != nullptr, "ApplicationRuntimeServices config not initialized");
		return m_RuntimeServices;
	}


	void Application::PushLayer(Unique<Layer> layer)
	{
		m_LayerStack.PushLayer(std::move(layer));
	}


	void Application::InsertLayerBefore(LayerId before, Unique<Layer> layer)
	{
		m_LayerStack.InsertLayerBefore(before, std::move(layer));
	}


	void Application::PushOverlay(Unique<Layer> overlay)
	{
		m_LayerStack.PushOverlay(std::move(overlay));
	}


	// Extension point - see declaration in Application.hpp.
	// void Application::ProcessQueuedEvents()
	// {
	// 	DS_ASSERT(s_Instance.has_value(), "Application not created");
	// 	s_Instance->get().processPendingEvents();
	// }

	// ===== Instance lifecycle =====

	Application::Application(int argc, char **argv, ApplicationSetup setup)
		: m_ProjectSetup(std::move(setup))
	{
		ApplicationDetail::InstallCrashFallbackHandlers();
		Threading::SetMainThread();
		ApplicationDetail::SetCrashStage("construct application");
		m_LogRegistry = CreateRef<LogRegistry>();
		m_EventQueue.Configure(RuntimeDefaults::EventQueueInitialCapacity, RuntimeDefaults::EventQueueGrowthStep);

		m_Runtime.argc = argc;
		m_Runtime.argv = argv;

		ApplicationSpecification spec = ApplicationDetail::ParseApplicationArguments(m_Runtime.argc,m_Runtime.argv);
		bool created = createFromSpecification(spec);
		if (!created)
			DS_LOG_ERROR("Application creation failed");
	}


	Application::~Application()
	{
		shutdownInternal();
		if (s_Instance.has_value() && &s_Instance->get() == this)
			s_Instance.reset();
	}


	// ===== High-level runtime flow =====

	void Application::mainLoop()
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");

		auto commands = GetCommandRegistryHandle().lock();
		ApplicationDetail::RegisterScreenshotCommand(commands, GetJobSystemHandle().lock());
		if (!m_Runtime.specification.openPath.Empty())
		{
			const auto result = ApplicationDetail::OpenStartupFile(*commands, m_Runtime.specification.openPath);
			if (result)
				DS_LOG_INFO("Startup --open dispatched: {} path={}", result.Value().id.value, m_Runtime.specification.openPath.Utf8());
			else
				DS_LOG_WARN("Startup --open ignored/failed: {} ({})", result.Error().technicalDetails, m_Runtime.specification.openPath.Utf8());
		}

		ImGuiIO &io = ImGui::GetIO();
		const ImVec4 clearColor = ImVec4(
			m_Config.appearance.clearColor[0],
			m_Config.appearance.clearColor[1],
			m_Config.appearance.clearColor[2],
			m_Config.appearance.clearColor[3]);
		m_Runtime.lastFrameTime = glfwGetTime();
		DS_LOG_INFO("Entering render loop");
		TracyMessageL("Entering render loop");

		while (m_Runtime.Running() && !m_Graphics.window->ShouldClose())
			runMainLoopFrame(clearColor, io);
	}


	template <typename TEvent>
	void Application::OnEvent(TEvent &event)
	{
		if (m_Runtime.specification.traceEvents)
		{
			if constexpr (std::is_same_v<TEvent, MouseMovedEvent> || std::is_same_v<TEvent, MouseScrolledEvent>)
			{
				// High-frequency pointer events are intentionally skipped in trace logging.
			}
			else
			{
				DS_LOG_DEBUG("Event: {}", event.GetName());
			}
		}

        if constexpr (std::is_same_v<TEvent, WindowCloseEvent>)
        {
            if (m_CloseGuard && !m_CloseGuard())
            {
                glfwSetWindowShouldClose(m_Graphics.window->GetNativeHandle(), GLFW_FALSE);
                event.handled = true;
                return;
            }
        }
		HandleLifecycleEvent(event, m_Runtime.lifecycle);

		if (event.handled)
			return;

		dispatchEventToLayers(event);
	}


	namespace { // SCAPSLAB_PERF=1: per-layer UI cost, top entries every 2 s
		std::map<std::string, std::pair<double, int>> g_perf;
		std::chrono::steady_clock::time_point g_perfLast = std::chrono::steady_clock::now();
		void perfAdd(const std::string &n, double ms) { auto &e = g_perf[n]; e.first += ms; ++e.second; }
		void perfFlush()
		{
			if (std::chrono::steady_clock::now() - g_perfLast < std::chrono::seconds(2)) return;
			std::vector<std::pair<double, std::string>> v;
			for (auto &[n, e] : g_perf) v.push_back({e.first, n + " x" + std::to_string(e.second)});
			std::sort(v.rbegin(), v.rend());
			std::string out = "[perf] top layers (total ms/2s):";
			for (size_t i = 0; i < v.size() && i < 6; ++i) out += " | " + v[i].second + "=" + std::to_string(int(v[i].first));
			std::fprintf(stderr, "%s\n", out.c_str());
			g_perf.clear(); g_perfLast = std::chrono::steady_clock::now();
		}
	}

	void PerfAdd(const std::string &name, double ms) { perfAdd(name, ms); }

	void Application::runMainLoopFrame(const ImVec4 &clearColor, ImGuiIO &io)
	{
		static const bool perf = std::getenv("SCAPSLAB_PERF") != nullptr;
		const double now = glfwGetTime();
		const float deltaTime = static_cast<float>(now - m_Runtime.lastFrameTime);
		m_Runtime.lastFrameTime = now;


		onUpdate(deltaTime);

		beginFrame();
        for (const auto &layer : m_LayerStack) {
            if(m_FailedLayers.contains(layer.get())) continue;
            ImGuiErrorRecoveryState saved;ImGui::ErrorRecoveryStoreState(&saved);
            const auto lt0 = std::chrono::steady_clock::now();
            try {layer->OnImGuiRender();}
            catch(const std::exception &error) {
                const bool assertRecovery=io.ConfigErrorRecoveryEnableAssert;io.ConfigErrorRecoveryEnableAssert=false;
                ImGui::ErrorRecoveryTryToRecoverState(&saved);io.ConfigErrorRecoveryEnableAssert=assertRecovery;
                handleLayerException(*layer,error.what());
            }
            if (perf) perfAdd(layer->GetName(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lt0).count());
        }
		const auto rt0 = std::chrono::steady_clock::now();
		onRender(clearColor, io.Framerate);
		if (perf) { perfAdd("<onRender(draw+swap)>", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rt0).count()); perfFlush(); }
	}


    void Application::handleLayerException(const Layer &layer,const std::string &message) {
        m_FailedLayers.insert(&layer);
        const auto details=layer.GetName()+": "+message;
        DS_LOG_CRITICAL("Unhandled layer exception: {}",details);Logger::Flush();
        ApplicationDetail::AppendCrashMarker(details.c_str());
        if(auto ui=m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock())
            ui->SetBlockingError(StructuredError{ErrorCategory::Internal,Severity::Error,
                "An application component failed. Your project is still in memory.",details,
                "The failing component has been disabled. Save your project and restart the application.",layer.GetName(),"layer_exception"});
    }

	void Application::onUpdate(float deltaTime)
	{
        for (const auto &layer : m_LayerStack) {
            if(m_FailedLayers.contains(layer.get())) continue;
            try {layer->OnUpdate(deltaTime);}
            catch(const std::exception &error) {handleLayerException(*layer,error.what());}
        }

		const double wait = m_IdleWait ? m_IdleWait() : 0.0;
		if (wait > 0.0 && glfwGetTime() > m_ActiveUntil)
		{
			const double t0 = glfwGetTime();
			glfwWaitEventsTimeout(wait);
			if (glfwGetTime() - t0 < wait * 0.9) m_ActiveUntil = glfwGetTime() + 0.4; // input woke us: keep rendering briefly (hover/animation settle)
		}
		else
			m_Graphics.window->PollEvents();
		processPendingEvents();
	}


	void Application::queueEvent(EventVariant event)
	{
		m_EventQueue.Add(std::move(event));
	}


	void Application::processPendingEvents()
	{
		std::vector<EventVariant> events = m_EventQueue.Drain();
		if (events.empty())
			return;

		for (auto &variant : events)
		{
			std::visit([this](auto &event) { OnEvent(event); }, variant);
		}
	}

	void Application::onRender(const ImVec4 &clearColor, float frameRate)
	{
		renderFrame(clearColor, frameRate);
	}


	template <typename TEvent>
	void Application::dispatchEventToLayers(TEvent &event)
	{
		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			auto &layer = *it;
			layer->OnEvent(event);
			if (event.handled)
				break;
		}
	}


	void Application::beginFrame()
	{
		auto imGuiLayer = m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock();
		DS_ASSERT(imGuiLayer != nullptr, "ImGuiLayer not initialized");
		imGuiLayer->BeginFrame();
	}


	void Application::renderFrame(const ImVec4 &clearColor, float frameRate)
	{
		DS_ASSERT(m_Graphics.window != nullptr, "Main window was not created");
		(void)frameRate;

		int displayWidth = 0;
		int displayHeight = 0;
		m_Graphics.window->GetFramebufferSize(displayWidth, displayHeight);
		glViewport(0, 0, displayWidth, displayHeight);
		glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		if (auto imGuiLayer = m_LayerStack.FindLayerAs<ImGuiLayer>(LayerId::ImGui).lock())
		{
			imGuiLayer->EndFrame();
			imGuiLayer->Render();
		}
		TracyPlot("FPS", frameRate);
		if (!m_Runtime.specification.screenshotPath.Empty() &&
		    ++m_Runtime.renderedFrames >= m_Runtime.specification.screenshotAfterFrames)
		{
			// ImGui restores the main context after rendering detached viewports.
			glfwMakeContextCurrent(m_Graphics.window->GetNativeHandle());
			CommandContext context;
			context.SetSource("CLI screenshot");
			context.Set("path", m_Runtime.specification.screenshotPath);
			const auto result = GetCommandRegistryHandle().lock()->Execute(CommandID{"app.screenshot"}, std::move(context));
			if (!result) throw std::runtime_error(result.Error().technicalDetails);
			DS_LOG_INFO("Screenshot written: {}", m_Runtime.specification.screenshotPath.Utf8());
			m_Runtime.specification.screenshotPath = {};
            if (m_Runtime.specification.exitAfterScreenshot)
                m_Runtime.lifecycle.SetRunning(false);
		}

		m_Graphics.window->SwapBuffers();
		FrameMark;
	}


	template void Application::OnEvent<WindowCloseEvent>(WindowCloseEvent &);
	template void Application::OnEvent<WindowResizeEvent>(WindowResizeEvent &);
	template void Application::OnEvent<KeyPressedEvent>(KeyPressedEvent &);
	template void Application::OnEvent<KeyReleasedEvent>(KeyReleasedEvent &);
	template void Application::OnEvent<KeyRepeatedEvent>(KeyRepeatedEvent &);
	template void Application::OnEvent<MouseButtonPressedEvent>(MouseButtonPressedEvent &);
	template void Application::OnEvent<MouseButtonReleasedEvent>(MouseButtonReleasedEvent &);
	template void Application::OnEvent<MouseMovedEvent>(MouseMovedEvent &);
	template void Application::OnEvent<MouseScrolledEvent>(MouseScrolledEvent &);
	template void Application::OnEvent<TouchpadGestureEvent>(TouchpadGestureEvent &);
} // namespace SST
