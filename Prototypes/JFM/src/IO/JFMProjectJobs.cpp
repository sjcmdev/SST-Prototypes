#include "Core/dspch.hpp"

#include "IO/JFMProjectJobs.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMProjectYamlSerializer.hpp"

namespace SST::Prototypes::JFM
{
	SaveProjectJob::SaveProjectJob(Ref<EventBus> eventBus, Path path, JFMProject snapshot)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Snapshot(std::move(snapshot))
	{
	}

	std::string SaveProjectJob::GetName() const
	{
		return "Save JFM Project";
	}

	std::string SaveProjectJob::GetType() const
	{
		return "jfm.project.save";
	}

	void SaveProjectJob::Execute(JobContext &context)
	{
		context.SetStage("Serialize");
		context.SetProgress(0.0f, 2.0f);
		auto serialized = JFMProjectYamlSerializer::Serialize(m_Snapshot);
		if (!serialized)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectSaveFailed{m_Path, serialized.Error().technicalDetails});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetStage("Write");
		context.SetProgress(1.0f, 2.0f);
		std::string error;
		if (!TextFileIO::Save(m_Path, serialized.Value(), error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectSaveFailed{m_Path, error});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectSaveCompleted{m_Path});
	}

	LoadProjectJob::LoadProjectJob(Ref<EventBus> eventBus, Path path)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path))
	{
	}

	std::string LoadProjectJob::GetName() const
	{
		return "Load JFM Project";
	}

	std::string LoadProjectJob::GetType() const
	{
		return "jfm.project.load";
	}

	void LoadProjectJob::Execute(JobContext &context)
	{
		context.SetStage("Read");
		context.SetProgress(0.0f, 2.0f);
		std::string text;
		std::string error;
		if (!TextFileIO::Load(m_Path, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectLoadFailed{m_Path, error});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetStage("Deserialize");
		context.SetProgress(1.0f, 2.0f);
		auto project = JFMProjectYamlSerializer::Deserialize(text);
		if (!project)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectLoadFailed{m_Path, project.Error().technicalDetails});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectLoadCompleted{m_Path, project.Value()});
	}
} // namespace SST::Prototypes::JFM
