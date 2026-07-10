#include "Core/dspch.hpp"

#include "IO/JFMExportRawTsvJob.hpp"

#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Events/JFMEvents.hpp"

namespace SST::Prototypes::JFM
{
	namespace
	{

	const std::vector<double> &selectYValues(const JFMCharacteristic &characteristic)
	{
		if (!characteristic.data.currentDensity.empty())
			return characteristic.data.currentDensity;
		return characteristic.data.current;
	}
	}

	JFMExportRawTsvJob::JFMExportRawTsvJob(Ref<EventBus> eventBus, Path path, JFMCharacteristic characteristic)
		: m_EventBus(std::move(eventBus)), m_Path(std::move(path)), m_Characteristic(std::move(characteristic))
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
		const std::vector<double> &y = selectYValues(m_Characteristic);
		const std::size_t count = std::min(m_Characteristic.data.voltage.size(), y.size());
		if (count == 0)
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportRawTsvFailed{m_Path, "Selected characteristic has no raw arrays."});
			return;
		}

		std::ostringstream output;
		output << "voltage\t" << (m_Characteristic.data.currentDensity.empty() ? "current" : "current_density") << '\n';
		for (std::size_t index = 0; index < count; ++index)
			output << m_Characteristic.data.voltage[index] << '\t' << y[index] << '\n';

		context.ThrowIfCancellationRequested();
		context.SetStage("Write");
		context.SetProgress(1.0f, 2.0f);
		std::string error;
		if (!TextFileIO::Save(m_Path, output.str(), error))
		{
			if (m_EventBus != nullptr)
				m_EventBus->Queue(ExportRawTsvFailed{m_Path, error});
			return;
		}

		context.SetProgress(2.0f, 2.0f);
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ExportRawTsvCompleted{m_Path});
	}
} // namespace SST::Prototypes::JFM
