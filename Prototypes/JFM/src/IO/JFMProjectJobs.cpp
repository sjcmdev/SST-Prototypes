#include "Core/dspch.hpp"

#include "IO/JFMProjectJobs.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMPythonImportBridge.hpp"
#include "IO/JFMProjectYamlSerializer.hpp"

#include <algorithm>
#include <unordered_map>

namespace SST::Prototypes::JFM
{

	const JFMCharacteristic *FindMaterializedMatch(
		const std::vector<JFMCharacteristic> &candidates,
		const JFMCharacteristicInfo &info)
	{
		const auto byName = std::find_if(
			candidates.begin(),
			candidates.end(),
			[&info](const JFMCharacteristic &candidate) {
				return candidate.info.name == info.name;
			});
		if (byName != candidates.end())
			return &*byName;

		return candidates.size() == 1 ? &candidates.front() : nullptr;
	}


	SaveProjectJob::SaveProjectJob(
		Ref<EventBus> eventBus,
		Path path,
		JFMProject snapshot,
		ProjectHandle project,
		JFMOperationId operation,
		std::uint64_t revision)
		: m_EventBus(std::move(eventBus)),
		  m_Path(std::move(path)),
		  m_Snapshot(std::move(snapshot)),
		  m_Project(project),
		  m_Operation(operation),
		  m_Revision(revision)
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
				m_EventBus->Queue(ProjectSaveFailed{m_Path, serialized.Error().technicalDetails, m_Project, m_Operation});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetStage("Write");
		context.SetProgress(1.0f, 2.0f);
		std::string error;
		if (!TextFileIO::Save(m_Path, serialized.Value(), error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectSaveFailed{m_Path, error, m_Project, m_Operation});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectSaveCompleted{m_Path, m_Project, m_Operation, m_Revision});
	}

	LoadProjectJob::LoadProjectJob(Ref<EventBus> eventBus, Path path, JFMOperationId operation)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Operation(operation)
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
				m_EventBus->Queue(ProjectLoadFailed{m_Path, error, m_Operation});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetStage("Deserialize");
		context.SetProgress(1.0f, 2.0f);
		auto project = JFMProjectYamlSerializer::Deserialize(text);
		if (!project)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ProjectLoadFailed{m_Path, project.Error().technicalDetails, m_Operation});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectLoadCompleted{m_Path, project.Value(), m_Operation});
	}

	MaterializeProjectDataJob::MaterializeProjectDataJob(
		Ref<EventBus> eventBus,
		JFMProject project,
		Path pythonRoot,
		ProjectHandle projectHandle,
		JFMOperationId operation)
		: m_EventBus(std::move(eventBus)),
		  m_Project(std::move(project)),
		  m_PythonRoot(std::move(pythonRoot)),
		  m_ProjectHandle(projectHandle),
		  m_Operation(operation)
	{
	}

	std::string MaterializeProjectDataJob::GetName() const
	{
		return "Materialize JFM Project Data";
	}

	std::string MaterializeProjectDataJob::GetType() const
	{
		return "jfm.project.materialize_data";
	}

	bool MaterializeProjectDataJob::UsesPythonRuntime() const
	{
		return true;
	}

	void MaterializeProjectDataJob::Execute(JobContext &context)
	{
		const std::size_t total = m_Project.characteristics.size();
		context.SetStage("Materialize raw data");
		context.SetProgress(0.0f, static_cast<float>(total));

		JFMPythonImportBridge bridge(m_PythonRoot);
		std::unordered_map<std::string, JFMImportBatchResult> parsedBySource;
		std::vector<JFMCharacteristic> materialized;
		std::vector<std::string> warnings;
		std::size_t completed = 0;
		parsedBySource.reserve(total);
		materialized.reserve(total);
		warnings.reserve(total);

		const auto advanceProgress = [&context, &completed, total]() {
			++completed;
			context.SetProgress(static_cast<float>(completed), static_cast<float>(total));
		};

		for (const JFMCharacteristicInfo &info : m_Project.characteristics)
		{
			context.ThrowIfCancellationRequested();
			context.SetMessage("Materializing " + info.name);

			if (info.sourcePath.Empty())
			{
				warnings.emplace_back("No source_path for " + info.name + "; raw arrays were not materialized.");
				advanceProgress();
				continue;
			}

			const std::string sourceKey = info.sourcePath.String();
			auto parsed = parsedBySource.find(sourceKey);
			if (parsed == parsedBySource.end())
			{
				JFMImportBatchResult batch;
				if (!FileSystem::Exists(info.sourcePath.Native()))
				{
					warnings.emplace_back("Source file missing for " + info.name + ": " + sourceKey);
					parsedBySource.emplace(sourceKey, std::move(batch));
					advanceProgress();
					continue;
				}

				auto result = bridge.ImportFiles({info.sourcePath}, info.importOptions, {});
				if (!result)
				{
					warnings.emplace_back("Raw data materialization failed for " + sourceKey + ": " + result.Error().technicalDetails);
					parsedBySource.emplace(sourceKey, std::move(batch));
					advanceProgress();
					continue;
				}

				batch = std::move(result).Value();
				warnings.insert(warnings.end(), batch.warnings.begin(), batch.warnings.end());
				parsed = parsedBySource.emplace(sourceKey, std::move(batch)).first;
			}

			const JFMCharacteristic *match = FindMaterializedMatch(parsed->second.characteristics, info);
			if (match == nullptr)
			{
				if (FileSystem::Exists(info.sourcePath.Native()))
					warnings.emplace_back("No matching raw data found for " + info.name + " in " + sourceKey);
				advanceProgress();
				continue;
			}

			JFMCharacteristic characteristic;
			characteristic.info = info;
			characteristic.data = match->data;
			materialized.emplace_back(std::move(characteristic));
			advanceProgress();
		}

		if (m_EventBus != nullptr)
			m_EventBus->Queue(ProjectDataMaterializationCompleted{
				std::move(materialized),
				std::move(warnings),
				m_ProjectHandle,
				m_Operation});
	}
} // namespace SST::Prototypes::JFM
