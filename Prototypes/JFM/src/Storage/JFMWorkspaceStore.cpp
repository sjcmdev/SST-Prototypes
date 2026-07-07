#include "Core/dspch.hpp"

#include "Storage/JFMWorkspaceStore.hpp"

#include <algorithm>

namespace SST::Prototypes::JFM
{
	JFMWorkspaceStore::JFMWorkspaceStore()
		: m_Project(MakeDefaultProject("Untitled JFM Project"))
	{
		ensureSelectionLocked();
	}

	JFMProject JFMWorkspaceStore::GetProjectSnapshot() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_Project;
	}

	std::optional<Path> JFMWorkspaceStore::GetProjectPath() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_ProjectPath;
	}

	std::string JFMWorkspaceStore::GetStatusMessage() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_StatusMessage;
	}

	bool JFMWorkspaceStore::IsImportRunning() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_ImportRunning;
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::GetSelectedCharacteristic() const
	{
		std::scoped_lock lock(m_Mutex);
		for (const JFMSeries &series : m_Project.series)
		{
			for (const JFMCharacteristic &characteristic : series.characteristics)
			{
				if (characteristic.id == m_SelectedCharacteristicId)
					return characteristic;
			}
		}
		return std::nullopt;
	}

	std::string JFMWorkspaceStore::GetSelectedSeriesId() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_SelectedSeriesId;
	}

	std::string JFMWorkspaceStore::GetSelectedCharacteristicId() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_SelectedCharacteristicId;
	}

	std::vector<std::string> JFMWorkspaceStore::GetLastImportWarnings() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_LastImportWarnings;
	}

	void JFMWorkspaceStore::ResetProject(std::string name)
	{
		std::scoped_lock lock(m_Mutex);
		m_Project = MakeDefaultProject(std::move(name));
		m_ProjectPath.reset();
		m_LastImportWarnings.clear();
		m_ImportRunning = false;
		m_StatusMessage = "New JFM project created.";
		ensureSelectionLocked();
	}

	void JFMWorkspaceStore::ReplaceProject(JFMProject project, std::optional<Path> path)
	{
		std::scoped_lock lock(m_Mutex);
		m_Project = std::move(project);
		m_ProjectPath = std::move(path);
		m_StatusMessage = "Project loaded.";
		ensureSelectionLocked();
	}

	void JFMWorkspaceStore::SetProjectPath(Path path)
	{
		std::scoped_lock lock(m_Mutex);
		m_ProjectPath = std::move(path);
	}

	void JFMWorkspaceStore::SetStatusMessage(std::string message)
	{
		std::scoped_lock lock(m_Mutex);
		m_StatusMessage = std::move(message);
	}

	void JFMWorkspaceStore::SetImportRunning(bool running)
	{
		std::scoped_lock lock(m_Mutex);
		m_ImportRunning = running;
	}

	void JFMWorkspaceStore::SetLastImportWarnings(std::vector<std::string> warnings)
	{
		std::scoped_lock lock(m_Mutex);
		m_LastImportWarnings = std::move(warnings);
	}

	void JFMWorkspaceStore::SelectSeries(std::string seriesId)
	{
		std::scoped_lock lock(m_Mutex);
		m_SelectedSeriesId = std::move(seriesId);
		ensureSelectionLocked();
	}

	void JFMWorkspaceStore::SelectCharacteristic(std::string characteristicId)
	{
		std::scoped_lock lock(m_Mutex);
		m_SelectedCharacteristicId = std::move(characteristicId);
		ensureSelectionLocked();
	}

	void JFMWorkspaceStore::AddImportedCharacteristics(std::vector<JFMCharacteristic> characteristics)
	{
		std::scoped_lock lock(m_Mutex);
		ensureSelectionLocked();
		if (m_Project.series.empty())
			m_Project.series.push_back(JFMSeries{nextId("series"), "Imported", {}});
		if (m_SelectedSeriesId.empty())
			m_SelectedSeriesId = m_Project.series.front().id;

		auto target = std::find_if(
			m_Project.series.begin(),
			m_Project.series.end(),
			[this](const JFMSeries &series) { return series.id == m_SelectedSeriesId; });
		if (target == m_Project.series.end())
			target = m_Project.series.begin();

		for (JFMCharacteristic &characteristic : characteristics)
		{
			if (characteristic.id.empty())
				characteristic.id = nextId("characteristic");
			if (characteristic.name.empty())
				characteristic.name = characteristic.sourcePath.empty() ? characteristic.id : Path(characteristic.sourcePath).filename().String();
			m_SelectedCharacteristicId = characteristic.id;
			target->characteristics.push_back(std::move(characteristic));
		}

		m_StatusMessage = "Imported " + std::to_string(characteristics.size()) + " characteristic(s).";
		m_ImportRunning = false;
		ensureSelectionLocked();
	}

	JFMProject JFMWorkspaceStore::MakeDefaultProject(std::string name)
	{
		JFMProject project;
		project.id = "jfm-project";
		project.name = std::move(name);
		project.metadata["prototype"] = "JFM";
		project.series.push_back(JFMSeries{"series-1", "Series 1", {}});
		return project;
	}

	std::string JFMWorkspaceStore::nextId(const char *prefix) const
	{
		return std::string(prefix) + "-" + std::to_string(m_NextId++);
	}

	void JFMWorkspaceStore::ensureSelectionLocked()
	{
		if (m_Project.series.empty())
			m_Project.series.push_back(JFMSeries{nextId("series"), "Series 1", {}});

		const auto seriesIt = std::find_if(
			m_Project.series.begin(),
			m_Project.series.end(),
			[this](const JFMSeries &series) { return series.id == m_SelectedSeriesId; });
		if (seriesIt == m_Project.series.end())
			m_SelectedSeriesId = m_Project.series.front().id;

		bool characteristicFound = false;
		for (const JFMSeries &series : m_Project.series)
		{
			for (const JFMCharacteristic &characteristic : series.characteristics)
			{
				if (characteristic.id == m_SelectedCharacteristicId)
				{
					characteristicFound = true;
					break;
				}
			}
			if (characteristicFound)
				break;
		}

		if (characteristicFound)
			return;

		m_SelectedCharacteristicId.clear();
		for (const JFMSeries &series : m_Project.series)
		{
			if (!series.characteristics.empty())
			{
				m_SelectedCharacteristicId = series.characteristics.front().id;
				return;
			}
		}
	}
} // namespace SST::Prototypes::JFM
