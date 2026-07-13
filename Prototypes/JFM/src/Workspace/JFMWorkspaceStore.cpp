#include "Core/dspch.hpp"

#include "Workspace/JFMWorkspaceStore.hpp"

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
		auto info = findInfoLocked(m_SelectedCharacteristicId);
		return info ? makeCharacteristicLocked(info->get()) : std::nullopt;
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetSelectedCharacteristics() const
	{
		std::scoped_lock lock(m_Mutex);
		std::vector<JFMCharacteristic> characteristics;
		characteristics.reserve(m_SelectedCharacteristicIds.size());
		for (const CharacteristicId id : m_SelectedCharacteristicIds)
		{
			auto info = findInfoLocked(id);
			if (info)
				characteristics.push_back(*makeCharacteristicLocked(info->get()));
		}
		return characteristics;
	}

	std::string JFMWorkspaceStore::GetSelectedSeriesId() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_SelectedSeriesId;
	}

	CharacteristicId JFMWorkspaceStore::GetSelectedCharacteristicId() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_SelectedCharacteristicId;
	}

	std::vector<CharacteristicId> JFMWorkspaceStore::GetSelectedCharacteristicIds() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_SelectedCharacteristicIds;
	}

	std::vector<std::string> JFMWorkspaceStore::GetLastImportWarnings() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_LastImportWarnings;
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::FindCharacteristic(const CharacteristicId &id) const
	{
		std::scoped_lock lock(m_Mutex);
		auto info = findInfoLocked(id);
		return info ? makeCharacteristicLocked(info->get()) : std::nullopt;
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::FindCharacteristicByName(const std::string &name) const
	{
		std::scoped_lock lock(m_Mutex);
		const auto it = std::find_if(
			m_Project.characteristics.begin(),
			m_Project.characteristics.end(),
			[&name](const JFMCharacteristicInfo &info) { return info.name == name; });
		return it != m_Project.characteristics.end() ? makeCharacteristicLocked(*it) : std::nullopt;
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetSeriesCharacteristics(const std::string &seriesId) const
	{
		std::scoped_lock lock(m_Mutex);
		auto series = findSeriesLocked(seriesId);
		return series ? seriesCharacteristicsLocked(series->get()) : std::vector<JFMCharacteristic>{};
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetGroupCharacteristics(const std::string &groupId) const
	{
		std::scoped_lock lock(m_Mutex);
		std::vector<std::string> groupIds;
		collectGroupIdsLocked(groupId, groupIds);

		std::vector<JFMCharacteristic> characteristics;
		for (const JFMSeries &series : m_Project.series)
		{
			if (std::find(groupIds.begin(), groupIds.end(), series.groupId) == groupIds.end())
				continue;
			std::vector<JFMCharacteristic> seriesCharacteristics = seriesCharacteristicsLocked(series);
			characteristics.insert(characteristics.end(), seriesCharacteristics.begin(), seriesCharacteristics.end());
		}
		return characteristics;
	}

	JFMGroup JFMWorkspaceStore::AddGroup(std::string name, std::string parentId)
	{
		std::scoped_lock lock(m_Mutex);
		if (name.empty())
			name = "Group " + std::to_string(m_Project.groups.size() + 1);
		if (!parentId.empty() && !findGroupLocked(parentId))
			parentId.clear();
		JFMGroup group{nextId("group"), std::move(name), std::move(parentId)};
		m_Project.groups.push_back(group);
		return group;
	}

	std::string JFMWorkspaceStore::FindOrCreateGroupPath(const std::vector<std::string> &names)
	{
		std::scoped_lock lock(m_Mutex);
		std::string parentId;
		for (const std::string &name : names)
		{
			if (name.empty())
				continue;
			auto existing = findGroupByNameAndParentLocked(name, parentId);
			if (existing)
			{
				parentId = existing->get().id;
				continue;
			}

			JFMGroup group{nextId("group"), name, parentId};
			parentId = group.id;
			m_Project.groups.push_back(std::move(group));
		}
		return parentId;
	}

	JFMSeries JFMWorkspaceStore::AddSeries(std::string name)
	{
		return AddSeries(std::move(name), {});
	}

	JFMSeries JFMWorkspaceStore::AddSeries(std::string name, std::string groupId)
	{
		std::scoped_lock lock(m_Mutex);
		if (!groupId.empty() && !findGroupLocked(groupId))
			groupId.clear();
		JFMSeries series{nextId("series"), std::move(name), std::move(groupId), {}};
		if (series.name.empty())
			series.name = "Series " + std::to_string(m_Project.series.size() + 1);
		m_Project.series.push_back(series);
		m_SelectedSeriesId = series.id;
		ensureSelectionLocked();
		return series;
	}

	std::vector<CharacteristicId> JFMWorkspaceStore::AddCharacteristics(std::vector<JFMCharacteristic> characteristics, std::string targetSeriesId)
	{
		std::scoped_lock lock(m_Mutex);
		ensureSelectionLocked();
		if (m_Project.series.empty())
			m_Project.series.push_back(JFMSeries{nextId("series"), "Imported", {}, {}});
		if (targetSeriesId.empty())
			targetSeriesId = m_SelectedSeriesId.empty() ? m_Project.series.front().id : m_SelectedSeriesId;

		auto targetRef = findSeriesLocked(targetSeriesId);
		if (!targetRef)
			targetRef = std::ref(m_Project.series.front());
		JFMSeries &target = targetRef->get();

		std::vector<CharacteristicId> addedIds;
		addedIds.reserve(characteristics.size());
		for (JFMCharacteristic &characteristic : characteristics)
		{
			if (characteristic.info.id == InvalidCharacteristicId)
				characteristic.info.id = m_NextCharacteristicId++;
			else if (characteristic.info.id >= m_NextCharacteristicId)
				m_NextCharacteristicId = characteristic.info.id + 1;
			if (characteristic.info.name.empty())
			{
				characteristic.info.name = characteristic.info.sourcePath.Empty()
					? std::to_string(characteristic.info.id)
					: characteristic.info.sourcePath.filename().String();
			}

			removeCharacteristicReferencesLocked(characteristic.info.id);
			if (auto existing = findInfoLocked(characteristic.info.id))
				existing->get() = characteristic.info;
			else
				m_Project.characteristics.push_back(characteristic.info);

			m_CharacteristicData[characteristic.info.id] = std::move(characteristic.data);
			target.characteristicIds.push_back(characteristic.info.id);
			m_SelectedCharacteristicId = characteristic.info.id;
			addedIds.push_back(characteristic.info.id);
		}

		m_SelectedSeriesId = target.id;
		m_SelectedCharacteristicIds = addedIds;
		m_StatusMessage = "Imported " + std::to_string(characteristics.size()) + " characteristic(s).";
		ensureSelectionLocked();
		return addedIds;
	}

	bool JFMWorkspaceStore::DeleteCharacteristic(const CharacteristicId &id)
	{
		std::scoped_lock lock(m_Mutex);
		const auto oldSize = m_Project.characteristics.size();
		m_Project.characteristics.erase(
			std::remove_if(
				m_Project.characteristics.begin(),
				m_Project.characteristics.end(),
				[&id](const JFMCharacteristicInfo &info) { return info.id == id; }),
			m_Project.characteristics.end());
		m_CharacteristicData.erase(id);
		removeCharacteristicReferencesLocked(id);
		m_SelectedCharacteristicIds.erase(
			std::remove(m_SelectedCharacteristicIds.begin(), m_SelectedCharacteristicIds.end(), id),
			m_SelectedCharacteristicIds.end());
		if (m_SelectedCharacteristicId == id)
			m_SelectedCharacteristicId = InvalidCharacteristicId;
		ensureSelectionLocked();
		return m_Project.characteristics.size() != oldSize;
	}

	bool JFMWorkspaceStore::DeleteSeries(const std::string &seriesId)
	{
		std::scoped_lock lock(m_Mutex);
		auto series = findSeriesLocked(seriesId);
		if (!series)
			return false;

		std::vector<CharacteristicId> ids = series->get().characteristicIds;
		for (const CharacteristicId id : ids)
		{
			m_Project.characteristics.erase(
				std::remove_if(
					m_Project.characteristics.begin(),
					m_Project.characteristics.end(),
					[id](const JFMCharacteristicInfo &info) { return info.id == id; }),
				m_Project.characteristics.end());
			m_CharacteristicData.erase(id);
			m_SelectedCharacteristicIds.erase(
				std::remove(m_SelectedCharacteristicIds.begin(), m_SelectedCharacteristicIds.end(), id),
				m_SelectedCharacteristicIds.end());
		}

		m_Project.series.erase(
			std::remove_if(
				m_Project.series.begin(),
				m_Project.series.end(),
				[&seriesId](const JFMSeries &candidate) { return candidate.id == seriesId; }),
			m_Project.series.end());
		if (m_SelectedSeriesId == seriesId)
			m_SelectedSeriesId.clear();
		if (std::find(ids.begin(), ids.end(), m_SelectedCharacteristicId) != ids.end())
			m_SelectedCharacteristicId = InvalidCharacteristicId;
		m_StatusMessage = "Series deleted.";
		ensureSelectionLocked();
		return true;
	}

	bool JFMWorkspaceStore::DeleteGroup(const std::string &groupId)
	{
		std::scoped_lock lock(m_Mutex);
		if (!findGroupLocked(groupId))
			return false;

		std::vector<std::string> groupIds;
		collectGroupIdsLocked(groupId, groupIds);
		std::vector<std::string> seriesIds;
		for (const JFMSeries &series : m_Project.series)
		{
			if (std::find(groupIds.begin(), groupIds.end(), series.groupId) != groupIds.end())
				seriesIds.push_back(series.id);
		}
		for (const std::string &seriesId : seriesIds)
		{
			auto series = findSeriesLocked(seriesId);
			if (!series)
				continue;
			for (const CharacteristicId id : series->get().characteristicIds)
			{
				m_Project.characteristics.erase(
					std::remove_if(
						m_Project.characteristics.begin(),
						m_Project.characteristics.end(),
						[id](const JFMCharacteristicInfo &info) { return info.id == id; }),
					m_Project.characteristics.end());
				m_CharacteristicData.erase(id);
			}
		}
		m_Project.series.erase(
			std::remove_if(
				m_Project.series.begin(),
				m_Project.series.end(),
				[&groupIds](const JFMSeries &series) {
					return std::find(groupIds.begin(), groupIds.end(), series.groupId) != groupIds.end();
				}),
			m_Project.series.end());
		m_Project.groups.erase(
			std::remove_if(
				m_Project.groups.begin(),
				m_Project.groups.end(),
				[&groupIds](const JFMGroup &group) {
					return std::find(groupIds.begin(), groupIds.end(), group.id) != groupIds.end();
				}),
			m_Project.groups.end());
		m_SelectedSeriesId.clear();
		m_SelectedCharacteristicId = InvalidCharacteristicId;
		m_SelectedCharacteristicIds.clear();
		m_StatusMessage = "Group deleted.";
		ensureSelectionLocked();
		return true;
	}

	bool JFMWorkspaceStore::MoveCharacteristic(const CharacteristicId &id, const std::string &targetSeriesId)
	{
		std::scoped_lock lock(m_Mutex);
		if (!findInfoLocked(id))
			return false;

		auto target = findSeriesLocked(targetSeriesId);
		if (!target)
			return false;

		removeCharacteristicReferencesLocked(id);
		target->get().characteristicIds.push_back(id);
		m_SelectedSeriesId = target->get().id;
		m_SelectedCharacteristicId = id;
		ensureSelectionLocked();
		return true;
	}

	JFMScriptInfo JFMWorkspaceStore::AddScript(Path path)
	{
		std::scoped_lock lock(m_Mutex);
		JFMScriptInfo script;
		script.id = nextId("script");
		script.path = std::move(path);
		script.name = script.path.filename().String();
		if (script.name.empty())
			script.name = script.id;
		m_Project.scripts.push_back(script);
		m_StatusMessage = "Script registered: " + script.name;
		return script;
	}

	bool JFMWorkspaceStore::RemoveScript(const std::string &scriptId)
	{
		std::scoped_lock lock(m_Mutex);
		const auto oldSize = m_Project.scripts.size();
		m_Project.scripts.erase(
			std::remove_if(
				m_Project.scripts.begin(),
				m_Project.scripts.end(),
				[&scriptId](const JFMScriptInfo &script) { return script.id == scriptId; }),
			m_Project.scripts.end());
		if (oldSize == m_Project.scripts.size())
			return false;
		m_StatusMessage = "Script removed.";
		return true;
	}

	bool JFMWorkspaceStore::ReloadScript(const std::string &scriptId)
	{
		std::scoped_lock lock(m_Mutex);
		const auto it = std::find_if(
			m_Project.scripts.begin(),
			m_Project.scripts.end(),
			[&scriptId](const JFMScriptInfo &script) { return script.id == scriptId; });
		if (it == m_Project.scripts.end())
			return false;
		++it->reloadCount;
		m_StatusMessage = "Script reloaded: " + it->name;
		return true;
	}

	void JFMWorkspaceStore::ResetProject(std::string name)
	{
		std::scoped_lock lock(m_Mutex);
		m_Project = MakeDefaultProject(std::move(name));
		m_CharacteristicData.clear();
		m_NextCharacteristicId = 1;
		m_ProjectPath.reset();
		m_LastImportWarnings.clear();
		m_ImportRunning = false;
		m_SelectedCharacteristicIds.clear();
		m_StatusMessage = "New JFM project created.";
		ensureSelectionLocked();
	}

	void JFMWorkspaceStore::ReplaceProject(JFMProject project, std::optional<Path> path)
	{
		std::scoped_lock lock(m_Mutex);
		m_Project = std::move(project);
		m_CharacteristicData.clear();
		updateNextCharacteristicIdLocked();
		m_ProjectPath = std::move(path);
		m_SelectedCharacteristicIds.clear();
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

	void JFMWorkspaceStore::SelectCharacteristic(CharacteristicId characteristicId)
	{
		std::scoped_lock lock(m_Mutex);
		m_SelectedCharacteristicId = std::move(characteristicId);
		m_SelectedCharacteristicIds = m_SelectedCharacteristicId == InvalidCharacteristicId
			? std::vector<CharacteristicId>{}
			: std::vector<CharacteristicId>{m_SelectedCharacteristicId};
		ensureSelectionLocked();
	}

	void JFMWorkspaceStore::SetSelectedCharacteristics(std::vector<CharacteristicId> characteristicIds, CharacteristicId primary, std::string seriesId)
	{
		std::scoped_lock lock(m_Mutex);
		characteristicIds.erase(
			std::remove_if(
				characteristicIds.begin(),
				characteristicIds.end(),
				[this](CharacteristicId id) { return !findInfoLocked(id).has_value(); }),
			characteristicIds.end());
		std::sort(characteristicIds.begin(), characteristicIds.end());
		characteristicIds.erase(std::unique(characteristicIds.begin(), characteristicIds.end()), characteristicIds.end());
		m_SelectedCharacteristicIds = std::move(characteristicIds);
		m_SelectedCharacteristicId = findInfoLocked(primary) ? primary : (m_SelectedCharacteristicIds.empty() ? InvalidCharacteristicId : m_SelectedCharacteristicIds.back());
		if (!seriesId.empty() && findSeriesLocked(seriesId))
			m_SelectedSeriesId = std::move(seriesId);
		ensureSelectionLocked();
	}

	void JFMWorkspaceStore::AddImportedCharacteristics(std::vector<JFMCharacteristic> characteristics)
	{
		(void)AddCharacteristics(std::move(characteristics), GetSelectedSeriesId());
	}

	std::size_t JFMWorkspaceStore::UpdateCharacteristicData(std::vector<JFMCharacteristic> characteristics)
	{
		std::scoped_lock lock(m_Mutex);
		std::size_t updated = 0;
		for (JFMCharacteristic &characteristic : characteristics)
		{
			if (characteristic.info.id == InvalidCharacteristicId || !findInfoLocked(characteristic.info.id))
				continue;

			m_CharacteristicData[characteristic.info.id] = std::move(characteristic.data);
			++updated;
		}
		ensureSelectionLocked();
		return updated;
	}

	JFMProject JFMWorkspaceStore::MakeDefaultProject(std::string name)
	{
		JFMProject project;
		project.id = "jfm-project";
		project.name = std::move(name);
		project.metadata["prototype"] = "JFM";
		project.series.push_back(JFMSeries{"series-default", "Series 1", {}, {}});
		return project;
	}

	std::string JFMWorkspaceStore::nextId(const char *prefix) const
	{
		std::string id;
		do
		{
			id = std::string(prefix) + "-" + std::to_string(m_NextId++);
		} while (
			std::any_of(m_Project.groups.begin(), m_Project.groups.end(), [&id](const JFMGroup &group) { return group.id == id; })
			|| std::any_of(m_Project.series.begin(), m_Project.series.end(), [&id](const JFMSeries &series) { return series.id == id; })
			|| std::any_of(m_Project.scripts.begin(), m_Project.scripts.end(), [&id](const JFMScriptInfo &script) { return script.id == id; }));
		return id;
	}

	JFMWorkspaceStore::OptionalRef<JFMGroup> JFMWorkspaceStore::findGroupLocked(const std::string &groupId)
	{
		const auto it = std::find_if(
			m_Project.groups.begin(),
			m_Project.groups.end(),
			[&groupId](const JFMGroup &group) { return group.id == groupId; });
		if (it == m_Project.groups.end())
			return std::nullopt;
		return std::ref(*it);
	}

	JFMWorkspaceStore::OptionalRef<const JFMGroup> JFMWorkspaceStore::findGroupLocked(const std::string &groupId) const
	{
		const auto it = std::find_if(
			m_Project.groups.begin(),
			m_Project.groups.end(),
			[&groupId](const JFMGroup &group) { return group.id == groupId; });
		if (it == m_Project.groups.end())
			return std::nullopt;
		return std::cref(*it);
	}

	JFMWorkspaceStore::OptionalRef<JFMGroup> JFMWorkspaceStore::findGroupByNameAndParentLocked(const std::string &name, const std::string &parentId)
	{
		const auto it = std::find_if(
			m_Project.groups.begin(),
			m_Project.groups.end(),
			[&name, &parentId](const JFMGroup &group) { return group.name == name && group.parentId == parentId; });
		if (it == m_Project.groups.end())
			return std::nullopt;
		return std::ref(*it);
	}

	JFMWorkspaceStore::OptionalRef<JFMSeries> JFMWorkspaceStore::findSeriesLocked(const std::string &seriesId)
	{
		const auto it = std::find_if(
			m_Project.series.begin(),
			m_Project.series.end(),
			[&seriesId](const JFMSeries &series) { return series.id == seriesId; });
		if (it == m_Project.series.end())
			return std::nullopt;
		return std::ref(*it);
	}

	JFMWorkspaceStore::OptionalRef<const JFMSeries> JFMWorkspaceStore::findSeriesLocked(const std::string &seriesId) const
	{
		const auto it = std::find_if(
			m_Project.series.begin(),
			m_Project.series.end(),
			[&seriesId](const JFMSeries &series) { return series.id == seriesId; });
		if (it == m_Project.series.end())
			return std::nullopt;
		return std::cref(*it);
	}

	JFMWorkspaceStore::OptionalRef<JFMCharacteristicInfo> JFMWorkspaceStore::findInfoLocked(const CharacteristicId &id)
	{
		const auto it = std::find_if(
			m_Project.characteristics.begin(),
			m_Project.characteristics.end(),
			[&id](const JFMCharacteristicInfo &info) { return info.id == id; });
		if (it == m_Project.characteristics.end())
			return std::nullopt;
		return std::ref(*it);
	}

	JFMWorkspaceStore::OptionalRef<const JFMCharacteristicInfo> JFMWorkspaceStore::findInfoLocked(const CharacteristicId &id) const
	{
		const auto it = std::find_if(
			m_Project.characteristics.begin(),
			m_Project.characteristics.end(),
			[&id](const JFMCharacteristicInfo &info) { return info.id == id; });
		if (it == m_Project.characteristics.end())
			return std::nullopt;
		return std::cref(*it);
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::makeCharacteristicLocked(const JFMCharacteristicInfo &info) const
	{
		JFMCharacteristic characteristic;
		characteristic.info = info;
		if (const auto dataIt = m_CharacteristicData.find(info.id); dataIt != m_CharacteristicData.end())
			characteristic.data = dataIt->second;
		return characteristic;
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::seriesCharacteristicsLocked(const JFMSeries &series) const
	{
		std::vector<JFMCharacteristic> characteristics;
		characteristics.reserve(series.characteristicIds.size());
		for (const CharacteristicId id : series.characteristicIds)
		{
			auto info = findInfoLocked(id);
			if (info)
				characteristics.push_back(*makeCharacteristicLocked(info->get()));
		}
		return characteristics;
	}

	void JFMWorkspaceStore::collectGroupIdsLocked(const std::string &groupId, std::vector<std::string> &groupIds) const
	{
		if (!findGroupLocked(groupId))
			return;
		groupIds.push_back(groupId);
		for (const JFMGroup &group : m_Project.groups)
		{
			if (group.parentId == groupId)
				collectGroupIdsLocked(group.id, groupIds);
		}
	}

	void JFMWorkspaceStore::ensureSelectionLocked()
	{
		if (m_Project.series.empty())
			m_Project.series.push_back(JFMSeries{nextId("series"), "Series 1", {}, {}});

		if (!findSeriesLocked(m_SelectedSeriesId))
			m_SelectedSeriesId = m_Project.series.front().id;

		if (findInfoLocked(m_SelectedCharacteristicId))
		{
			if (m_SelectedCharacteristicIds.empty())
				m_SelectedCharacteristicIds.push_back(m_SelectedCharacteristicId);
			return;
		}

		m_SelectedCharacteristicId = InvalidCharacteristicId;
		m_SelectedCharacteristicIds.clear();
		for (const JFMSeries &series : m_Project.series)
		{
			for (const CharacteristicId &id : series.characteristicIds)
			{
				if (findInfoLocked(id))
				{
					m_SelectedCharacteristicId = id;
					m_SelectedCharacteristicIds.push_back(id);
					return;
				}
			}
		}
	}

	void JFMWorkspaceStore::removeCharacteristicReferencesLocked(const CharacteristicId &id)
	{
		for (JFMSeries &series : m_Project.series)
		{
			series.characteristicIds.erase(
				std::remove(series.characteristicIds.begin(), series.characteristicIds.end(), id),
				series.characteristicIds.end());
		}
	}

	void JFMWorkspaceStore::updateNextCharacteristicIdLocked()
	{
		m_NextCharacteristicId = 1;
		for (const JFMCharacteristicInfo &info : m_Project.characteristics)
		{
			if (info.id >= m_NextCharacteristicId)
				m_NextCharacteristicId = info.id + 1;
		}
	}
} // namespace SST::Prototypes::JFM
