#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	class JFMWorkspaceStore
	{
	public:
		JFMWorkspaceStore();

		[[nodiscard]] JFMProject GetProjectSnapshot() const;
		[[nodiscard]] std::optional<Path> GetProjectPath() const;
		[[nodiscard]] std::string GetStatusMessage() const;
		[[nodiscard]] bool IsImportRunning() const;
		[[nodiscard]] std::optional<JFMCharacteristic> GetSelectedCharacteristic() const;
		[[nodiscard]] std::string GetSelectedSeriesId() const;
		[[nodiscard]] CharacteristicId GetSelectedCharacteristicId() const;
		[[nodiscard]] std::vector<std::string> GetLastImportWarnings() const;
		[[nodiscard]] std::optional<JFMCharacteristic> FindCharacteristic(const CharacteristicId &id) const;
		[[nodiscard]] std::optional<JFMCharacteristic> FindCharacteristicByName(const std::string &name) const;

		JFMGroup AddGroup(std::string name, std::string parentId = {});
		std::string FindOrCreateGroupPath(const std::vector<std::string> &names);
		JFMSeries AddSeries(std::string name);
		JFMSeries AddSeries(std::string name, std::string groupId);
		std::vector<CharacteristicId> AddCharacteristics(std::vector<JFMCharacteristic> characteristics, std::string targetSeriesId = {});
		bool DeleteCharacteristic(const CharacteristicId &id);
		bool MoveCharacteristic(const CharacteristicId &id, const std::string &targetSeriesId);

		void ResetProject(std::string name = "Untitled JFM Project");
		void ReplaceProject(JFMProject project, std::optional<Path> path);
		void SetProjectPath(Path path);
		void SetStatusMessage(std::string message);
		void SetImportRunning(bool running);
		void SetLastImportWarnings(std::vector<std::string> warnings);
		void SelectSeries(std::string seriesId);
		void SelectCharacteristic(CharacteristicId characteristicId);
		void AddImportedCharacteristics(std::vector<JFMCharacteristic> characteristics);
		std::size_t UpdateCharacteristicData(std::vector<JFMCharacteristic> characteristics);

	private:
		template <typename T>
		using OptionalRef = std::optional<std::reference_wrapper<T>>;

		[[nodiscard]] static JFMProject MakeDefaultProject(std::string name);
		[[nodiscard]] std::string nextId(const char *prefix) const;
		[[nodiscard]] OptionalRef<JFMGroup> findGroupLocked(const std::string &groupId);
		[[nodiscard]] OptionalRef<const JFMGroup> findGroupLocked(const std::string &groupId) const;
		[[nodiscard]] OptionalRef<JFMGroup> findGroupByNameAndParentLocked(const std::string &name, const std::string &parentId);
		[[nodiscard]] OptionalRef<JFMSeries> findSeriesLocked(const std::string &seriesId);
		[[nodiscard]] OptionalRef<const JFMSeries> findSeriesLocked(const std::string &seriesId) const;
		[[nodiscard]] OptionalRef<JFMCharacteristicInfo> findInfoLocked(const CharacteristicId &id);
		[[nodiscard]] OptionalRef<const JFMCharacteristicInfo> findInfoLocked(const CharacteristicId &id) const;
		[[nodiscard]] std::optional<JFMCharacteristic> makeCharacteristicLocked(const JFMCharacteristicInfo &info) const;
		void ensureSelectionLocked();
		void removeCharacteristicReferencesLocked(const CharacteristicId &id);
		void updateNextCharacteristicIdLocked();

	private:
		mutable std::mutex m_Mutex;
		JFMProject m_Project;
		std::unordered_map<CharacteristicId, JFMCharacteristicData> m_CharacteristicData;
		std::optional<Path> m_ProjectPath;
		std::string m_SelectedSeriesId;
		CharacteristicId m_SelectedCharacteristicId;
		std::string m_StatusMessage = "Ready.";
		std::vector<std::string> m_LastImportWarnings;
		bool m_ImportRunning = false;
		mutable std::size_t m_NextId = 1;
		CharacteristicId m_NextCharacteristicId = 1;
	};
} // namespace SST::Prototypes::JFM
