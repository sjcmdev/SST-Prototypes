#include "Core/dspch.hpp"

#include "IO/JFMImportPreviewJob.hpp"

#include "Core/JobSystem/JobContext.hpp"
#include "Core/IO/ImportPreview.hpp"
#include "Events/JFMEvents.hpp"
#include "IO/JFMImportSources.hpp"
#include "IO/JFMPythonImportBridge.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <sstream>

namespace SST::Prototypes::JFM::Detail
{
	constexpr std::size_t PreviewLineLimit = 12;
	constexpr std::size_t PreviewPointLimit = 512;

	std::string ReadPreviewText(const Path &path)
	{
		std::ifstream input(path.Native());
		if (!input)
			return "Unable to open file for preview.";

		std::ostringstream output;
		std::string line;
		for (std::size_t index = 0; index < PreviewLineLimit && std::getline(input, line); ++index)
			output << line << '\n';
		return output.str();
	}

	std::vector<std::size_t> PreviewIndices(std::size_t count)
	{
		if (count <= PreviewPointLimit)
		{
			std::vector<std::size_t> indices(count);
			std::iota(indices.begin(), indices.end(), 0);
			return indices;
		}

		std::vector<std::size_t> indices;
		indices.reserve(PreviewPointLimit);
		for (std::size_t index = 0; index < PreviewPointLimit; ++index)
			indices.emplace_back(index * (count - 1) / (PreviewPointLimit - 1));
		return indices;
	}

	std::vector<double> SelectPreviewPoints(const std::vector<double> &values, const std::vector<std::size_t> &indices)
	{
		std::vector<double> result;
		result.reserve(indices.size());
		for (std::size_t index : indices)
		{
			if (index < values.size())
				result.emplace_back(values[index]);
		}
		return result;
	}

	JFMCharacteristicRecord MakePreviewRecord(JFMCharacteristic characteristic)
	{
		const std::vector<std::size_t> indices = PreviewIndices(characteristic.data.voltage.size());
		JFMCharacteristicData data;
		data.voltage = SelectPreviewPoints(characteristic.data.voltage, indices);
		data.current = SelectPreviewPoints(characteristic.data.current, indices);
		data.currentDensity = SelectPreviewPoints(characteristic.data.currentDensity, indices);
		return {std::move(characteristic.info), CreateRef<const JFMCharacteristicData>(std::move(data))};
	}

	void SetRelativeLabels(JFMImportPreviewItem &item, const ImportSource &source)
	{
		item.relativePath = ImportSourceLabel(source);
		item.folderKey = item.path.parent_path().Utf8();
		const auto parent = source.root.Empty() ? std::filesystem::path{} :
			item.path.Native().lexically_relative(source.root.Native()).parent_path();
		item.folderLabel = parent.empty() ? item.path.parent_path().filename().Utf8() : Path::FromResolved(parent).Utf8();
	}
} // namespace SST::Prototypes::JFM::Detail

namespace SST::Prototypes::JFM
{
	JFMImportPreviewJob::JFMImportPreviewJob(
		Ref<EventBus> eventBus,
		std::uint64_t generation,
		std::vector<Path> paths,
		bool recursive,
		Path pythonRoot,
		JFMImportOptions options)
		: m_EventBus(std::move(eventBus)),
		  m_Generation(generation),
		  m_Paths(std::move(paths)),
		  m_Recursive(recursive),
		  m_PythonRoot(std::move(pythonRoot)),
		  m_Options(std::move(options))
	{
	}

	std::string JFMImportPreviewJob::GetName() const
	{
		return "Preview JFM Import";
	}

	std::string JFMImportPreviewJob::GetType() const
	{
		return "jfm.import.preview";
	}

	bool JFMImportPreviewJob::UsesPythonRuntime() const
	{
		return false; // the Python bridge acquires the GIL only around its calls
	}

	void JFMImportPreviewJob::Execute(JobContext &context) try
	{
		const auto sources = CollectSources(m_Paths, m_Recursive, IsJFMDataFile);
		JFMPythonImportBridge bridge(m_PythonRoot);
		auto previews = PreviewSources<JFMImportPreviewItem>(context, sources, [&](const ImportSource &source) -> Result<JFMImportPreviewItem> {
			JFMImportPreviewItem item;
			item.path = source.path;
			Detail::SetRelativeLabels(item, source);
			item.text = Detail::ReadPreviewText(item.path);
			auto imported = bridge.ImportFiles({item.path}, m_Options, {});
			if (!imported) return imported.Error();
			for (JFMCharacteristic &characteristic : imported->characteristics)
				item.characteristics.emplace_back(Detail::MakePreviewRecord(std::move(characteristic)));
			if (!imported->warnings.empty()) item.warning = imported->warnings.front();
			return item;
		});
		auto payload = CreateRef<JFMImportPreviewPayload>();
		for (auto &preview : previews) {
			if (preview.value) payload->items.push_back(std::move(*preview.value));
			else {
				JFMImportPreviewItem item;
				item.path = preview.source.path;
				Detail::SetRelativeLabels(item, preview.source);
				item.text = Detail::ReadPreviewText(item.path);
				item.warning = preview.error->technicalDetails;
				payload->items.push_back(std::move(item));
			}
		}
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportPreviewCompleted{m_Generation, std::move(payload)});
	}
	catch (const JobCancelledException &)
	{
		throw;
	}
	catch (const std::exception &error)
	{
		if (m_EventBus != nullptr)
			m_EventBus->Queue(ImportPreviewFailed{m_Generation, error.what()});
	}
} // namespace SST::Prototypes::JFM
