#include "Core/dspch.hpp"

#include "IO/JFMExportRawTsvJob.hpp"

#include "Analysis/JFMArrheniusAnalysis.hpp"
#include "Analysis/JFMFitEngine.hpp"
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

	bool buildCharacteristicsFitTsv(const std::vector<JFMCharacteristicRecord> &characteristics, std::string &text, std::string &error)
	{
		if (characteristics.empty())
		{
			error = "No characteristics selected.";
			return false;
		}

		std::size_t rowCount = 0;
		for (const JFMCharacteristicRecord &characteristic : characteristics)
		{
			if (characteristic.data != nullptr)
				rowCount = std::max(rowCount, characteristic.data->voltage.size());
		}
		if (rowCount == 0)
		{
			error = "Selected characteristics have no raw arrays.";
			return false;
		}

		std::ostringstream output;
		output << "index";
		for (const JFMCharacteristicRecord &characteristic : characteristics)
		{
			const std::string name = characteristic.info.name.empty() ? "characteristic" : characteristic.info.name;
			output << '\t' << name << "_V\t" << name << "_I\t" << name << "_fit";
		}
		output << '\n';

		for (std::size_t row = 0; row < rowCount; ++row)
		{
			output << row;
			for (const JFMCharacteristicRecord &characteristic : characteristics)
			{
				output << '\t';
				if (characteristic.data != nullptr && row < characteristic.data->voltage.size())
					output << characteristic.data->voltage[row];
				output << '\t';
				if (characteristic.data != nullptr)
				{
					const std::vector<double> &y = selectYValues(*characteristic.data);
					if (row < y.size())
						output << y[row];
				}
				output << '\t';
				if (characteristic.data != nullptr && row < characteristic.data->voltage.size() && characteristic.info.temperatureK && characteristic.info.fit && characteristic.info.fit->status == "completed")
				{
					std::unordered_map<std::string, double> parameters;
					for (const auto &[name, estimate] : characteristic.info.fit->parameters)
						parameters[name] = estimate.value;
					const double fitCurrent = EvaluateJFMCurrent(characteristic.info.fit->modelId, characteristic.data->voltage[row], *characteristic.info.temperatureK, parameters);
					if (std::isfinite(fitCurrent))
						output << fitCurrent;
				}
			}
			output << '\n';
		}
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

	JFMExportArrheniusTsvJob::JFMExportArrheniusTsvJob(Ref<EventBus> eventBus, Path path, std::vector<JFMCharacteristicRecord> characteristics, ProjectHandle project, JFMOperationId operation)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Characteristics(std::move(characteristics)), m_Project(project), m_Operation(operation)
	{
	}

	std::string JFMExportArrheniusTsvJob::GetName() const
	{
		return "Export JFM Arrhenius TSV";
	}

	std::string JFMExportArrheniusTsvJob::GetType() const
	{
		return "jfm.export.arrhenius_tsv";
	}

	void JFMExportArrheniusTsvJob::Execute(JobContext &context)
	{
		context.SetStage("Build Arrhenius TSV");
		std::string text;
		std::string error;
		if (!BuildJFMArrheniusTsv(m_Characteristics, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportAnalysisTsvFailed{m_Path, "Arrhenius TSV", error, m_Project, m_Operation});
			return;
		}
		context.ThrowIfCancellationRequested();
		context.SetStage("Write");
		if (!TextFileIO::Save(m_Path, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportAnalysisTsvFailed{m_Path, "Arrhenius TSV", error, m_Project, m_Operation});
			return;
		}
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ExportAnalysisTsvCompleted{m_Path, "Arrhenius TSV", m_Project, m_Operation});
	}

	JFMExportCharacteristicsFitTsvJob::JFMExportCharacteristicsFitTsvJob(Ref<EventBus> eventBus, Path path, std::vector<JFMCharacteristicRecord> characteristics, ProjectHandle project, JFMOperationId operation)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Characteristics(std::move(characteristics)), m_Project(project), m_Operation(operation)
	{
	}

	std::string JFMExportCharacteristicsFitTsvJob::GetName() const
	{
		return "Export JFM Characteristics Fit TSV";
	}

	std::string JFMExportCharacteristicsFitTsvJob::GetType() const
	{
		return "jfm.export.characteristics_fit_tsv";
	}

	void JFMExportCharacteristicsFitTsvJob::Execute(JobContext &context)
	{
		context.SetStage("Build Characteristics + Fit TSV");
		std::string text;
		std::string error;
		if (!Detail::buildCharacteristicsFitTsv(m_Characteristics, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportAnalysisTsvFailed{m_Path, "Characteristics + Fit TSV", error, m_Project, m_Operation});
			return;
		}
		context.ThrowIfCancellationRequested();
		context.SetStage("Write");
		if (!TextFileIO::Save(m_Path, text, error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportAnalysisTsvFailed{m_Path, "Characteristics + Fit TSV", error, m_Project, m_Operation});
			return;
		}
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ExportAnalysisTsvCompleted{m_Path, "Characteristics + Fit TSV", m_Project, m_Operation});
	}
} // namespace SST::Prototypes::JFM
