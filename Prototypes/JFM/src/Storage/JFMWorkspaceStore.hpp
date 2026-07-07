#pragma once

#include <mutex>
#include <optional>
#include <string>
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
		[[nodiscard]] std::string GetSelectedCharacteristicId() const;
		[[nodiscard]] std::vector<std::string> GetLastImportWarnings() const;

		void ResetProject(std::string name = "Untitled JFM Project");
		void ReplaceProject(JFMProject project, std::optional<Path> path);
		void SetProjectPath(Path path);
		void SetStatusMessage(std::string message);
		void SetImportRunning(bool running);
		void SetLastImportWarnings(std::vector<std::string> warnings);
		void SelectSeries(std::string seriesId);
		void SelectCharacteristic(std::string characteristicId);
		void AddImportedCharacteristics(std::vector<JFMCharacteristic> characteristics);

	private:
		[[nodiscard]] static JFMProject MakeDefaultProject(std::string name);
		[[nodiscard]] std::string nextId(const char *prefix) const;
		void ensureSelectionLocked();

	private:
		mutable std::mutex m_Mutex;
		JFMProject m_Project;
		std::optional<Path> m_ProjectPath;
		std::string m_SelectedSeriesId;
		std::string m_SelectedCharacteristicId;
		std::string m_StatusMessage = "Ready.";
		std::vector<std::string> m_LastImportWarnings;
		bool m_ImportRunning = false;
		mutable std::size_t m_NextId = 1;
	};
} // namespace SST::Prototypes::JFM
