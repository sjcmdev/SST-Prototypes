#include "Core/dspch.hpp"

#include "IO/JFMPrefitJob.hpp"

#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMPythonPrefitBridge.hpp"

namespace SST::Prototypes::JFM
{
	bool JFMPrefitWorkQueue::Enqueue(std::vector<JFMPrefitWorkItem> items)
	{
		std::scoped_lock lock(m_Mutex);
		for (JFMPrefitWorkItem &item : items)
		{
			const auto existing = std::find_if(m_Pending.begin(), m_Pending.end(), [&item](const JFMPrefitWorkItem &pending) {
				return pending.ref == item.ref;
			});
			if (existing == m_Pending.end())
				m_Pending.push_back(std::move(item));
			else
				*existing = std::move(item);
		}
		if (m_JobScheduled || m_Pending.empty())
			return false;
		m_JobScheduled = true;
		return true;
	}

	std::optional<std::vector<JFMPrefitWorkItem>> JFMPrefitWorkQueue::TakeNext()
	{
		std::scoped_lock lock(m_Mutex);
		if (m_Pending.empty())
		{
			m_JobScheduled = false;
			return std::nullopt;
		}
		std::vector<JFMPrefitWorkItem> items = std::move(m_Pending);
		m_Pending.clear();
		return items;
	}

	void JFMPrefitWorkQueue::Reset()
	{
		std::scoped_lock lock(m_Mutex);
		m_Pending.clear();
		m_JobScheduled = false;
	}

	JFMPrefitJob::JFMPrefitJob(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Path pythonRoot, Ref<JFMPrefitWorkQueue> queue)
		: m_EventBus(std::move(eventBus)), m_Store(std::move(store)), m_PythonRoot(std::move(pythonRoot)), m_Queue(std::move(queue))
	{
	}

	std::string JFMPrefitJob::GetName() const
	{
		return "Prefit JFM Characteristics";
	}

	std::string JFMPrefitJob::GetType() const
	{
		return "jfm.prefit";
	}

	bool JFMPrefitJob::UsesPythonRuntime() const
	{
		return true;
	}

	void JFMPrefitJob::Execute(JobContext &context)
	{
		context.SetStage("JFM data range and prefit");
		JFMPythonPrefitBridge bridge(m_PythonRoot);
		try
		{
			while (m_Queue != nullptr)
			{
				auto items = m_Queue->TakeNext();
				if (!items)
					break;
				for (std::size_t index = 0; index < items->size(); ++index)
				{
					context.ThrowIfCancellationRequested();
					const JFMPrefitWorkItem &item = (*items)[index];
					context.SetProgress(static_cast<float>(index), static_cast<float>(items->size()));
					context.SetMessage(item.characteristic.info.name);
					if (m_Store == nullptr || !m_Store->IsPrefitGenerationCurrent(item.ref, item.generation))
						continue;

					auto result = bridge.Run(item.characteristic);
					if (m_EventBus == nullptr)
						continue;
					if (result)
						m_EventBus->Queue(PrefitCompleted{item.ref, item.generation, std::move(result->state), std::move(result->curve)});
					else
						m_EventBus->Queue(PrefitFailed{item.ref, item.generation, result.Error().technicalDetails});
				}
				context.SetProgress(static_cast<float>(items->size()), static_cast<float>(items->size()));
			}
		}
		catch (...)
		{
			if (m_Queue != nullptr)
				m_Queue->Reset();
			throw;
		}
	}
}
