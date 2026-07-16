#include "Core/dspch.hpp"

#include "IO/JFMExportRawTsvJob.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"

namespace SST::Prototypes::JFM::Detail
{
	const std::vector<double> &selectYValues(const JFMCharacteristicData &data)
	{
		return data.currentDensity.empty() ? data.current : data.currentDensity;
	}

	bool buildTsv(const JFMCharacteristicRecord &characteristic, std::string &text, std::string &error)
	{
		if (!characteristic.data)
		{
			error = "Selected characteristic has no raw arrays.";
			return false;
		}
		const JFMCharacteristicData &data = *characteristic.data;
		const std::vector<double> &y = selectYValues(data);
		const std::size_t count = std::min(data.voltage.size(), y.size());
		if (count == 0)
		{
			error = "Selected characteristic has no raw arrays.";
			return false;
		}

		std::ostringstream output;
		output << "voltage\t" << (data.currentDensity.empty() ? "current" : "current_density") << '\n';
		for (std::size_t index = 0; index < count; ++index)
			output << data.voltage[index] << '\t' << y[index] << '\n';
		text = output.str();
		return true;
	}
} // namespace SST::Prototypes::JFM::Detail

namespace SST::Prototypes::JFM
{

	JFMExportRawTsvJob::JFMExportRawTsvJob(Ref<EventBus> eventBus, Path path, JFMCharacteristicRecord characteristic, ProjectHandle project, JFMOperationId operation)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Characteristic(std::move(characteristic)), m_Project(project), m_Operation(operation)
	{
	}

	std::string JFMExportRawTsvJob::GetName() const
	{
		return "Export JFM Raw TSV";
	}

	std::string JFMExportRawTsvJob::GetType() const
	{
		return "jfm.export.raw_tsv";
	}

	void JFMExportRawTsvJob::Execute(JobContext &context)
	{
		context.SetStage("Build TSV");
		context.SetProgress(0.0f, 2.0f);
		std::string text;
		std::string error;
		if (!Detail::buildTsv(m_Characteristic, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportRawTsvFailed{m_Path, error, m_Project, m_Operation});
			return;
		}

		context.ThrowIfCancellationRequested();
		context.SetStage("Write");
		context.SetProgress(1.0f, 2.0f);
		if (!TextFileIO::Save(m_Path, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportRawTsvFailed{m_Path, error, m_Project, m_Operation});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ExportRawTsvCompleted{m_Path, m_Project, m_Operation});
	}

	JFMExportRawTsvBatchJob::JFMExportRawTsvBatchJob(Ref<EventBus> eventBus, Path outputDirectory, std::vector<JFMExportItem> items, ProjectHandle project, JFMOperationId operation)
		: m_EventBus(std::move(eventBus)), m_OutputDirectory(std::move(outputDirectory)), m_Items(std::move(items)), m_Project(project), m_Operation(operation)
	{
	}

	std::string JFMExportRawTsvBatchJob::GetName() const
	{
		return "Export JFM Raw TSV Batch";
	}

	std::string JFMExportRawTsvBatchJob::GetType() const
	{
		return "jfm.export.raw_tsv_batch";
	}

	void JFMExportRawTsvBatchJob::Execute(JobContext &context)
	{
		context.SetStage("Export TSV batch");
		context.SetProgress(0.0f, static_cast<float>(m_Items.size()));

		std::vector<std::string> warnings;
		std::size_t exported = 0;
		std::error_code directoryError;
		(void)FileSystem::CreateDirectories(m_OutputDirectory.Native(), directoryError);

		for (std::size_t index = 0; index < m_Items.size(); ++index)
		{
			context.ThrowIfCancellationRequested();
			const JFMExportItem &item = m_Items[index];
			const Path outputPath = m_OutputDirectory / item.relativePath;
			(void)FileSystem::CreateDirectories(outputPath.parent_path().Native(), directoryError);

			std::string text;
			std::string error;
			if (!Detail::buildTsv(item.characteristic, text, error))
			{
				warnings.emplace_back(item.characteristic.info.name + ": " + error);
				context.SetProgress(static_cast<float>(index + 1), static_cast<float>(m_Items.size()));
				continue;
			}

			if (!TextFileIO::Save(outputPath, text, error))
			{
				warnings.emplace_back(outputPath.String() + ": " + error);
				context.SetProgress(static_cast<float>(index + 1), static_cast<float>(m_Items.size()));
				continue;
			}

			++exported;
			context.SetProgress(static_cast<float>(index + 1), static_cast<float>(m_Items.size()));
		}

		if (m_EventBus == nullptr)
			return;
		if (exported == 0 && !m_Items.empty())
			m_EventBus->Queue(ExportRawTsvBatchFailed{m_OutputDirectory, warnings.empty() ? "No files exported." : warnings.front(), m_Project, m_Operation});
		else
			m_EventBus->Queue(ExportRawTsvBatchCompleted{m_OutputDirectory, exported, std::move(warnings), m_Project, m_Operation});
	}
} // namespace SST::Prototypes::JFM
