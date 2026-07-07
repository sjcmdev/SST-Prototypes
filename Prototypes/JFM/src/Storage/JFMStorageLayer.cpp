#include "Core/dspch.hpp"

#include "Storage/JFMStorageLayer.hpp"

#include <functional>

#include "App/Application.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Events/JFMEvents.hpp"
#include "Storage/JFMProjectJobs.hpp"

namespace SST::Prototypes::JFM
{

	template <typename EventType>
	SubscriptionHandle subscribeStorageLayer(
		EventBus &bus,
		JFMStorageLayer &layer,
		void (JFMStorageLayer::*method)(const EventType &))
	{
		return bus.Subscribe<EventType>(std::bind_front(method, &layer));
	}
	

	JFMStorageLayer::JFMStorageLayer(Ref<JFMWorkspaceStore> store)
		: Layer("JFMStorage"), m_Store(std::move(store))
	{
	}

	void JFMStorageLayer::OnAttach()
	{
		EventBus &eventBus = Application::Get().GetEventBus();
		AddSubscription(subscribeStorageLayer<ProjectSaveRequested>(eventBus, *this, &JFMStorageLayer::onProjectSaveRequested));
		AddSubscription(subscribeStorageLayer<ProjectSaveCompleted>(eventBus, *this, &JFMStorageLayer::onProjectSaveCompleted));
		AddSubscription(subscribeStorageLayer<ProjectSaveFailed>(eventBus, *this, &JFMStorageLayer::onProjectSaveFailed));
		AddSubscription(subscribeStorageLayer<ProjectLoadRequested>(eventBus, *this, &JFMStorageLayer::onProjectLoadRequested));
		AddSubscription(subscribeStorageLayer<ProjectLoadCompleted>(eventBus, *this, &JFMStorageLayer::onProjectLoadCompleted));
		AddSubscription(subscribeStorageLayer<ProjectLoadFailed>(eventBus, *this, &JFMStorageLayer::onProjectLoadFailed));
		DS_LOG_INFO("JFM storage layer attached");
	}

	void JFMStorageLayer::OnDetach()
	{
		ClearSubscriptions();
	}

	void JFMStorageLayer::onProjectSaveRequested(const ProjectSaveRequested &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Saving project: " + event.path.String());

		auto job = CreateRef<SaveProjectJob>(Application::Get().GetEventBusRef(), event.path, event.snapshot);
		Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::High, "JFMStorageLayer"});
	}

	void JFMStorageLayer::onProjectSaveCompleted(const ProjectSaveCompleted &event)
	{
		if (m_Store == nullptr)
			return;

		m_Store->SetProjectPath(event.path);
		m_Store->SetStatusMessage("Project saved: " + event.path.String());
	}

	void JFMStorageLayer::onProjectSaveFailed(const ProjectSaveFailed &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Project save failed: " + event.error);
		DS_LOG_WARN("JFM project save failed [{}]: {}", event.path.String(), event.error);
	}

	void JFMStorageLayer::onProjectLoadRequested(const ProjectLoadRequested &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Loading project: " + event.path.String());

		auto job = CreateRef<LoadProjectJob>(Application::Get().GetEventBusRef(), event.path);
		Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::High, "JFMStorageLayer"});
	}

	void JFMStorageLayer::onProjectLoadCompleted(const ProjectLoadCompleted &event)
	{
		if (m_Store != nullptr)
			m_Store->ReplaceProject(event.project, event.path);
	}

	void JFMStorageLayer::onProjectLoadFailed(const ProjectLoadFailed &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Project load failed: " + event.error);
		DS_LOG_WARN("JFM project load failed [{}]: {}", event.path.String(), event.error);
	}
} // namespace SST::Prototypes::JFM
