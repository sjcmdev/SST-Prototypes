#include "Core/dspch.hpp"

#include "IO/JFMImportJob.hpp"

#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMPythonImportBridge.hpp"

namespace SST::Prototypes::JFM
{
	JFMImportJob::JFMImportJob(Ref<EventBus> eventBus, std::vector<Path> paths, Path pythonRoot)
		: m_EventBus(std::move(eventBus)), m_Paths(std::move(paths)), m_PythonRoot(std::move(pythonRoot))
	{
	}

	std::string JFMImportJob::GetName() const
	{
		return "Import JFM Data";
	}

	std::string JFMImportJob::GetType() const
	{
		return "jfm.import";
	}

	void JFMImportJob::reportProgress(JobContext &context, std::size_t completed, std::size_t total, const std::string &message) const
	{
		context.SetProgress(static_cast<float>(completed), static_cast<float>(total));
		context.SetMessage(message);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportProgress{static_cast<float>(completed), static_cast<float>(total), message});
	}

	void JFMImportJob::Execute(JobContext &context)
	{
		context.SetStage("Python import");
		context.SetProgress(0.0f, static_cast<float>(m_Paths.size()));
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportProgress{0.0f, static_cast<float>(m_Paths.size()), "Import starting"});
		JFMPythonImportBridge bridge(m_PythonRoot);
		auto result = bridge.ImportFiles(
			m_Paths,
			[&context, this](std::size_t completed, std::size_t total, const std::string &message) {
				reportProgress(context, completed, total, message);
			});

		if (!result)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ImportFailed{result.Error().technicalDetails});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetProgress(static_cast<float>(m_Paths.size()), static_cast<float>(m_Paths.size()));
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportCompleted{result->characteristics, result->warnings});
	}
} // namespace SST::Prototypes::JFM
