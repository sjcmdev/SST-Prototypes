#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMProjectDocumentInfo
	{
		ProjectHandle handle = InvalidProjectHandle;
		std::string name;
		std::optional<Path> path;
		std::string status;
		bool active = false;
		bool dirty = false;
		bool importRunning = false;
	};

	struct JFMProjectStateSnapshot
	{
		JFMProject project;
		std::unordered_map<CharacteristicId, Ref<const JFMCharacteristicData>> characteristicData;
		std::string selectedSeriesId;
		CharacteristicId selectedCharacteristicId = InvalidCharacteristicId;
		std::vector<CharacteristicId> selectedCharacteristicIds;
		std::uint64_t revision = 0;
	};

	class JFMWorkspaceStore
	{
	public:
		JFMWorkspaceStore();

		[[nodiscard]] std::vector<JFMProjectDocumentInfo> GetProjects() const;
		[[nodiscard]] ProjectHandle GetActiveProjectHandle() const;
		[[nodiscard]] bool SetActiveProject(ProjectHandle handle);
		[[nodiscard]] ProjectHandle CreateProject(std::string name = "Untitled JFM Project");
		[[nodiscard]] ProjectHandle CreateTransientProject(std::string name = "Transient JFM Project");
		[[nodiscard]] ProjectHandle AttachProject(JFMProject project, std::optional<Path> path, bool activate = true);
		[[nodiscard]] ProjectHandle AttachProjectBundle(JFMProjectBundleSnapshot snapshot, std::optional<Path> path, bool activate = true);
		[[nodiscard]] std::optional<ProjectHandle> FindProjectByPath(const Path &path) const;

		[[nodiscard]] JFMProject GetProjectSnapshot() const;
		[[nodiscard]] JFMProject GetProjectSnapshot(ProjectHandle handle) const;
		[[nodiscard]] std::optional<Path> GetProjectPath() const;
		[[nodiscard]] std::optional<Path> GetProjectPath(ProjectHandle handle) const;
		[[nodiscard]] std::string GetStatusMessage() const;
		[[nodiscard]] std::string GetStatusMessage(ProjectHandle handle) const;
		[[nodiscard]] bool IsImportRunning() const;
		[[nodiscard]] bool IsImportRunning(ProjectHandle handle) const;
		[[nodiscard]] bool IsDirty(ProjectHandle handle) const;
		[[nodiscard]] std::uint64_t GetProjectRevision(ProjectHandle handle) const;
		[[nodiscard]] std::optional<JFMProjectStateSnapshot> CaptureProjectState(ProjectHandle handle) const;
		[[nodiscard]] std::optional<JFMProjectBundleSnapshot> CaptureProjectBundle(ProjectHandle handle) const;
		bool RestoreProjectState(ProjectHandle handle, JFMProjectStateSnapshot snapshot, bool activate = true);
		[[nodiscard]] std::optional<JFMArrheniusLayoutDocument> GetArrheniusLayout(ProjectHandle handle) const;
		void SetArrheniusLayout(ProjectHandle handle, std::optional<JFMArrheniusLayoutDocument> layout);

		[[nodiscard]] std::optional<JFMCharacteristic> GetSelectedCharacteristic() const;
		[[nodiscard]] std::vector<JFMCharacteristic> GetSelectedCharacteristics() const;
		[[nodiscard]] std::optional<JFMCharacteristicRecord> GetSelectedCharacteristicRecord() const;
		[[nodiscard]] std::vector<JFMCharacteristicRecord> GetSelectedCharacteristicRecords() const;
		[[nodiscard]] std::string GetSelectedSeriesId() const;
		[[nodiscard]] std::string GetSelectedGroupId() const;
		[[nodiscard]] CharacteristicId GetSelectedCharacteristicId() const;
		[[nodiscard]] std::vector<CharacteristicId> GetSelectedCharacteristicIds() const;
		[[nodiscard]] std::vector<std::string> GetLastImportWarnings() const;
		[[nodiscard]] std::optional<JFMCharacteristic> FindCharacteristic(const CharacteristicId &id) const;
		[[nodiscard]] std::optional<JFMCharacteristic> FindCharacteristic(JFMCharacteristicRef ref) const;
		[[nodiscard]] std::optional<JFMCharacteristicRecord> FindCharacteristicRecord(JFMCharacteristicRef ref) const;
		[[nodiscard]] std::optional<JFMCharacteristic> FindCharacteristicByName(const std::string &name) const;
		[[nodiscard]] std::vector<JFMCharacteristic> GetSeriesCharacteristics(const std::string &seriesId) const;
		[[nodiscard]] std::vector<JFMCharacteristic> GetSeriesCharacteristics(ProjectHandle handle, const std::string &seriesId) const;
		[[nodiscard]] std::vector<JFMCharacteristicRecord> GetSeriesCharacteristicRecords(ProjectHandle handle, const std::string &seriesId) const;
		[[nodiscard]] std::vector<JFMCharacteristic> GetGroupCharacteristics(const std::string &groupId) const;
		[[nodiscard]] std::vector<JFMCharacteristic> GetGroupCharacteristics(ProjectHandle handle, const std::string &groupId) const;
		[[nodiscard]] std::vector<CharacteristicId> GetGroupCharacteristicIds(ProjectHandle handle, const std::string &groupId) const;
		[[nodiscard]] std::optional<JFMPrefitState> GetPrefitState(JFMCharacteristicRef ref) const;
		[[nodiscard]] Ref<const JFMPrefitCurve> GetPrefitCurve(JFMCharacteristicRef ref) const;
		[[nodiscard]] std::optional<JFMFitState> GetFitState(JFMCharacteristicRef ref) const;
		[[nodiscard]] Ref<const JFMFitCurve> GetFitCurve(JFMCharacteristicRef ref) const;
		[[nodiscard]] std::optional<JFMMonteCarloState> GetMonteCarloState(JFMCharacteristicRef ref) const;
		[[nodiscard]] Ref<const JFMMonteCarloResult> GetMonteCarloResult(JFMCharacteristicRef ref) const;

		JFMGroup AddGroup(std::string name, std::string parentId = {});
		JFMGroup AddGroup(ProjectHandle handle, std::string name, std::string parentId = {});
		std::string FindOrCreateGroupPath(const std::vector<std::string> &names);
		std::string FindOrCreateGroupPath(ProjectHandle handle, const std::vector<std::string> &names, std::string parentId = {});
		JFMSeries AddSeries(std::string name);
		JFMSeries AddSeries(std::string name, std::string groupId);
		JFMSeries AddSeries(ProjectHandle handle, std::string name, std::string groupId = {});
		std::vector<CharacteristicId> AddCharacteristics(std::vector<JFMCharacteristic> characteristics, std::string targetSeriesId = {});
		std::vector<CharacteristicId> AddCharacteristics(ProjectHandle handle, std::vector<JFMCharacteristic> characteristics, std::string targetSeriesId = {});
		std::vector<CharacteristicId> AddCharacteristicRecords(ProjectHandle handle, std::vector<JFMCharacteristicRecord> characteristics, std::string targetSeriesId = {});
		bool DeleteCharacteristic(const CharacteristicId &id);
		bool DeleteCharacteristic(ProjectHandle handle, const CharacteristicId &id);
		bool DeleteSeries(const std::string &seriesId);
		bool DeleteSeries(ProjectHandle handle, const std::string &seriesId);
		bool DeleteGroup(const std::string &groupId);
		bool DeleteGroup(ProjectHandle handle, const std::string &groupId);
		bool MoveCharacteristic(const CharacteristicId &id, const std::string &targetSeriesId);
		bool MoveCharacteristic(ProjectHandle handle, const CharacteristicId &id, const std::string &targetSeriesId);
		bool RenameProject(std::string name);
		bool RenameProject(ProjectHandle handle, std::string name);
		bool RenameGroup(const std::string &groupId, std::string name);
		bool RenameGroup(ProjectHandle handle, const std::string &groupId, std::string name);
		bool RenameSeries(const std::string &seriesId, std::string name);
		bool RenameSeries(ProjectHandle handle, const std::string &seriesId, std::string name);
		bool RenameCharacteristic(const CharacteristicId &id, std::string name);
		bool RenameCharacteristic(ProjectHandle handle, const CharacteristicId &id, std::string name);
		bool SetGroupShortName(const std::string &groupId, std::string shortName);
		bool SetGroupShortName(ProjectHandle handle, const std::string &groupId, std::string shortName);
		bool SetSeriesShortName(const std::string &seriesId, std::string shortName);
		bool SetSeriesShortName(ProjectHandle handle, const std::string &seriesId, std::string shortName);
		bool SetCharacteristicShortName(const CharacteristicId &id, std::string shortName);
		bool SetCharacteristicShortName(ProjectHandle handle, const CharacteristicId &id, std::string shortName);
		bool SetCharacteristicPlotColor(ProjectHandle handle, const std::vector<CharacteristicId> &ids, std::string color);
		bool SetCharacteristicMetadata(ProjectHandle handle, const std::vector<CharacteristicId> &ids, std::string key, std::string value);
		bool SetPrefitModel(ProjectHandle handle, const std::vector<CharacteristicId> &ids, const std::string &modelId);
		bool SetManualFitRange(ProjectHandle handle, CharacteristicId id, double lowerVoltage, double upperVoltage);
		bool ResetFitRange(ProjectHandle handle, CharacteristicId id);
		bool SetAutoModelSelection(ProjectHandle handle, const std::vector<CharacteristicId> &ids, bool enabled);
		[[nodiscard]] std::uint64_t BeginPrefit(JFMCharacteristicRef ref);
		[[nodiscard]] bool IsPrefitGenerationCurrent(JFMCharacteristicRef ref, std::uint64_t generation) const;
		bool ApplyPrefitResult(JFMCharacteristicRef ref, std::uint64_t generation, JFMPrefitState result, JFMPrefitCurve curve);
		bool FailPrefit(JFMCharacteristicRef ref, std::uint64_t generation, std::string error);
		[[nodiscard]] std::uint64_t BeginFit(JFMCharacteristicRef ref);
		[[nodiscard]] bool IsFitGenerationCurrent(JFMCharacteristicRef ref, std::uint64_t generation) const;
		bool ApplyFitResult(JFMCharacteristicRef ref, std::uint64_t generation, JFMFitState result, JFMFitCurve curve);
		bool FailFit(JFMCharacteristicRef ref, std::uint64_t generation, std::string error);
		[[nodiscard]] std::uint64_t BeginMonteCarlo(JFMCharacteristicRef ref);
		[[nodiscard]] bool IsMonteCarloGenerationCurrent(JFMCharacteristicRef ref, std::uint64_t generation) const;
		bool ApplyMonteCarloResult(JFMCharacteristicRef ref, std::uint64_t generation, JFMMonteCarloState state, JFMMonteCarloResult result);
		bool FailMonteCarlo(JFMCharacteristicRef ref, std::uint64_t generation, std::string error);
		bool ClearMonteCarloResults(ProjectHandle handle, const std::vector<CharacteristicId> &ids);

		JFMGroupingDefinition AddGrouping(ProjectHandle handle, JFMGroupingDefinition grouping);
		bool UpdateGrouping(ProjectHandle handle, JFMGroupingDefinition grouping);
		bool RenameGrouping(ProjectHandle handle, const std::string &groupingId, std::string name);
		bool DeleteGrouping(ProjectHandle handle, const std::string &groupingId);
		[[nodiscard]] std::vector<JFMGroupingBucket> ResolveGrouping(ProjectHandle handle, const std::string &groupingId) const;
		[[nodiscard]] std::vector<JFMGroupingBucket> ResolveGrouping(ProjectHandle handle, const JFMGroupingDefinition &grouping) const;
		[[nodiscard]] std::vector<std::string> GetAvailableGroupingKeys(ProjectHandle handle) const;

		JFMScriptInfo AddScript(Path path);
		JFMScriptInfo AddScript(ProjectHandle handle, Path path);
		void EnsureDefaultScripts(Path scriptsDirectory);
		bool RemoveScript(const std::string &scriptId);
		bool RemoveScript(ProjectHandle handle, const std::string &scriptId);
		bool ReloadScript(const std::string &scriptId);
		bool ReloadScript(ProjectHandle handle, const std::string &scriptId);

		void ResetProject(std::string name = "Untitled JFM Project");
		void ReplaceProject(JFMProject project, std::optional<Path> path);
		void SetProjectPath(Path path);
		void SetProjectPath(ProjectHandle handle, Path path);
		void MarkProjectSaved(ProjectHandle handle, Path path, std::uint64_t revision = 0);
		void SetStatusMessage(std::string message);
		void SetStatusMessage(ProjectHandle handle, std::string message);
		void SetImportRunning(bool running);
		void SetImportRunning(ProjectHandle handle, bool running);
		void SetLastImportWarnings(std::vector<std::string> warnings);
		void SetLastImportWarnings(ProjectHandle handle, std::vector<std::string> warnings);
		void SelectSeries(std::string seriesId);
		void SelectGroup(std::string groupId);
		void SelectCharacteristic(CharacteristicId characteristicId);
		void SetSelectedCharacteristics(std::vector<CharacteristicId> characteristicIds, CharacteristicId primary, std::string seriesId = {});
		void SetSelectedCharacteristics(ProjectHandle handle, std::vector<CharacteristicId> characteristicIds, CharacteristicId primary, std::string seriesId = {});
		void AddImportedCharacteristics(std::vector<JFMCharacteristic> characteristics);
		std::size_t UpdateCharacteristicData(std::vector<JFMCharacteristic> characteristics);
		std::size_t UpdateCharacteristicData(ProjectHandle handle, std::vector<JFMCharacteristic> characteristics);
		std::size_t UpdateCharacteristicRecords(ProjectHandle handle, std::vector<JFMCharacteristicRecord> characteristics);
		void SetFocusedTreeItem(JFMTreeItemRef item);
		[[nodiscard]] JFMTreeItemRef GetFocusedTreeItem() const;
		void SetTreeClipboard(JFMTreeItemRef item);
		[[nodiscard]] std::optional<JFMTreeItemRef> GetTreeClipboard() const;

	private:
		template <typename T>
		using OptionalRef = std::optional<std::reference_wrapper<T>>;

		struct ProjectDocument
		{
			ProjectHandle handle = InvalidProjectHandle;
			JFMProject project;
			std::unordered_map<CharacteristicId, Ref<const JFMCharacteristicData>> characteristicData;
			std::unordered_map<CharacteristicId, Ref<const JFMPrefitCurve>> prefitCurves;
			std::unordered_map<CharacteristicId, std::uint64_t> prefitGenerations;
			std::unordered_map<CharacteristicId, Ref<const JFMFitCurve>> fitCurves;
			std::unordered_map<CharacteristicId, std::uint64_t> fitGenerations;
			std::unordered_map<CharacteristicId, Ref<const JFMMonteCarloResult>> monteCarloResults;
			std::unordered_map<CharacteristicId, std::uint64_t> monteCarloGenerations;
			std::optional<JFMArrheniusLayoutDocument> arrheniusLayout;
			std::optional<Path> path;
			std::string selectedSeriesId;
			std::string selectedGroupId;
			CharacteristicId selectedCharacteristicId = InvalidCharacteristicId;
			std::vector<CharacteristicId> selectedCharacteristicIds;
			std::string status = "Ready.";
			std::vector<std::string> lastImportWarnings;
			bool importRunning = false;
			bool hidden = false;
			std::size_t nextId = 1;
			CharacteristicId nextCharacteristicId = 1;
			std::uint64_t revision = 1;
			std::uint64_t savedRevision = 1;
		};

		[[nodiscard]] static JFMProject MakeDefaultProject(std::string name);
		[[nodiscard]] OptionalRef<ProjectDocument> findDocumentLocked(ProjectHandle handle);
		[[nodiscard]] OptionalRef<const ProjectDocument> findDocumentLocked(ProjectHandle handle) const;
		[[nodiscard]] ProjectDocument &activeDocumentLocked();
		[[nodiscard]] const ProjectDocument &activeDocumentLocked() const;
		[[nodiscard]] std::string nextId(ProjectDocument &document, const char *prefix) const;
		[[nodiscard]] static OptionalRef<JFMGroup> findGroupLocked(ProjectDocument &document, const std::string &groupId);
		[[nodiscard]] static OptionalRef<const JFMGroup> findGroupLocked(const ProjectDocument &document, const std::string &groupId);
		[[nodiscard]] static OptionalRef<JFMGroup> findGroupByNameAndParentLocked(ProjectDocument &document, const std::string &name, const std::string &parentId);
		[[nodiscard]] static OptionalRef<JFMSeries> findSeriesLocked(ProjectDocument &document, const std::string &seriesId);
		[[nodiscard]] static OptionalRef<const JFMSeries> findSeriesLocked(const ProjectDocument &document, const std::string &seriesId);
		[[nodiscard]] static OptionalRef<JFMCharacteristicInfo> findInfoLocked(ProjectDocument &document, const CharacteristicId &id);
		[[nodiscard]] static OptionalRef<const JFMCharacteristicInfo> findInfoLocked(const ProjectDocument &document, const CharacteristicId &id);
		[[nodiscard]] static OptionalRef<JFMScriptInfo> findScriptLocked(ProjectDocument &document, const std::string &scriptId);
		[[nodiscard]] static OptionalRef<JFMScriptInfo> findScriptByPathLocked(ProjectDocument &document, const Path &path);
		[[nodiscard]] static std::optional<JFMCharacteristicRecord> makeCharacteristicRecordLocked(const ProjectDocument &document, const JFMCharacteristicInfo &info);
		[[nodiscard]] static std::optional<JFMCharacteristic> makeCharacteristicLocked(const ProjectDocument &document, const JFMCharacteristicInfo &info);
		[[nodiscard]] static std::vector<JFMCharacteristicRecord> seriesCharacteristicRecordsLocked(const ProjectDocument &document, const JFMSeries &series);
		[[nodiscard]] static std::vector<JFMCharacteristic> seriesCharacteristicsLocked(const ProjectDocument &document, const JFMSeries &series);
		static void collectGroupIdsLocked(const ProjectDocument &document, const std::string &groupId, std::vector<std::string> &groupIds);
		static void ensureSelectionLocked(ProjectDocument &document);
		static void removeCharacteristicReferencesLocked(ProjectDocument &document, const CharacteristicId &id);
		static void updateNextCharacteristicIdLocked(ProjectDocument &document);
		void bumpRevisionLocked(ProjectDocument &document);
		[[nodiscard]] static std::optional<std::string> groupingValue(const JFMCharacteristicInfo &info, const std::string &key);
		[[nodiscard]] static std::vector<JFMGroupingBucket> resolveGroupingLocked(
			const ProjectDocument &document,
			const JFMGroupingDefinition &grouping);

	private:
		mutable std::mutex m_Mutex;
		std::vector<ProjectDocument> m_Projects;
		ProjectHandle m_ActiveProject = InvalidProjectHandle;
		ProjectHandle m_NextProjectHandle = 1;
		std::uint64_t m_NextRevision = 2;
		JFMTreeItemRef m_FocusedTreeItem;
		std::optional<JFMTreeItemRef> m_TreeClipboard;
	};
} // namespace SST::Prototypes::JFM
