#include "Core/dspch.hpp"

#include "Workspace/JFMWorkspaceStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace SST::Prototypes::JFM
{
	namespace WorkspaceDetail
	{
		std::filesystem::path normalizedPath(const Path &path)
		{
			std::error_code error;
			std::filesystem::path normalized = std::filesystem::weakly_canonical(path.Native(), error);
			return (error ? path.Native() : normalized).lexically_normal();
		}

		std::string numberLabel(double value)
		{
			std::ostringstream stream;
			stream << std::setprecision(12) << value;
			return stream.str();
		}

		std::optional<double> parseNumber(const std::string &text)
		{
			if (text.empty())
				return std::nullopt;
			char *end = nullptr;
			const double value = std::strtod(text.c_str(), &end);
			return end != text.c_str() && end != nullptr && *end == '\0' ? std::optional<double>{value} : std::nullopt;
		}
	} // namespace WorkspaceDetail
	using namespace WorkspaceDetail;

	JFMWorkspaceStore::JFMWorkspaceStore()
	{
		(void)CreateProject();
	}

	std::vector<JFMProjectDocumentInfo> JFMWorkspaceStore::GetProjects() const
	{
		std::scoped_lock lock(m_Mutex);
		std::vector<JFMProjectDocumentInfo> projects;
		projects.reserve(m_Projects.size());
		for (const ProjectDocument &document : m_Projects)
		{
			if (document.hidden)
				continue;
			projects.push_back({
				document.handle,
				document.project.name,
				document.path,
				document.status,
				document.handle == m_ActiveProject,
				document.revision != document.savedRevision,
				document.importRunning});
		}
		return projects;
	}

	ProjectHandle JFMWorkspaceStore::GetActiveProjectHandle() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_ActiveProject;
	}

	bool JFMWorkspaceStore::SetActiveProject(ProjectHandle handle)
	{
		std::scoped_lock lock(m_Mutex);
		const auto document = findDocumentLocked(handle);
		if (!document || document->get().hidden)
			return false;
		m_ActiveProject = handle;
		return true;
	}

	ProjectHandle JFMWorkspaceStore::CreateProject(std::string name)
	{
		std::scoped_lock lock(m_Mutex);
		ProjectDocument document;
		document.handle = m_NextProjectHandle++;
		document.project = MakeDefaultProject(std::move(name));
		document.revision = m_NextRevision++;
		document.savedRevision = document.revision;
		ensureSelectionLocked(document);
		m_Projects.push_back(std::move(document));
		m_ActiveProject = m_Projects.back().handle;
		return m_ActiveProject;
	}

	ProjectHandle JFMWorkspaceStore::CreateTransientProject(std::string name)
	{
		std::scoped_lock lock(m_Mutex);
		ProjectDocument document;
		document.handle = m_NextProjectHandle++;
		document.project = MakeDefaultProject(std::move(name));
		document.hidden = true;
		document.status = "Import staging.";
		document.revision = m_NextRevision++;
		document.savedRevision = document.revision;
		ensureSelectionLocked(document);
		m_Projects.push_back(std::move(document));
		return m_Projects.back().handle;
	}

	ProjectHandle JFMWorkspaceStore::AttachProject(JFMProject project, std::optional<Path> path, bool activate)
	{
		std::scoped_lock lock(m_Mutex);
		if (path && !path->Empty())
		{
			const std::filesystem::path requested = normalizedPath(*path);
			for (ProjectDocument &existing : m_Projects)
			{
				if (existing.path && normalizedPath(*existing.path) == requested)
				{
					if (activate)
						m_ActiveProject = existing.handle;
					return existing.handle;
				}
			}
		}

		ProjectDocument document;
		document.handle = m_NextProjectHandle++;
		document.project = std::move(project);
		document.project.schemaVersion = JFMProjectSchemaVersion;
		document.path = std::move(path);
		document.status = "Project loaded.";
		document.revision = m_NextRevision++;
		document.savedRevision = document.revision;
		updateNextCharacteristicIdLocked(document);
		ensureSelectionLocked(document);
		m_Projects.push_back(std::move(document));
		if (activate || m_ActiveProject == InvalidProjectHandle)
			m_ActiveProject = m_Projects.back().handle;
		return m_Projects.back().handle;
	}

	ProjectHandle JFMWorkspaceStore::AttachProjectBundle(JFMProjectBundleSnapshot snapshot, std::optional<Path> path, bool activate)
	{
		std::scoped_lock lock(m_Mutex);
		if (path && !path->Empty())
		{
			const std::filesystem::path requested = normalizedPath(*path);
			for (ProjectDocument &existing : m_Projects)
			{
				if (existing.path && normalizedPath(*existing.path) == requested)
				{
					if (activate)
						m_ActiveProject = existing.handle;
					return existing.handle;
				}
			}
		}

		ProjectDocument document;
		document.handle = m_NextProjectHandle++;
		document.project = std::move(snapshot.project);
		document.project.schemaVersion = JFMProjectSchemaVersion;
		document.path = std::move(path);
		document.status = "Project bundle loaded.";
		document.revision = m_NextRevision++;
		document.savedRevision = document.revision;
		document.arrheniusLayout = std::move(snapshot.arrheniusLayout);
		for (JFMCharacteristicRecord &record : snapshot.characteristics)
		{
			if (record.info.id == InvalidCharacteristicId)
				continue;
			document.characteristicData[record.info.id] = std::move(record.data);
		}
		for (JFMMonteCarloBundleRecord &record : snapshot.monteCarloResults)
		{
			if (record.characteristic != InvalidCharacteristicId)
				document.monteCarloResults[record.characteristic] = CreateRef<const JFMMonteCarloResult>(std::move(record.result));
		}
		updateNextCharacteristicIdLocked(document);
		ensureSelectionLocked(document);
		m_Projects.push_back(std::move(document));
		if (activate || m_ActiveProject == InvalidProjectHandle)
			m_ActiveProject = m_Projects.back().handle;
		return m_Projects.back().handle;
	}

	std::optional<ProjectHandle> JFMWorkspaceStore::FindProjectByPath(const Path &path) const
	{
		if (path.Empty())
			return std::nullopt;
		std::scoped_lock lock(m_Mutex);
		const std::filesystem::path requested = normalizedPath(path);
		for (const ProjectDocument &document : m_Projects)
		{
			if (document.path && normalizedPath(*document.path) == requested)
				return document.handle;
		}
		return std::nullopt;
	}

	JFMProject JFMWorkspaceStore::GetProjectSnapshot() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().project;
	}

	JFMProject JFMWorkspaceStore::GetProjectSnapshot(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document ? document->get().project : JFMProject{};
	}

	std::optional<Path> JFMWorkspaceStore::GetProjectPath() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().path;
	}

	std::optional<Path> JFMWorkspaceStore::GetProjectPath(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document ? document->get().path : std::nullopt;
	}

	std::string JFMWorkspaceStore::GetStatusMessage() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().status;
	}

	std::string JFMWorkspaceStore::GetStatusMessage(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document ? document->get().status : std::string{};
	}

	bool JFMWorkspaceStore::IsImportRunning() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().importRunning;
	}

	bool JFMWorkspaceStore::IsImportRunning(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document && document->get().importRunning;
	}

	bool JFMWorkspaceStore::IsDirty(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document && document->get().revision != document->get().savedRevision;
	}

	std::uint64_t JFMWorkspaceStore::GetProjectRevision(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document ? document->get().revision : 0;
	}

	std::optional<JFMProjectStateSnapshot> JFMWorkspaceStore::CaptureProjectState(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return std::nullopt;
		const ProjectDocument &value = document->get();
		return JFMProjectStateSnapshot{
			value.project,
			value.characteristicData,
			value.selectedSeriesId,
			value.selectedCharacteristicId,
			value.selectedCharacteristicIds,
			value.revision};
	}

	std::optional<JFMProjectBundleSnapshot> JFMWorkspaceStore::CaptureProjectBundle(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return std::nullopt;
		const ProjectDocument &value = document->get();
		JFMProjectBundleSnapshot snapshot;
		snapshot.project = value.project;
		snapshot.revision = value.revision;
		snapshot.arrheniusLayout = value.arrheniusLayout;
		snapshot.characteristics.reserve(value.project.characteristics.size());
		for (const JFMCharacteristicInfo &info : value.project.characteristics)
		{
			const auto data = value.characteristicData.find(info.id);
			snapshot.characteristics.push_back(JFMCharacteristicRecord{
				info,
				data == value.characteristicData.end() ? Ref<const JFMCharacteristicData>{} : data->second});
		}
		snapshot.monteCarloResults.reserve(value.monteCarloResults.size());
		for (const auto &[id, result] : value.monteCarloResults)
		{
			if (result != nullptr)
				snapshot.monteCarloResults.push_back(JFMMonteCarloBundleRecord{id, *result});
		}
		return snapshot;
	}

	bool JFMWorkspaceStore::RestoreProjectState(ProjectHandle handle, JFMProjectStateSnapshot snapshot, bool activate)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		ProjectDocument &value = document->get();
		value.project = std::move(snapshot.project);
		value.characteristicData = std::move(snapshot.characteristicData);
		value.selectedSeriesId = std::move(snapshot.selectedSeriesId);
		value.selectedCharacteristicId = snapshot.selectedCharacteristicId;
		value.selectedCharacteristicIds = std::move(snapshot.selectedCharacteristicIds);
		value.revision = snapshot.revision;
		value.prefitCurves.clear();
		value.fitCurves.clear();
		value.monteCarloResults.clear();
		for (auto &[id, generation] : value.prefitGenerations)
		{
			(void)id;
			++generation;
		}
		for (auto &[id, generation] : value.fitGenerations)
		{
			(void)id;
			++generation;
		}
		for (auto &[id, generation] : value.monteCarloGenerations)
		{
			(void)id;
			++generation;
		}
		updateNextCharacteristicIdLocked(value);
		ensureSelectionLocked(value);
		if (activate && !value.hidden)
			m_ActiveProject = handle;
		return true;
	}

	std::optional<JFMArrheniusLayoutDocument> JFMWorkspaceStore::GetArrheniusLayout(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document ? document->get().arrheniusLayout : std::nullopt;
	}

	void JFMWorkspaceStore::SetArrheniusLayout(ProjectHandle handle, std::optional<JFMArrheniusLayoutDocument> layout)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (document)
			document->get().arrheniusLayout = std::move(layout);
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::GetSelectedCharacteristic() const
	{
		std::scoped_lock lock(m_Mutex);
		const ProjectDocument &document = activeDocumentLocked();
		auto info = findInfoLocked(document, document.selectedCharacteristicId);
		return info ? makeCharacteristicLocked(document, info->get()) : std::nullopt;
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetSelectedCharacteristics() const
	{
		std::scoped_lock lock(m_Mutex);
		const ProjectDocument &document = activeDocumentLocked();
		std::vector<JFMCharacteristic> characteristics;
		characteristics.reserve(document.selectedCharacteristicIds.size());
		for (const CharacteristicId id : document.selectedCharacteristicIds)
		{
			auto info = findInfoLocked(document, id);
			if (info)
				characteristics.push_back(*makeCharacteristicLocked(document, info->get()));
		}
		return characteristics;
	}

	std::optional<JFMCharacteristicRecord> JFMWorkspaceStore::GetSelectedCharacteristicRecord() const
	{
		std::scoped_lock lock(m_Mutex);
		const ProjectDocument &document = activeDocumentLocked();
		auto info = findInfoLocked(document, document.selectedCharacteristicId);
		return info ? makeCharacteristicRecordLocked(document, info->get()) : std::nullopt;
	}

	std::vector<JFMCharacteristicRecord> JFMWorkspaceStore::GetSelectedCharacteristicRecords() const
	{
		std::scoped_lock lock(m_Mutex);
		const ProjectDocument &document = activeDocumentLocked();
		std::vector<JFMCharacteristicRecord> characteristics;
		characteristics.reserve(document.selectedCharacteristicIds.size());
		for (const CharacteristicId id : document.selectedCharacteristicIds)
		{
			auto info = findInfoLocked(document, id);
			if (info)
				characteristics.emplace_back(*makeCharacteristicRecordLocked(document, info->get()));
		}
		return characteristics;
	}

	std::string JFMWorkspaceStore::GetSelectedSeriesId() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().selectedSeriesId;
	}

	std::string JFMWorkspaceStore::GetSelectedGroupId() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().selectedGroupId;
	}

	CharacteristicId JFMWorkspaceStore::GetSelectedCharacteristicId() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().selectedCharacteristicId;
	}

	std::vector<CharacteristicId> JFMWorkspaceStore::GetSelectedCharacteristicIds() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().selectedCharacteristicIds;
	}

	std::vector<std::string> JFMWorkspaceStore::GetLastImportWarnings() const
	{
		std::scoped_lock lock(m_Mutex);
		return activeDocumentLocked().lastImportWarnings;
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::FindCharacteristic(const CharacteristicId &id) const
	{
		return FindCharacteristic({GetActiveProjectHandle(), id});
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::FindCharacteristic(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return std::nullopt;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		return info ? makeCharacteristicLocked(document->get(), info->get()) : std::nullopt;
	}

	std::optional<JFMCharacteristicRecord> JFMWorkspaceStore::FindCharacteristicRecord(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return std::nullopt;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		return info ? makeCharacteristicRecordLocked(document->get(), info->get()) : std::nullopt;
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::FindCharacteristicByName(const std::string &name) const
	{
		std::scoped_lock lock(m_Mutex);
		const ProjectDocument &document = activeDocumentLocked();
		const auto it = std::find_if(document.project.characteristics.begin(), document.project.characteristics.end(), [&name](const JFMCharacteristicInfo &info) {
			return info.name == name;
		});
		return it != document.project.characteristics.end() ? makeCharacteristicLocked(document, *it) : std::nullopt;
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetSeriesCharacteristics(const std::string &seriesId) const
	{
		return GetSeriesCharacteristics(GetActiveProjectHandle(), seriesId);
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetSeriesCharacteristics(ProjectHandle handle, const std::string &seriesId) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return {};
		auto series = findSeriesLocked(document->get(), seriesId);
		return series ? seriesCharacteristicsLocked(document->get(), series->get()) : std::vector<JFMCharacteristic>{};
	}

	std::vector<JFMCharacteristicRecord> JFMWorkspaceStore::GetSeriesCharacteristicRecords(ProjectHandle handle, const std::string &seriesId) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return {};
		auto series = findSeriesLocked(document->get(), seriesId);
		return series ? seriesCharacteristicRecordsLocked(document->get(), series->get()) : std::vector<JFMCharacteristicRecord>{};
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetGroupCharacteristics(const std::string &groupId) const
	{
		return GetGroupCharacteristics(GetActiveProjectHandle(), groupId);
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::GetGroupCharacteristics(ProjectHandle handle, const std::string &groupId) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return {};
		const ProjectDocument &value = document->get();
		std::vector<std::string> groupIds;
		collectGroupIdsLocked(value, groupId, groupIds);
		std::vector<JFMCharacteristic> characteristics;
		for (const JFMSeries &series : value.project.series)
		{
			if (std::find(groupIds.begin(), groupIds.end(), series.groupId) == groupIds.end())
				continue;
			std::vector<JFMCharacteristic> current = seriesCharacteristicsLocked(value, series);
			characteristics.insert(characteristics.end(), current.begin(), current.end());
		}
		return characteristics;
	}

	std::vector<CharacteristicId> JFMWorkspaceStore::GetGroupCharacteristicIds(ProjectHandle handle, const std::string &groupId) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return {};
		const ProjectDocument &value = document->get();
		std::vector<std::string> groupIds;
		collectGroupIdsLocked(value, groupId, groupIds);
		std::vector<CharacteristicId> ids;
		for (const JFMSeries &series : value.project.series)
		{
			if (std::find(groupIds.begin(), groupIds.end(), series.groupId) != groupIds.end())
				ids.insert(ids.end(), series.characteristicIds.begin(), series.characteristicIds.end());
		}
		return ids;
	}

	std::optional<JFMPrefitState> JFMWorkspaceStore::GetPrefitState(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return std::nullopt;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		return info ? info->get().prefit : std::nullopt;
	}

	Ref<const JFMPrefitCurve> JFMWorkspaceStore::GetPrefitCurve(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return {};
		const auto curve = document->get().prefitCurves.find(ref.characteristic);
		return curve == document->get().prefitCurves.end() ? Ref<const JFMPrefitCurve>{} : curve->second;
	}

	std::optional<JFMFitState> JFMWorkspaceStore::GetFitState(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return std::nullopt;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		return info ? info->get().fit : std::nullopt;
	}

	Ref<const JFMFitCurve> JFMWorkspaceStore::GetFitCurve(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return {};
		const auto curve = document->get().fitCurves.find(ref.characteristic);
		return curve == document->get().fitCurves.end() ? Ref<const JFMFitCurve>{} : curve->second;
	}

	std::optional<JFMMonteCarloState> JFMWorkspaceStore::GetMonteCarloState(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return std::nullopt;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		return info ? info->get().monteCarlo : std::nullopt;
	}

	Ref<const JFMMonteCarloResult> JFMWorkspaceStore::GetMonteCarloResult(JFMCharacteristicRef ref) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return {};
		const auto result = document->get().monteCarloResults.find(ref.characteristic);
		return result == document->get().monteCarloResults.end() ? Ref<const JFMMonteCarloResult>{} : result->second;
	}

	JFMGroup JFMWorkspaceStore::AddGroup(std::string name, std::string parentId)
	{
		return AddGroup(GetActiveProjectHandle(), std::move(name), std::move(parentId));
	}

	JFMGroup JFMWorkspaceStore::AddGroup(ProjectHandle handle, std::string name, std::string parentId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return {};
		ProjectDocument &document = documentRef->get();
		if (name.empty())
			name = "Group " + std::to_string(document.project.groups.size() + 1);
		if (!parentId.empty() && !findGroupLocked(document, parentId))
			parentId.clear();
		JFMGroup group{nextId(document, "group"), std::move(name), std::move(parentId)};
		document.project.groups.push_back(group);
		bumpRevisionLocked(document);
		return group;
	}

	std::string JFMWorkspaceStore::FindOrCreateGroupPath(const std::vector<std::string> &names)
	{
		return FindOrCreateGroupPath(GetActiveProjectHandle(), names);
	}

	std::string JFMWorkspaceStore::FindOrCreateGroupPath(ProjectHandle handle, const std::vector<std::string> &names, std::string parentId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return {};
		ProjectDocument &document = documentRef->get();
		bool changed = false;
		for (const std::string &name : names)
		{
			if (name.empty())
				continue;
			auto existing = findGroupByNameAndParentLocked(document, name, parentId);
			if (existing)
			{
				parentId = existing->get().id;
				continue;
			}
			JFMGroup group{nextId(document, "group"), name, parentId};
			parentId = group.id;
			document.project.groups.push_back(std::move(group));
			changed = true;
		}
		if (changed)
			bumpRevisionLocked(document);
		return parentId;
	}

	JFMSeries JFMWorkspaceStore::AddSeries(std::string name)
	{
		return AddSeries(GetActiveProjectHandle(), std::move(name));
	}

	JFMSeries JFMWorkspaceStore::AddSeries(std::string name, std::string groupId)
	{
		return AddSeries(GetActiveProjectHandle(), std::move(name), std::move(groupId));
	}

	JFMSeries JFMWorkspaceStore::AddSeries(ProjectHandle handle, std::string name, std::string groupId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return {};
		ProjectDocument &document = documentRef->get();
		if (!groupId.empty() && !findGroupLocked(document, groupId))
			groupId.clear();
		JFMSeries series{nextId(document, "series"), std::move(name), std::move(groupId), {}};
		if (series.name.empty())
			series.name = "Series " + std::to_string(document.project.series.size() + 1);
		document.project.series.push_back(series);
		document.selectedSeriesId = series.id;
		ensureSelectionLocked(document);
		bumpRevisionLocked(document);
		return series;
	}

	std::vector<CharacteristicId> JFMWorkspaceStore::AddCharacteristics(std::vector<JFMCharacteristic> characteristics, std::string targetSeriesId)
	{
		return AddCharacteristics(GetActiveProjectHandle(), std::move(characteristics), std::move(targetSeriesId));
	}

	std::vector<CharacteristicId> JFMWorkspaceStore::AddCharacteristics(ProjectHandle handle, std::vector<JFMCharacteristic> characteristics, std::string targetSeriesId)
	{
		std::vector<JFMCharacteristicRecord> records;
		records.reserve(characteristics.size());
		for (JFMCharacteristic &characteristic : characteristics)
			records.push_back({std::move(characteristic.info), CreateRef<const JFMCharacteristicData>(std::move(characteristic.data))});
		return AddCharacteristicRecords(handle, std::move(records), std::move(targetSeriesId));
	}

	std::vector<CharacteristicId> JFMWorkspaceStore::AddCharacteristicRecords(ProjectHandle handle, std::vector<JFMCharacteristicRecord> characteristics, std::string targetSeriesId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef || characteristics.empty())
			return {};
		ProjectDocument &document = documentRef->get();
		ensureSelectionLocked(document);
		if (document.project.series.empty())
			document.project.series.push_back(JFMSeries{nextId(document, "series"), "Series 1", {}, {}});
		if (targetSeriesId.empty())
			targetSeriesId = document.selectedSeriesId.empty() ? document.project.series.front().id : document.selectedSeriesId;
		auto targetRef = findSeriesLocked(document, targetSeriesId);
		if (!targetRef)
			targetRef = std::ref(document.project.series.front());
		JFMSeries &target = targetRef->get();

		std::vector<CharacteristicId> addedIds;
		addedIds.reserve(characteristics.size());
		for (JFMCharacteristicRecord &characteristic : characteristics)
		{
			if (characteristic.info.id == InvalidCharacteristicId)
				characteristic.info.id = document.nextCharacteristicId++;
			else if (characteristic.info.id >= document.nextCharacteristicId)
				document.nextCharacteristicId = characteristic.info.id + 1;
			if (characteristic.info.name.empty())
			{
				characteristic.info.name = characteristic.info.sourcePath.Empty()
					? std::to_string(characteristic.info.id)
					: characteristic.info.sourcePath.filename().String();
			}
			removeCharacteristicReferencesLocked(document, characteristic.info.id);
			if (auto existing = findInfoLocked(document, characteristic.info.id))
				existing->get() = characteristic.info;
			else
				document.project.characteristics.push_back(characteristic.info);
			document.characteristicData[characteristic.info.id] = std::move(characteristic.data);
			document.prefitCurves.erase(characteristic.info.id);
			++document.prefitGenerations[characteristic.info.id];
			document.fitCurves.erase(characteristic.info.id);
			++document.fitGenerations[characteristic.info.id];
			document.monteCarloResults.erase(characteristic.info.id);
			++document.monteCarloGenerations[characteristic.info.id];
			target.characteristicIds.push_back(characteristic.info.id);
			document.selectedCharacteristicId = characteristic.info.id;
			addedIds.push_back(characteristic.info.id);
		}
		document.selectedSeriesId = target.id;
		document.selectedCharacteristicIds = addedIds;
		document.status = "Imported " + std::to_string(characteristics.size()) + " characteristic(s).";
		ensureSelectionLocked(document);
		bumpRevisionLocked(document);
		return addedIds;
	}

	bool JFMWorkspaceStore::DeleteCharacteristic(const CharacteristicId &id)
	{
		return DeleteCharacteristic(GetActiveProjectHandle(), id);
	}

	bool JFMWorkspaceStore::DeleteCharacteristic(ProjectHandle handle, const CharacteristicId &id)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return false;
		ProjectDocument &document = documentRef->get();
		const auto oldSize = document.project.characteristics.size();
		document.project.characteristics.erase(std::remove_if(document.project.characteristics.begin(), document.project.characteristics.end(), [id](const JFMCharacteristicInfo &info) {
			return info.id == id;
		}), document.project.characteristics.end());
		if (document.project.characteristics.size() == oldSize)
			return false;
		document.characteristicData.erase(id);
		document.prefitCurves.erase(id);
		document.prefitGenerations.erase(id);
		document.fitCurves.erase(id);
		document.fitGenerations.erase(id);
		document.monteCarloResults.erase(id);
		document.monteCarloGenerations.erase(id);
		removeCharacteristicReferencesLocked(document, id);
		document.selectedCharacteristicIds.erase(std::remove(document.selectedCharacteristicIds.begin(), document.selectedCharacteristicIds.end(), id), document.selectedCharacteristicIds.end());
		if (document.selectedCharacteristicId == id)
			document.selectedCharacteristicId = InvalidCharacteristicId;
		ensureSelectionLocked(document);
		bumpRevisionLocked(document);
		return true;
	}

	bool JFMWorkspaceStore::DeleteSeries(const std::string &seriesId)
	{
		return DeleteSeries(GetActiveProjectHandle(), seriesId);
	}

	bool JFMWorkspaceStore::DeleteSeries(ProjectHandle handle, const std::string &seriesId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return false;
		ProjectDocument &document = documentRef->get();
		auto series = findSeriesLocked(document, seriesId);
		if (!series)
			return false;
		const std::vector<CharacteristicId> ids = series->get().characteristicIds;
		for (const CharacteristicId id : ids)
		{
			document.project.characteristics.erase(std::remove_if(document.project.characteristics.begin(), document.project.characteristics.end(), [id](const JFMCharacteristicInfo &info) {
				return info.id == id;
			}), document.project.characteristics.end());
			document.characteristicData.erase(id);
			document.prefitCurves.erase(id);
			document.prefitGenerations.erase(id);
			document.fitCurves.erase(id);
			document.fitGenerations.erase(id);
			document.monteCarloResults.erase(id);
			document.monteCarloGenerations.erase(id);
			document.selectedCharacteristicIds.erase(std::remove(document.selectedCharacteristicIds.begin(), document.selectedCharacteristicIds.end(), id), document.selectedCharacteristicIds.end());
		}
		document.project.series.erase(std::remove_if(document.project.series.begin(), document.project.series.end(), [&seriesId](const JFMSeries &candidate) {
			return candidate.id == seriesId;
		}), document.project.series.end());
		for (JFMGroupingDefinition &grouping : document.project.groupings)
			grouping.sourceSeriesIds.erase(std::remove(grouping.sourceSeriesIds.begin(), grouping.sourceSeriesIds.end(), seriesId), grouping.sourceSeriesIds.end());
		if (document.selectedSeriesId == seriesId)
			document.selectedSeriesId.clear();
		if (std::find(ids.begin(), ids.end(), document.selectedCharacteristicId) != ids.end())
			document.selectedCharacteristicId = InvalidCharacteristicId;
		document.status = "Series deleted.";
		ensureSelectionLocked(document);
		bumpRevisionLocked(document);
		return true;
	}

	bool JFMWorkspaceStore::DeleteGroup(const std::string &groupId)
	{
		return DeleteGroup(GetActiveProjectHandle(), groupId);
	}

	bool JFMWorkspaceStore::DeleteGroup(ProjectHandle handle, const std::string &groupId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return false;
		ProjectDocument &document = documentRef->get();
		if (!findGroupLocked(document, groupId))
			return false;
		std::vector<std::string> groupIds;
		collectGroupIdsLocked(document, groupId, groupIds);
		std::vector<std::string> seriesIds;
		std::vector<CharacteristicId> characteristicIds;
		for (const JFMSeries &series : document.project.series)
		{
			if (std::find(groupIds.begin(), groupIds.end(), series.groupId) != groupIds.end())
			{
				seriesIds.push_back(series.id);
				characteristicIds.insert(characteristicIds.end(), series.characteristicIds.begin(), series.characteristicIds.end());
			}
		}
		document.project.characteristics.erase(std::remove_if(document.project.characteristics.begin(), document.project.characteristics.end(), [&characteristicIds](const JFMCharacteristicInfo &info) {
			return std::find(characteristicIds.begin(), characteristicIds.end(), info.id) != characteristicIds.end();
		}), document.project.characteristics.end());
		for (CharacteristicId id : characteristicIds)
		{
			document.characteristicData.erase(id);
			document.prefitCurves.erase(id);
			document.prefitGenerations.erase(id);
			document.fitCurves.erase(id);
			document.fitGenerations.erase(id);
			document.monteCarloResults.erase(id);
			document.monteCarloGenerations.erase(id);
		}
		document.project.series.erase(std::remove_if(document.project.series.begin(), document.project.series.end(), [&seriesIds](const JFMSeries &series) {
			return std::find(seriesIds.begin(), seriesIds.end(), series.id) != seriesIds.end();
		}), document.project.series.end());
		document.project.groups.erase(std::remove_if(document.project.groups.begin(), document.project.groups.end(), [&groupIds](const JFMGroup &group) {
			return std::find(groupIds.begin(), groupIds.end(), group.id) != groupIds.end();
		}), document.project.groups.end());
		for (JFMGroupingDefinition &grouping : document.project.groupings)
		{
			grouping.sourceSeriesIds.erase(std::remove_if(grouping.sourceSeriesIds.begin(), grouping.sourceSeriesIds.end(), [&seriesIds](const std::string &id) {
				return std::find(seriesIds.begin(), seriesIds.end(), id) != seriesIds.end();
			}), grouping.sourceSeriesIds.end());
		}
		document.selectedSeriesId.clear();
		document.selectedCharacteristicId = InvalidCharacteristicId;
		document.selectedCharacteristicIds.clear();
		document.status = "Group deleted.";
		ensureSelectionLocked(document);
		bumpRevisionLocked(document);
		return true;
	}

	bool JFMWorkspaceStore::MoveCharacteristic(const CharacteristicId &id, const std::string &targetSeriesId)
	{
		return MoveCharacteristic(GetActiveProjectHandle(), id, targetSeriesId);
	}

	bool JFMWorkspaceStore::MoveCharacteristic(ProjectHandle handle, const CharacteristicId &id, const std::string &targetSeriesId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return false;
		ProjectDocument &document = documentRef->get();
		if (!findInfoLocked(document, id))
			return false;
		auto target = findSeriesLocked(document, targetSeriesId);
		if (!target)
			return false;
		if (std::find(target->get().characteristicIds.begin(), target->get().characteristicIds.end(), id) != target->get().characteristicIds.end())
			return true;
		removeCharacteristicReferencesLocked(document, id);
		target->get().characteristicIds.push_back(id);
		document.selectedSeriesId = target->get().id;
		document.selectedCharacteristicId = id;
		ensureSelectionLocked(document);
		bumpRevisionLocked(document);
		return true;
	}

	bool JFMWorkspaceStore::RenameProject(std::string name)
	{
		return RenameProject(GetActiveProjectHandle(), std::move(name));
	}

	bool JFMWorkspaceStore::RenameProject(ProjectHandle handle, std::string name)
	{
		if (name.empty())
			return false;
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document || document->get().project.name == name)
			return false;
		document->get().project.name = std::move(name);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::RenameGroup(const std::string &groupId, std::string name)
	{
		return RenameGroup(GetActiveProjectHandle(), groupId, std::move(name));
	}

	bool JFMWorkspaceStore::RenameGroup(ProjectHandle handle, const std::string &groupId, std::string name)
	{
		if (name.empty())
			return false;
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto group = findGroupLocked(document->get(), groupId);
		if (!group || group->get().name == name)
			return false;
		group->get().name = std::move(name);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::RenameSeries(const std::string &seriesId, std::string name)
	{
		return RenameSeries(GetActiveProjectHandle(), seriesId, std::move(name));
	}

	bool JFMWorkspaceStore::RenameSeries(ProjectHandle handle, const std::string &seriesId, std::string name)
	{
		if (name.empty())
			return false;
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto series = findSeriesLocked(document->get(), seriesId);
		if (!series || series->get().name == name)
			return false;
		series->get().name = std::move(name);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::RenameCharacteristic(const CharacteristicId &id, std::string name)
	{
		return RenameCharacteristic(GetActiveProjectHandle(), id, std::move(name));
	}

	bool JFMWorkspaceStore::RenameCharacteristic(ProjectHandle handle, const CharacteristicId &id, std::string name)
	{
		if (name.empty())
			return false;
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto info = findInfoLocked(document->get(), id);
		if (!info || info->get().name == name)
			return false;
		info->get().name = std::move(name);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::SetGroupShortName(const std::string &groupId, std::string shortName)
	{
		return SetGroupShortName(GetActiveProjectHandle(), groupId, std::move(shortName));
	}

	bool JFMWorkspaceStore::SetGroupShortName(ProjectHandle handle, const std::string &groupId, std::string shortName)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto group = findGroupLocked(document->get(), groupId);
		if (!group)
			return false;
		if (shortName.empty())
			group->get().metadata.erase("short_name");
		else
			group->get().metadata["short_name"] = std::move(shortName);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::SetSeriesShortName(const std::string &seriesId, std::string shortName)
	{
		return SetSeriesShortName(GetActiveProjectHandle(), seriesId, std::move(shortName));
	}

	bool JFMWorkspaceStore::SetSeriesShortName(ProjectHandle handle, const std::string &seriesId, std::string shortName)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto series = findSeriesLocked(document->get(), seriesId);
		if (!series)
			return false;
		if (shortName.empty())
			series->get().metadata.erase("short_name");
		else
			series->get().metadata["short_name"] = std::move(shortName);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::SetCharacteristicShortName(const CharacteristicId &id, std::string shortName)
	{
		return SetCharacteristicShortName(GetActiveProjectHandle(), id, std::move(shortName));
	}

	bool JFMWorkspaceStore::SetCharacteristicShortName(ProjectHandle handle, const CharacteristicId &id, std::string shortName)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto info = findInfoLocked(document->get(), id);
		if (!info)
			return false;
		if (shortName.empty())
			info->get().metadata.erase("short_name");
		else
			info->get().metadata["short_name"] = std::move(shortName);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::SetCharacteristicPlotColor(ProjectHandle handle, const std::vector<CharacteristicId> &ids, std::string color)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		bool changed = false;
		for (CharacteristicId id : ids)
		{
			auto info = findInfoLocked(document->get(), id);
			if (!info)
				continue;
			if (color.empty())
			{
				changed = info->get().metadata.erase("plot_color") > 0 || changed;
				continue;
			}
			if (info->get().metadata["plot_color"] == color)
				continue;
			info->get().metadata["plot_color"] = color;
			changed = true;
		}
		if (changed)
			bumpRevisionLocked(document->get());
		return changed;
	}

	bool JFMWorkspaceStore::SetCharacteristicMetadata(ProjectHandle handle, const std::vector<CharacteristicId> &ids, std::string key, std::string value)
	{
		if (key.empty())
			return false;
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		bool changed = false;
		for (CharacteristicId id : ids)
		{
			auto info = findInfoLocked(document->get(), id);
			if (!info)
				continue;
			if (value.empty())
			{
				changed = info->get().metadata.erase(key) > 0 || changed;
				continue;
			}
			if (info->get().metadata[key] == value)
				continue;
			info->get().metadata[key] = value;
			changed = true;
		}
		if (changed)
			bumpRevisionLocked(document->get());
		return changed;
	}

	bool JFMWorkspaceStore::SetPrefitModel(ProjectHandle handle, const std::vector<CharacteristicId> &ids, const std::string &modelId)
	{
		if (modelId.empty())
			return false;
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		bool changed = false;
		for (CharacteristicId id : ids)
		{
			auto info = findInfoLocked(document->get(), id);
			if (!info)
				continue;
			JFMPrefitState &state = info->get().prefit.emplace(info->get().prefit.value_or(JFMPrefitState{}));
			if (state.modelId == modelId && !state.autoModelSelection)
				continue;
			state.modelId = modelId;
			state.autoModelSelection = false;
			state.status = "queued";
			state.warnings.clear();
			++document->get().prefitGenerations[id];
			info->get().fit.reset();
			info->get().monteCarlo.reset();
			document->get().fitCurves.erase(id);
			document->get().monteCarloResults.erase(id);
			++document->get().fitGenerations[id];
			++document->get().monteCarloGenerations[id];
			changed = true;
		}
		if (changed)
			bumpRevisionLocked(document->get());
		return changed;
	}

	bool JFMWorkspaceStore::SetManualFitRange(ProjectHandle handle, CharacteristicId id, double lowerVoltage, double upperVoltage)
	{
		if (!std::isfinite(lowerVoltage) || !std::isfinite(upperVoltage) || lowerVoltage == upperVoltage)
			return false;
		if (lowerVoltage > upperVoltage)
			std::swap(lowerVoltage, upperVoltage);
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto info = findInfoLocked(document->get(), id);
		if (!info)
			return false;
		JFMPrefitState &state = info->get().prefit.emplace(info->get().prefit.value_or(JFMPrefitState{}));
		if (state.manualLowerVoltage == lowerVoltage && state.manualUpperVoltage == upperVoltage && !state.autoModelSelection)
			return false;
		state.manualLowerVoltage = lowerVoltage;
		state.manualUpperVoltage = upperVoltage;
		state.autoModelSelection = false;
		state.status = "queued";
		state.warnings.clear();
		++document->get().prefitGenerations[id];
		info->get().fit.reset();
		info->get().monteCarlo.reset();
		document->get().fitCurves.erase(id);
		document->get().monteCarloResults.erase(id);
		++document->get().fitGenerations[id];
		++document->get().monteCarloGenerations[id];
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::ResetFitRange(ProjectHandle handle, CharacteristicId id)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto info = findInfoLocked(document->get(), id);
		if (!info || !info->get().prefit
			|| (!info->get().prefit->manualLowerVoltage && !info->get().prefit->manualUpperVoltage))
			return false;
		info->get().prefit->manualLowerVoltage.reset();
		info->get().prefit->manualUpperVoltage.reset();
		info->get().prefit->status = "queued";
		info->get().prefit->warnings.clear();
		++document->get().prefitGenerations[id];
		info->get().fit.reset();
		info->get().monteCarlo.reset();
		document->get().fitCurves.erase(id);
		document->get().monteCarloResults.erase(id);
		++document->get().fitGenerations[id];
		++document->get().monteCarloGenerations[id];
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::SetAutoModelSelection(ProjectHandle handle, const std::vector<CharacteristicId> &ids, bool enabled)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		bool changed = false;
		for (CharacteristicId id : ids)
		{
			auto info = findInfoLocked(document->get(), id);
			if (!info)
				continue;
			JFMPrefitState &state = info->get().prefit.emplace(info->get().prefit.value_or(JFMPrefitState{}));
			if (state.autoModelSelection == enabled)
				continue;
			state.autoModelSelection = enabled;
			if (enabled)
			{
				// The search picks its own range (docs/model-detection.md) --
				// a leftover manual override would otherwise pin it to a window
				// the search never gets to reconsider.
				state.manualLowerVoltage.reset();
				state.manualUpperVoltage.reset();
			}
			state.status = "queued";
			state.warnings.clear();
			++document->get().prefitGenerations[id];
			info->get().fit.reset();
			info->get().monteCarlo.reset();
			document->get().fitCurves.erase(id);
			document->get().monteCarloResults.erase(id);
			++document->get().fitGenerations[id];
			++document->get().monteCarloGenerations[id];
			changed = true;
		}
		if (changed)
			bumpRevisionLocked(document->get());
		return changed;
	}

	std::uint64_t JFMWorkspaceStore::BeginPrefit(JFMCharacteristicRef ref)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return 0;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info)
			return 0;
		JFMPrefitState &state = info->get().prefit.emplace(info->get().prefit.value_or(JFMPrefitState{}));
		state.status = "running";
		state.warnings.clear();
		info->get().fit.reset();
		info->get().monteCarlo.reset();
		document->get().fitCurves.erase(ref.characteristic);
		document->get().monteCarloResults.erase(ref.characteristic);
		++document->get().fitGenerations[ref.characteristic];
		++document->get().monteCarloGenerations[ref.characteristic];
		return ++document->get().prefitGenerations[ref.characteristic];
	}

	bool JFMWorkspaceStore::IsPrefitGenerationCurrent(JFMCharacteristicRef ref, std::uint64_t generation) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto current = document->get().prefitGenerations.find(ref.characteristic);
		return current != document->get().prefitGenerations.end() && current->second == generation;
	}

	bool JFMWorkspaceStore::ApplyPrefitResult(JFMCharacteristicRef ref, std::uint64_t generation, JFMPrefitState result, JFMPrefitCurve curve)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto currentGeneration = document->get().prefitGenerations.find(ref.characteristic);
		if (currentGeneration == document->get().prefitGenerations.end() || currentGeneration->second != generation)
			return false;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info)
			return false;
		const JFMPrefitState current = info->get().prefit.value_or(JFMPrefitState{});
		// In auto mode the whole point is that the returned modelId can differ
		// from whatever modelId was last persisted (that's the branch/model
		// search's pick, not a stale echo) -- generation match above is what
		// proves freshness there; the modelId-equality check only makes sense
		// for the fixed-model manual path.
		if (!current.autoModelSelection && current.modelId != result.modelId)
			return false;
		result.manualLowerVoltage = current.manualLowerVoltage;
		result.manualUpperVoltage = current.manualUpperVoltage;
		result.autoModelSelection = current.autoModelSelection;
		result.status = "completed";
		info->get().prefit = std::move(result);
		document->get().prefitCurves[ref.characteristic] = CreateRef<const JFMPrefitCurve>(std::move(curve));
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::FailPrefit(JFMCharacteristicRef ref, std::uint64_t generation, std::string error)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto currentGeneration = document->get().prefitGenerations.find(ref.characteristic);
		if (currentGeneration == document->get().prefitGenerations.end() || currentGeneration->second != generation)
			return false;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info)
			return false;
		JFMPrefitState &state = info->get().prefit.emplace(info->get().prefit.value_or(JFMPrefitState{}));
		state.status = "failed";
		state.warnings = {std::move(error)};
		state.parameters.clear();
		document->get().prefitCurves.erase(ref.characteristic);
		info->get().fit.reset();
		info->get().monteCarlo.reset();
		document->get().fitCurves.erase(ref.characteristic);
		document->get().monteCarloResults.erase(ref.characteristic);
		++document->get().fitGenerations[ref.characteristic];
		++document->get().monteCarloGenerations[ref.characteristic];
		bumpRevisionLocked(document->get());
		return true;
	}

	std::uint64_t JFMWorkspaceStore::BeginFit(JFMCharacteristicRef ref)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return 0;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info || !info->get().prefit || info->get().prefit->status != "completed")
			return 0;
		JFMFitState &state = info->get().fit.emplace(info->get().fit.value_or(JFMFitState{}));
		state.modelId = info->get().prefit->modelId;
		state.fitRange = info->get().prefit->fitRange;
		state.status = "running";
		state.warnings.clear();
		info->get().monteCarlo.reset();
		document->get().fitCurves.erase(ref.characteristic);
		document->get().monteCarloResults.erase(ref.characteristic);
		++document->get().monteCarloGenerations[ref.characteristic];
		return ++document->get().fitGenerations[ref.characteristic];
	}

	bool JFMWorkspaceStore::IsFitGenerationCurrent(JFMCharacteristicRef ref, std::uint64_t generation) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto current = document->get().fitGenerations.find(ref.characteristic);
		return current != document->get().fitGenerations.end() && current->second == generation;
	}

	bool JFMWorkspaceStore::ApplyFitResult(JFMCharacteristicRef ref, std::uint64_t generation, JFMFitState result, JFMFitCurve curve)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto current = document->get().fitGenerations.find(ref.characteristic);
		if (current == document->get().fitGenerations.end() || current->second != generation)
			return false;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info || !info->get().prefit || info->get().prefit->modelId != result.modelId)
			return false;
		result.status = "completed";
		info->get().fit = std::move(result);
		document->get().fitCurves[ref.characteristic] = CreateRef<const JFMFitCurve>(std::move(curve));
		info->get().monteCarlo.reset();
		document->get().monteCarloResults.erase(ref.characteristic);
		++document->get().monteCarloGenerations[ref.characteristic];
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::FailFit(JFMCharacteristicRef ref, std::uint64_t generation, std::string error)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto current = document->get().fitGenerations.find(ref.characteristic);
		if (current == document->get().fitGenerations.end() || current->second != generation)
			return false;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info)
			return false;
		JFMFitState &state = info->get().fit.emplace(info->get().fit.value_or(JFMFitState{}));
		state.status = "failed";
		state.warnings = {std::move(error)};
		state.parameters.clear();
		document->get().fitCurves.erase(ref.characteristic);
		info->get().monteCarlo.reset();
		document->get().monteCarloResults.erase(ref.characteristic);
		++document->get().monteCarloGenerations[ref.characteristic];
		bumpRevisionLocked(document->get());
		return true;
	}

	std::uint64_t JFMWorkspaceStore::BeginMonteCarlo(JFMCharacteristicRef ref)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return 0;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info || !info->get().fit || info->get().fit->status != "completed")
			return 0;
		JFMMonteCarloState &state = info->get().monteCarlo.emplace(info->get().monteCarlo.value_or(JFMMonteCarloState{}));
		state.modelId = info->get().fit->modelId;
		state.status = "running";
		state.warnings.clear();
		document->get().monteCarloResults.erase(ref.characteristic);
		return ++document->get().monteCarloGenerations[ref.characteristic];
	}

	bool JFMWorkspaceStore::IsMonteCarloGenerationCurrent(JFMCharacteristicRef ref, std::uint64_t generation) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto current = document->get().monteCarloGenerations.find(ref.characteristic);
		return current != document->get().monteCarloGenerations.end() && current->second == generation;
	}

	bool JFMWorkspaceStore::ApplyMonteCarloResult(JFMCharacteristicRef ref, std::uint64_t generation, JFMMonteCarloState state, JFMMonteCarloResult result)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto current = document->get().monteCarloGenerations.find(ref.characteristic);
		if (current == document->get().monteCarloGenerations.end() || current->second != generation)
			return false;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info || !info->get().fit || info->get().fit->modelId != state.modelId)
			return false;
		state.status = "completed";
		info->get().monteCarlo = std::move(state);
		document->get().monteCarloResults[ref.characteristic] = CreateRef<const JFMMonteCarloResult>(std::move(result));
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::FailMonteCarlo(JFMCharacteristicRef ref, std::uint64_t generation, std::string error)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(ref.project);
		if (!document)
			return false;
		const auto current = document->get().monteCarloGenerations.find(ref.characteristic);
		if (current == document->get().monteCarloGenerations.end() || current->second != generation)
			return false;
		auto info = findInfoLocked(document->get(), ref.characteristic);
		if (!info)
			return false;
		JFMMonteCarloState &state = info->get().monteCarlo.emplace(info->get().monteCarlo.value_or(JFMMonteCarloState{}));
		state.status = "failed";
		state.warnings = {std::move(error)};
		document->get().monteCarloResults.erase(ref.characteristic);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::ClearMonteCarloResults(ProjectHandle handle, const std::vector<CharacteristicId> &ids)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		bool changed = false;
		for (CharacteristicId id : ids)
		{
			auto info = findInfoLocked(document->get(), id);
			if (!info)
				continue;
			changed |= info->get().monteCarlo.has_value() || document->get().monteCarloResults.contains(id);
			info->get().monteCarlo.reset();
			document->get().monteCarloResults.erase(id);
			++document->get().monteCarloGenerations[id];
		}
		if (changed)
			bumpRevisionLocked(document->get());
		return changed;
	}

	JFMGroupingDefinition JFMWorkspaceStore::AddGrouping(ProjectHandle handle, JFMGroupingDefinition grouping)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return {};
		if (grouping.id.empty())
			grouping.id = nextId(document->get(), "grouping");
		if (grouping.name.empty())
			grouping.name = grouping.metadataKey.empty() ? "Grouping" : "By " + grouping.metadataKey;
		if (grouping.metadataKey.empty())
			grouping.metadataKey = "temperature_K";
		if (grouping.numericStep <= 0.0)
			grouping.numericStep = 1.0;
		document->get().project.groupings.push_back(grouping);
		bumpRevisionLocked(document->get());
		return grouping;
	}

	bool JFMWorkspaceStore::UpdateGrouping(ProjectHandle handle, JFMGroupingDefinition grouping)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document || grouping.id.empty())
			return false;
		const auto it = std::find_if(document->get().project.groupings.begin(), document->get().project.groupings.end(), [&grouping](const JFMGroupingDefinition &candidate) {
			return candidate.id == grouping.id;
		});
		if (it == document->get().project.groupings.end())
			return false;
		if (grouping.numericStep <= 0.0)
			grouping.numericStep = 1.0;
		*it = std::move(grouping);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::RenameGrouping(ProjectHandle handle, const std::string &groupingId, std::string name)
	{
		if (name.empty())
			return false;
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		const auto it = std::find_if(document->get().project.groupings.begin(), document->get().project.groupings.end(), [&groupingId](const JFMGroupingDefinition &grouping) {
			return grouping.id == groupingId;
		});
		if (it == document->get().project.groupings.end())
			return false;
		it->name = std::move(name);
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::DeleteGrouping(ProjectHandle handle, const std::string &groupingId)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto &groupings = document->get().project.groupings;
		const auto oldSize = groupings.size();
		groupings.erase(std::remove_if(groupings.begin(), groupings.end(), [&groupingId](const JFMGroupingDefinition &grouping) {
			return grouping.id == groupingId;
		}), groupings.end());
		if (groupings.size() == oldSize)
			return false;
		bumpRevisionLocked(document->get());
		return true;
	}

	std::vector<JFMGroupingBucket> JFMWorkspaceStore::ResolveGrouping(ProjectHandle handle, const std::string &groupingId) const
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return {};
		const ProjectDocument &document = documentRef->get();
		const auto groupingIt = std::find_if(document.project.groupings.begin(), document.project.groupings.end(), [&groupingId](const JFMGroupingDefinition &grouping) {
			return grouping.id == groupingId;
		});
		if (groupingIt == document.project.groupings.end())
			return {};
		return resolveGroupingLocked(document, *groupingIt);
	}

	std::vector<JFMGroupingBucket> JFMWorkspaceStore::ResolveGrouping(ProjectHandle handle, const JFMGroupingDefinition &grouping) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		return document ? resolveGroupingLocked(document->get(), grouping) : std::vector<JFMGroupingBucket>{};
	}

	std::vector<JFMGroupingBucket> JFMWorkspaceStore::resolveGroupingLocked(
		const ProjectDocument &document,
		const JFMGroupingDefinition &grouping)
	{
		std::set<std::string> sourceSeries(grouping.sourceSeriesIds.begin(), grouping.sourceSeriesIds.end());
		std::map<std::string, JFMGroupingBucket> exactBuckets;
		std::map<double, std::vector<CharacteristicId>> numericBuckets;
		std::vector<CharacteristicId> missingIds;
		for (const JFMSeries &series : document.project.series)
		{
			if (!sourceSeries.empty() && !sourceSeries.contains(series.id))
				continue;
			for (CharacteristicId id : series.characteristicIds)
			{
				auto info = findInfoLocked(document, id);
				const std::optional<std::string> value = info ? groupingValue(info->get(), grouping.metadataKey) : std::nullopt;
				if (!value)
				{
					if (grouping.includeMissing)
						missingIds.push_back(id);
					continue;
				}
				if (grouping.mode == JFMGroupingMode::NumericStep)
				{
					const std::optional<double> numeric = parseNumber(*value);
					if (!numeric)
					{
						if (grouping.includeMissing)
							missingIds.push_back(id);
						continue;
					}
					const double step = grouping.numericStep > 0.0 ? grouping.numericStep : 1.0;
					const double center = std::round(*numeric / step) * step;
					numericBuckets[center].push_back(id);
				}
				else
				{
					JFMGroupingBucket &bucket = exactBuckets[*value];
					bucket.key = "exact:" + *value;
					bucket.label = *value;
					bucket.characteristicIds.push_back(id);
				}
			}
		}

		std::vector<JFMGroupingBucket> buckets;
		buckets.reserve(exactBuckets.size() + numericBuckets.size() + (missingIds.empty() ? 0u : 1u));
		for (auto &[value, bucket] : exactBuckets)
			buckets.push_back(std::move(bucket));
		for (auto &[center, ids] : numericBuckets)
		{
			const std::string value = numberLabel(center);
			buckets.push_back({"numeric:" + value, grouping.metadataKey == "temperature_K" ? value + " K" : value, std::move(ids)});
		}
		if (!missingIds.empty())
			buckets.push_back({"missing", "Missing " + grouping.metadataKey, std::move(missingIds)});
		return buckets;
	}

	std::vector<std::string> JFMWorkspaceStore::GetAvailableGroupingKeys(ProjectHandle handle) const
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return {};
		std::set<std::string> keys{"temperature_K", "measurement", "parser", "temperature_source"};
		for (const JFMCharacteristicInfo &info : document->get().project.characteristics)
		{
			for (const auto &[key, value] : info.metadata)
				keys.insert(key);
		}
		return {keys.begin(), keys.end()};
	}

	JFMScriptInfo JFMWorkspaceStore::AddScript(Path path)
	{
		return AddScript(GetActiveProjectHandle(), std::move(path));
	}

	JFMScriptInfo JFMWorkspaceStore::AddScript(ProjectHandle handle, Path path)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return {};
		auto &scripts = document->get().project.scripts;
		if (auto existing = findScriptByPathLocked(document->get(), path))
			return existing->get();
		JFMScriptInfo script;
		script.id = nextId(document->get(), "script");
		script.path = std::move(path);
		script.name = script.path.filename().String();
		if (script.name.empty())
			script.name = script.id;
		scripts.push_back(script);
		document->get().status = "Script registered: " + script.name;
		bumpRevisionLocked(document->get());
		return script;
	}

	void JFMWorkspaceStore::EnsureDefaultScripts(Path scriptsDirectory)
	{
		if (scriptsDirectory.Empty())
			return;
		const Path paths[] = {
			scriptsDirectory / Path("dat_parser.py"),
			scriptsDirectory / Path("scaps_iv_parser.py")};
		for (const Path &path : paths)
			(void)AddScript(path);
	}

	bool JFMWorkspaceStore::RemoveScript(const std::string &scriptId)
	{
		return RemoveScript(GetActiveProjectHandle(), scriptId);
	}

	bool JFMWorkspaceStore::RemoveScript(ProjectHandle handle, const std::string &scriptId)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto &scripts = document->get().project.scripts;
		auto script = findScriptLocked(document->get(), scriptId);
		if (!script)
			return false;
		const auto index = static_cast<std::size_t>(&script->get() - scripts.data());
		scripts.erase(scripts.begin() + static_cast<std::ptrdiff_t>(index));
		document->get().status = "Script removed.";
		bumpRevisionLocked(document->get());
		return true;
	}

	bool JFMWorkspaceStore::ReloadScript(const std::string &scriptId)
	{
		return ReloadScript(GetActiveProjectHandle(), scriptId);
	}

	bool JFMWorkspaceStore::ReloadScript(ProjectHandle handle, const std::string &scriptId)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return false;
		auto script = findScriptLocked(document->get(), scriptId);
		if (!script)
			return false;
		++script->get().reloadCount;
		document->get().status = "Script reloaded: " + script->get().name;
		bumpRevisionLocked(document->get());
		return true;
	}

	void JFMWorkspaceStore::ResetProject(std::string name)
	{
		std::scoped_lock lock(m_Mutex);
		ProjectDocument &document = activeDocumentLocked();
		document.project = MakeDefaultProject(std::move(name));
		document.characteristicData.clear();
		document.prefitCurves.clear();
		document.prefitGenerations.clear();
		document.fitCurves.clear();
		document.fitGenerations.clear();
		document.monteCarloResults.clear();
		document.monteCarloGenerations.clear();
		document.arrheniusLayout.reset();
		document.nextCharacteristicId = 1;
		document.path.reset();
		document.lastImportWarnings.clear();
		document.importRunning = false;
		document.selectedCharacteristicIds.clear();
		document.status = "New JFM project created.";
		ensureSelectionLocked(document);
		bumpRevisionLocked(document);
	}

	void JFMWorkspaceStore::ReplaceProject(JFMProject project, std::optional<Path> path)
	{
		std::scoped_lock lock(m_Mutex);
		ProjectDocument &document = activeDocumentLocked();
		document.project = std::move(project);
		document.project.schemaVersion = JFMProjectSchemaVersion;
		document.characteristicData.clear();
		document.prefitCurves.clear();
		document.prefitGenerations.clear();
		document.fitCurves.clear();
		document.fitGenerations.clear();
		document.monteCarloResults.clear();
		document.monteCarloGenerations.clear();
		document.arrheniusLayout.reset();
		updateNextCharacteristicIdLocked(document);
		document.path = std::move(path);
		document.selectedCharacteristicIds.clear();
		document.status = "Project loaded.";
		document.revision = m_NextRevision++;
		document.savedRevision = document.revision;
		ensureSelectionLocked(document);
	}

	void JFMWorkspaceStore::SetProjectPath(Path path)
	{
		SetProjectPath(GetActiveProjectHandle(), std::move(path));
	}

	void JFMWorkspaceStore::SetProjectPath(ProjectHandle handle, Path path)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (document)
			document->get().path = std::move(path);
	}

	void JFMWorkspaceStore::MarkProjectSaved(ProjectHandle handle, Path path, std::uint64_t revision)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return;
		document->get().path = std::move(path);
		document->get().savedRevision = revision == 0 ? document->get().revision : revision;
	}

	void JFMWorkspaceStore::SetStatusMessage(std::string message)
	{
		SetStatusMessage(GetActiveProjectHandle(), std::move(message));
	}

	void JFMWorkspaceStore::SetStatusMessage(ProjectHandle handle, std::string message)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (document)
			document->get().status = std::move(message);
	}

	void JFMWorkspaceStore::SetImportRunning(bool running)
	{
		SetImportRunning(GetActiveProjectHandle(), running);
	}

	void JFMWorkspaceStore::SetImportRunning(ProjectHandle handle, bool running)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (document)
			document->get().importRunning = running;
	}

	void JFMWorkspaceStore::SetLastImportWarnings(std::vector<std::string> warnings)
	{
		SetLastImportWarnings(GetActiveProjectHandle(), std::move(warnings));
	}

	void JFMWorkspaceStore::SetLastImportWarnings(ProjectHandle handle, std::vector<std::string> warnings)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (document)
			document->get().lastImportWarnings = std::move(warnings);
	}

	void JFMWorkspaceStore::SelectSeries(std::string seriesId)
	{
		std::scoped_lock lock(m_Mutex);
		ProjectDocument &document = activeDocumentLocked();
		document.selectedSeriesId = std::move(seriesId);
		ensureSelectionLocked(document);
	}

	void JFMWorkspaceStore::SelectGroup(std::string groupId)
	{
		std::scoped_lock lock(m_Mutex);
		ProjectDocument &document = activeDocumentLocked();
		document.selectedGroupId = findGroupLocked(document, groupId) ? std::move(groupId) : std::string{};
	}

	void JFMWorkspaceStore::SelectCharacteristic(CharacteristicId characteristicId)
	{
		std::scoped_lock lock(m_Mutex);
		ProjectDocument &document = activeDocumentLocked();
		document.selectedCharacteristicId = characteristicId;
		document.selectedCharacteristicIds = characteristicId == InvalidCharacteristicId ? std::vector<CharacteristicId>{} : std::vector<CharacteristicId>{characteristicId};
		ensureSelectionLocked(document);
	}

	void JFMWorkspaceStore::SetSelectedCharacteristics(std::vector<CharacteristicId> characteristicIds, CharacteristicId primary, std::string seriesId)
	{
		SetSelectedCharacteristics(GetActiveProjectHandle(), std::move(characteristicIds), primary, std::move(seriesId));
	}

	void JFMWorkspaceStore::SetSelectedCharacteristics(ProjectHandle handle, std::vector<CharacteristicId> characteristicIds, CharacteristicId primary, std::string seriesId)
	{
		std::scoped_lock lock(m_Mutex);
		auto documentRef = findDocumentLocked(handle);
		if (!documentRef)
			return;
		ProjectDocument &document = documentRef->get();
		characteristicIds.erase(std::remove_if(characteristicIds.begin(), characteristicIds.end(), [&document](CharacteristicId id) {
			return !findInfoLocked(document, id).has_value();
		}), characteristicIds.end());
		std::sort(characteristicIds.begin(), characteristicIds.end());
		characteristicIds.erase(std::unique(characteristicIds.begin(), characteristicIds.end()), characteristicIds.end());
		document.selectedCharacteristicIds = std::move(characteristicIds);
		document.selectedCharacteristicId = findInfoLocked(document, primary) ? primary : (document.selectedCharacteristicIds.empty() ? InvalidCharacteristicId : document.selectedCharacteristicIds.back());
		if (!seriesId.empty() && findSeriesLocked(document, seriesId))
			document.selectedSeriesId = std::move(seriesId);
		ensureSelectionLocked(document);
	}

	void JFMWorkspaceStore::AddImportedCharacteristics(std::vector<JFMCharacteristic> characteristics)
	{
		(void)AddCharacteristics(std::move(characteristics), GetSelectedSeriesId());
	}

	std::size_t JFMWorkspaceStore::UpdateCharacteristicData(std::vector<JFMCharacteristic> characteristics)
	{
		return UpdateCharacteristicData(GetActiveProjectHandle(), std::move(characteristics));
	}

	std::size_t JFMWorkspaceStore::UpdateCharacteristicData(ProjectHandle handle, std::vector<JFMCharacteristic> characteristics)
	{
		std::vector<JFMCharacteristicRecord> records;
		records.reserve(characteristics.size());
		for (JFMCharacteristic &characteristic : characteristics)
			records.emplace_back(JFMCharacteristicRecord{
				std::move(characteristic.info),
				CreateRef<const JFMCharacteristicData>(std::move(characteristic.data))});
		return UpdateCharacteristicRecords(handle, std::move(records));
	}

	std::size_t JFMWorkspaceStore::UpdateCharacteristicRecords(ProjectHandle handle, std::vector<JFMCharacteristicRecord> characteristics)
	{
		std::scoped_lock lock(m_Mutex);
		auto document = findDocumentLocked(handle);
		if (!document)
			return 0;
		std::size_t updated = 0;
		for (JFMCharacteristicRecord &characteristic : characteristics)
		{
			if (characteristic.info.id == InvalidCharacteristicId || !findInfoLocked(document->get(), characteristic.info.id))
				continue;
			document->get().characteristicData[characteristic.info.id] = std::move(characteristic.data);
			++updated;
		}
		ensureSelectionLocked(document->get());
		return updated;
	}

	void JFMWorkspaceStore::SetFocusedTreeItem(JFMTreeItemRef item)
	{
		std::scoped_lock lock(m_Mutex);
		m_FocusedTreeItem = std::move(item);
	}

	JFMTreeItemRef JFMWorkspaceStore::GetFocusedTreeItem() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_FocusedTreeItem;
	}

	void JFMWorkspaceStore::SetTreeClipboard(JFMTreeItemRef item)
	{
		std::scoped_lock lock(m_Mutex);
		m_TreeClipboard = item.IsValid() ? std::optional<JFMTreeItemRef>{std::move(item)} : std::nullopt;
	}

	std::optional<JFMTreeItemRef> JFMWorkspaceStore::GetTreeClipboard() const
	{
		std::scoped_lock lock(m_Mutex);
		return m_TreeClipboard;
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

	JFMWorkspaceStore::OptionalRef<JFMWorkspaceStore::ProjectDocument> JFMWorkspaceStore::findDocumentLocked(ProjectHandle handle)
	{
		const auto it = std::find_if(m_Projects.begin(), m_Projects.end(), [handle](const ProjectDocument &document) { return document.handle == handle; });
		return it == m_Projects.end() ? std::nullopt : OptionalRef<ProjectDocument>{std::ref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<const JFMWorkspaceStore::ProjectDocument> JFMWorkspaceStore::findDocumentLocked(ProjectHandle handle) const
	{
		const auto it = std::find_if(m_Projects.begin(), m_Projects.end(), [handle](const ProjectDocument &document) { return document.handle == handle; });
		return it == m_Projects.end() ? std::nullopt : OptionalRef<const ProjectDocument>{std::cref(*it)};
	}

	JFMWorkspaceStore::ProjectDocument &JFMWorkspaceStore::activeDocumentLocked()
	{
		auto document = findDocumentLocked(m_ActiveProject);
		return document ? document->get() : m_Projects.front();
	}

	const JFMWorkspaceStore::ProjectDocument &JFMWorkspaceStore::activeDocumentLocked() const
	{
		auto document = findDocumentLocked(m_ActiveProject);
		return document ? document->get() : m_Projects.front();
	}

	std::string JFMWorkspaceStore::nextId(ProjectDocument &document, const char *prefix) const
	{
		std::string id;
		do
		{
			id = std::string(prefix) + "-" + std::to_string(document.nextId++);
		} while (
			std::any_of(document.project.groups.begin(), document.project.groups.end(), [&id](const JFMGroup &group) { return group.id == id; })
			|| std::any_of(document.project.series.begin(), document.project.series.end(), [&id](const JFMSeries &series) { return series.id == id; })
			|| std::any_of(document.project.scripts.begin(), document.project.scripts.end(), [&id](const JFMScriptInfo &script) { return script.id == id; })
			|| std::any_of(document.project.groupings.begin(), document.project.groupings.end(), [&id](const JFMGroupingDefinition &grouping) { return grouping.id == id; }));
		return id;
	}

	JFMWorkspaceStore::OptionalRef<JFMGroup> JFMWorkspaceStore::findGroupLocked(ProjectDocument &document, const std::string &groupId)
	{
		const auto it = std::find_if(document.project.groups.begin(), document.project.groups.end(), [&groupId](const JFMGroup &group) { return group.id == groupId; });
		return it == document.project.groups.end() ? std::nullopt : OptionalRef<JFMGroup>{std::ref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<const JFMGroup> JFMWorkspaceStore::findGroupLocked(const ProjectDocument &document, const std::string &groupId)
	{
		const auto it = std::find_if(document.project.groups.begin(), document.project.groups.end(), [&groupId](const JFMGroup &group) { return group.id == groupId; });
		return it == document.project.groups.end() ? std::nullopt : OptionalRef<const JFMGroup>{std::cref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<JFMGroup> JFMWorkspaceStore::findGroupByNameAndParentLocked(ProjectDocument &document, const std::string &name, const std::string &parentId)
	{
		const auto it = std::find_if(document.project.groups.begin(), document.project.groups.end(), [&name, &parentId](const JFMGroup &group) {
			return group.name == name && group.parentId == parentId;
		});
		return it == document.project.groups.end() ? std::nullopt : OptionalRef<JFMGroup>{std::ref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<JFMSeries> JFMWorkspaceStore::findSeriesLocked(ProjectDocument &document, const std::string &seriesId)
	{
		const auto it = std::find_if(document.project.series.begin(), document.project.series.end(), [&seriesId](const JFMSeries &series) { return series.id == seriesId; });
		return it == document.project.series.end() ? std::nullopt : OptionalRef<JFMSeries>{std::ref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<const JFMSeries> JFMWorkspaceStore::findSeriesLocked(const ProjectDocument &document, const std::string &seriesId)
	{
		const auto it = std::find_if(document.project.series.begin(), document.project.series.end(), [&seriesId](const JFMSeries &series) { return series.id == seriesId; });
		return it == document.project.series.end() ? std::nullopt : OptionalRef<const JFMSeries>{std::cref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<JFMCharacteristicInfo> JFMWorkspaceStore::findInfoLocked(ProjectDocument &document, const CharacteristicId &id)
	{
		const auto it = std::find_if(document.project.characteristics.begin(), document.project.characteristics.end(), [id](const JFMCharacteristicInfo &info) { return info.id == id; });
		return it == document.project.characteristics.end() ? std::nullopt : OptionalRef<JFMCharacteristicInfo>{std::ref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<const JFMCharacteristicInfo> JFMWorkspaceStore::findInfoLocked(const ProjectDocument &document, const CharacteristicId &id)
	{
		const auto it = std::find_if(document.project.characteristics.begin(), document.project.characteristics.end(), [id](const JFMCharacteristicInfo &info) { return info.id == id; });
		return it == document.project.characteristics.end() ? std::nullopt : OptionalRef<const JFMCharacteristicInfo>{std::cref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<JFMScriptInfo> JFMWorkspaceStore::findScriptLocked(ProjectDocument &document, const std::string &scriptId)
	{
		const auto it = std::find_if(document.project.scripts.begin(), document.project.scripts.end(), [&scriptId](const JFMScriptInfo &script) { return script.id == scriptId; });
		return it == document.project.scripts.end() ? std::nullopt : OptionalRef<JFMScriptInfo>{std::ref(*it)};
	}

	JFMWorkspaceStore::OptionalRef<JFMScriptInfo> JFMWorkspaceStore::findScriptByPathLocked(ProjectDocument &document, const Path &path)
	{
		const auto it = std::find_if(document.project.scripts.begin(), document.project.scripts.end(), [&path](const JFMScriptInfo &script) { return script.path == path; });
		return it == document.project.scripts.end() ? std::nullopt : OptionalRef<JFMScriptInfo>{std::ref(*it)};
	}

	std::optional<JFMCharacteristic> JFMWorkspaceStore::makeCharacteristicLocked(const ProjectDocument &document, const JFMCharacteristicInfo &info)
	{
		const auto record = makeCharacteristicRecordLocked(document, info);
		JFMCharacteristic characteristic{record->info, {}};
		if (record->data)
			characteristic.data = *record->data;
		return characteristic;
	}

	std::optional<JFMCharacteristicRecord> JFMWorkspaceStore::makeCharacteristicRecordLocked(const ProjectDocument &document, const JFMCharacteristicInfo &info)
	{
		const auto data = document.characteristicData.find(info.id);
		return JFMCharacteristicRecord{info, data == document.characteristicData.end() ? nullptr : data->second};
	}

	std::vector<JFMCharacteristicRecord> JFMWorkspaceStore::seriesCharacteristicRecordsLocked(const ProjectDocument &document, const JFMSeries &series)
	{
		std::vector<JFMCharacteristicRecord> characteristics;
		characteristics.reserve(series.characteristicIds.size());
		for (CharacteristicId id : series.characteristicIds)
		{
			auto info = findInfoLocked(document, id);
			if (info)
				characteristics.emplace_back(*makeCharacteristicRecordLocked(document, info->get()));
		}
		return characteristics;
	}

	std::vector<JFMCharacteristic> JFMWorkspaceStore::seriesCharacteristicsLocked(const ProjectDocument &document, const JFMSeries &series)
	{
		std::vector<JFMCharacteristic> characteristics;
		characteristics.reserve(series.characteristicIds.size());
		for (CharacteristicId id : series.characteristicIds)
		{
			auto info = findInfoLocked(document, id);
			if (info)
				characteristics.push_back(*makeCharacteristicLocked(document, info->get()));
		}
		return characteristics;
	}

	void JFMWorkspaceStore::collectGroupIdsLocked(const ProjectDocument &document, const std::string &groupId, std::vector<std::string> &groupIds)
	{
		groupIds.push_back(groupId);
		for (const JFMGroup &group : document.project.groups)
		{
			if (group.parentId == groupId)
				collectGroupIdsLocked(document, group.id, groupIds);
		}
	}

	void JFMWorkspaceStore::ensureSelectionLocked(ProjectDocument &document)
	{
		if (document.project.series.empty())
			document.project.series.push_back(JFMSeries{"series-default", "Series 1", {}, {}});
		if (!findSeriesLocked(document, document.selectedSeriesId))
			document.selectedSeriesId = document.project.series.front().id;
		document.selectedCharacteristicIds.erase(std::remove_if(document.selectedCharacteristicIds.begin(), document.selectedCharacteristicIds.end(), [&document](CharacteristicId id) {
			return !findInfoLocked(document, id).has_value();
		}), document.selectedCharacteristicIds.end());
		if (!findInfoLocked(document, document.selectedCharacteristicId))
		{
			document.selectedCharacteristicId = document.selectedCharacteristicIds.empty() ? InvalidCharacteristicId : document.selectedCharacteristicIds.back();
		}
		if (document.selectedCharacteristicId == InvalidCharacteristicId)
		{
			for (const JFMSeries &series : document.project.series)
			{
				if (!series.characteristicIds.empty())
				{
					document.selectedSeriesId = series.id;
					document.selectedCharacteristicId = series.characteristicIds.front();
					document.selectedCharacteristicIds = {document.selectedCharacteristicId};
					break;
				}
			}
		}
	}

	void JFMWorkspaceStore::removeCharacteristicReferencesLocked(ProjectDocument &document, const CharacteristicId &id)
	{
		for (JFMSeries &series : document.project.series)
			series.characteristicIds.erase(std::remove(series.characteristicIds.begin(), series.characteristicIds.end(), id), series.characteristicIds.end());
	}

	void JFMWorkspaceStore::updateNextCharacteristicIdLocked(ProjectDocument &document)
	{
		document.nextCharacteristicId = 1;
		for (const JFMCharacteristicInfo &info : document.project.characteristics)
			document.nextCharacteristicId = std::max(document.nextCharacteristicId, info.id + 1);
	}

	void JFMWorkspaceStore::bumpRevisionLocked(ProjectDocument &document)
	{
		document.revision = m_NextRevision++;
	}

	std::optional<std::string> JFMWorkspaceStore::groupingValue(const JFMCharacteristicInfo &info, const std::string &key)
	{
		if (key == "temperature_K")
			return info.temperatureK ? std::optional<std::string>{numberLabel(*info.temperatureK)} : std::nullopt;
		if (key == "measurement")
			return info.measurement.empty() ? std::nullopt : std::optional<std::string>{info.measurement};
		if (key == "parser")
			return info.parser.empty() ? std::nullopt : std::optional<std::string>{info.parser};
		if (key == "temperature_source")
			return info.temperatureSource.empty() ? std::nullopt : std::optional<std::string>{info.temperatureSource};
		const auto value = info.metadata.find(key);
		return value == info.metadata.end() || value->second.empty() ? std::nullopt : std::optional<std::string>{value->second};
	}
} // namespace SST::Prototypes::JFM
