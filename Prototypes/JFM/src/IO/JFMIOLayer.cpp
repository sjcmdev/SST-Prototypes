#include "Core/dspch.hpp"

#include "IO/JFMIOLayer.hpp"

#include <functional>

#include "App/Application.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobEvents.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMExportRawTsvJob.hpp"
#include "IO/JFMImportJob.hpp"

namespace SST::Prototypes::JFM
{

	template <typename EventType>
	SubscriptionHandle subscribeIOLayer(
		EventBus &bus,
		JFMIOLayer &layer,
		void (JFMIOLayer::*method)(const EventType &))
	{
		return bus.Subscribe<EventType>(std::bind_front(method, &layer));
	}
	

	JFMIOLayer::JFMIOLayer(Ref<JFMWorkspaceStore> store)
		: Layer("JFMIO"), m_Store(std::move(store))
	{
	}

	void JFMIOLayer::OnAttach()
	{
		EventBus &eventBus = Application::Get().GetEventBus();
		AddSubscription(subscribeIOLayer<ImportRequested>(eventBus, *this, &JFMIOLayer::onImportRequested));
		AddSubscription(subscribeIOLayer<ImportProgress>(eventBus, *this, &JFMIOLayer::onImportProgress));
		AddSubscription(subscribeIOLayer<ImportCompleted>(eventBus, *this, &JFMIOLayer::onImportCompleted));
		AddSubscription(subscribeIOLayer<ImportFailed>(eventBus, *this, &JFMIOLayer::onImportFailed));
		AddSubscription(subscribeIOLayer<ExportRawTsvRequested>(eventBus, *this, &JFMIOLayer::onExportRawTsvRequested));
		AddSubscription(subscribeIOLayer<ExportRawTsvCompleted>(eventBus, *this, &JFMIOLayer::onExportRawTsvCompleted));
		AddSubscription(subscribeIOLayer<ExportRawTsvFailed>(eventBus, *this, &JFMIOLayer::onExportRawTsvFailed));
		DS_LOG_INFO("JFM IO layer attached");
	}

	void JFMIOLayer::OnDetach()
	{
		ClearSubscriptions();
	}

	void JFMIOLayer::onImportRequested(const ImportRequested &event)
	{
		if (event.paths.empty())
			return;

		if (m_ImportRunning)
		{
			Application::Get().GetEventBus().Queue(ImportFailed{"Another JFM import is already running."});
			return;
		}

		m_ImportRunning = true;
		if (m_Store != nullptr)
		{
			m_Store->SetImportRunning(true);
			m_Store->SetStatusMessage("Import queued.");
		}
		m_TargetSeriesId = event.targetSeriesId;

		const Path pythonRoot = Path::FromResolved(FileSystem::CurrentPath() / "python");
		auto job = CreateRef<JFMImportJob>(Application::Get().GetEventBusRef(), event.paths, pythonRoot);
		Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::Normal, "JFMIOLayer"});
	}

	void JFMIOLayer::onImportProgress(const ImportProgress &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Import: " + event.message);
	}

	void JFMIOLayer::onImportCompleted(const ImportCompleted &event)
	{
		m_ImportRunning = false;
		if (m_Store != nullptr)
		{
			if (!m_TargetSeriesId.empty())
				m_Store->SelectSeries(m_TargetSeriesId);
			m_Store->SetLastImportWarnings(event.warnings);
			m_Store->AddImportedCharacteristics(event.characteristics);
		}
		m_TargetSeriesId.clear();
	}

	void JFMIOLayer::onImportFailed(const ImportFailed &event)
	{
		m_ImportRunning = false;
		if (m_Store != nullptr)
		{
			m_Store->SetImportRunning(false);
			m_Store->SetStatusMessage("Import failed: " + event.error);
		}
		m_TargetSeriesId.clear();
		DS_LOG_WARN("JFM import failed: {}", event.error);
	}

	void JFMIOLayer::onExportRawTsvRequested(const ExportRawTsvRequested &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Raw TSV export queued: " + event.path.String());

		auto job = CreateRef<JFMExportRawTsvJob>(Application::Get().GetEventBusRef(), event.path, event.characteristic);
		Application::Get().GetEventBus().Queue(JobSubmitRequested{job, JobPriority::Normal, "JFMIOLayer"});
	}

	void JFMIOLayer::onExportRawTsvCompleted(const ExportRawTsvCompleted &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Raw TSV exported: " + event.path.String());
	}

	void JFMIOLayer::onExportRawTsvFailed(const ExportRawTsvFailed &event)
	{
		if (m_Store != nullptr)
			m_Store->SetStatusMessage("Raw TSV export failed: " + event.error);
		DS_LOG_WARN("JFM raw TSV export failed [{}]: {}", event.path.String(), event.error);
	}
} // namespace SST::Prototypes::JFM
