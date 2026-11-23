#include "Core/dspch.hpp"

#include "IO/JFMImportJob.hpp"

#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMPythonImportBridge.hpp"

namespace SST::Prototypes::JFM
{

	std::size_t CountPaths(const std::vector<JFMImportGroup> &groups)
	{
		std::size_t count = 0;
		for (const JFMImportGroup &group : groups)
			count += group.paths.size();
		return count;
	}


	JFMImportJob::JFMImportJob(Ref<EventBus> eventBus, std::vector<Path> paths, Path pythonRoot, JFMImportOptions options, ProjectHandle project, JFMOperationId operation)
		: JFMImportJob(
			std::move(eventBus),
			std::move(paths),
			false,
			JFMImportPlacement::NewSeries,
			{},
			std::move(pythonRoot),
			std::move(options),
			project,
			operation)
	{
	}

	JFMImportJob::JFMImportJob(
		Ref<EventBus> eventBus,
		std::vector<Path> paths,
		bool recursive,
		JFMImportPlacement placement,
		std::string targetSeriesId,
		Path pythonRoot,
		JFMImportOptions options,
		ProjectHandle project,
		JFMOperationId operation)
		: m_EventBus(std::move(eventBus)),
		  m_Paths(std::move(paths)),
		  m_Recursive(recursive),
		  m_Placement(placement),
		  m_TargetSeriesId(std::move(targetSeriesId)),
		  m_PythonRoot(std::move(pythonRoot)),
		  m_Options(std::move(options)),
		  m_Project(project),
		  m_Operation(operation)
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

	bool JFMImportJob::UsesPythonRuntime() const
	{
		return false; // the Python bridge acquires the GIL only around its calls
	}

	void JFMImportJob::reportProgress(JobContext &context, std::size_t completed, std::size_t total, const std::string &message) const
	{
		context.SetProgress(static_cast<float>(completed), static_cast<float>(total));
		context.SetMessage(message);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportProgress{static_cast<float>(completed), static_cast<float>(total), message, m_Project, m_Operation});
	}

	void JFMImportJob::Execute(JobContext &context)
	{
		std::vector<JFMImportGroup> groups = BuildImportGroups(m_Paths, m_Recursive, m_Placement, m_TargetSeriesId);
		const std::size_t total = CountPaths(groups);
		context.SetStage("Python import");
		context.SetProgress(0.0f, static_cast<float>(total));
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportProgress{0.0f, static_cast<float>(total), "Import starting", m_Project, m_Operation});
		if (groups.empty())
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ImportFailed{"No supported JFM data files were found.", m_Project, m_Operation});
			return;
		}

		JFMPythonImportBridge bridge(m_PythonRoot);
		std::size_t completedOffset = 0;
		std::size_t importedCount = 0;
		std::vector<std::string> allWarnings;
		for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
		{
			const JFMImportGroup &group = groups[groupIndex];
			if (group.paths.empty())
				continue;

			std::vector<JFMCharacteristicRecord> records;
			std::vector<std::string> groupWarnings;
			for (const Path &path : group.paths)
			{
				auto result = bridge.ImportFiles(
					{path},
					m_Options,
					[&context, this, completedOffset, total](std::size_t completed, std::size_t, const std::string &message) {
						reportProgress(context, completedOffset + completed, total, message);
					});
				++completedOffset;
				context.ThrowIfCancellationRequested();

				if (!result)
				{
					groupWarnings.push_back(result.Error().technicalDetails);
					allWarnings.push_back(result.Error().technicalDetails);
					reportProgress(context, completedOffset, total, "Skipped " + path.filename().Utf8());
					continue;
				}

				JFMImportBatchResult batch = std::move(result).Value();
				for (std::string &warning : batch.warnings)
					groupWarnings.emplace_back(std::move(warning));
				records.reserve(records.size() + batch.characteristics.size());
				for (JFMCharacteristic &characteristic : batch.characteristics)
				{
					records.emplace_back(JFMCharacteristicRecord{
						std::move(characteristic.info),
						CreateRef<const JFMCharacteristicData>(std::move(characteristic.data))});
				}
			}

			importedCount += records.size();
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ImportCompleted{
					std::move(records),
					std::move(groupWarnings),
					group.targetSeriesId,
					group.groupPath,
					group.seriesName,
					groupIndex + 1 == groups.size(),
					m_Project,
					m_Operation});
		}

		context.SetProgress(static_cast<float>(total), static_cast<float>(total));
		if (m_EventBus != nullptr && importedCount == 0 && !allWarnings.empty())
			m_EventBus->Queue(ImportFailed{allWarnings.front(), m_Project, m_Operation});
		else if (m_EventBus != nullptr && completedOffset == 0)
			m_EventBus->Queue(ImportCompleted{{}, {}, {}, {}, {}, true, m_Project, m_Operation});
	}
} // namespace SST::Prototypes::JFM
