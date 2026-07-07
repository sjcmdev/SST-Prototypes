#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Core/EventSystem/BusEventSystem/Event.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	struct ProjectSaveRequested final : public BusEvent
	{
		ProjectSaveRequested() = default;
		ProjectSaveRequested(Path requestedPath, JFMProject requestedSnapshot)
			: path(std::move(requestedPath)), snapshot(std::move(requestedSnapshot))
		{
		}

		Path path;
		JFMProject snapshot;
	};

	struct ProjectSaveCompleted final : public BusEvent
	{
		ProjectSaveCompleted() = default;
		explicit ProjectSaveCompleted(Path savedPath)
			: path(std::move(savedPath))
		{
		}

		Path path;
	};

	struct ProjectSaveFailed final : public BusEvent
	{
		ProjectSaveFailed() = default;
		ProjectSaveFailed(Path failedPath, std::string failure)
			: path(std::move(failedPath)), error(std::move(failure))
		{
		}

		Path path;
		std::string error;
	};

	struct ProjectLoadRequested final : public BusEvent
	{
		ProjectLoadRequested() = default;
		explicit ProjectLoadRequested(Path requestedPath)
			: path(std::move(requestedPath))
		{
		}

		Path path;
	};

	struct ProjectLoadCompleted final : public BusEvent
	{
		ProjectLoadCompleted() = default;
		ProjectLoadCompleted(Path loadedPath, JFMProject loadedProject)
			: path(std::move(loadedPath)), project(std::move(loadedProject))
		{
		}

		Path path;
		JFMProject project;
	};

	struct ProjectLoadFailed final : public BusEvent
	{
		ProjectLoadFailed() = default;
		ProjectLoadFailed(Path failedPath, std::string failure)
			: path(std::move(failedPath)), error(std::move(failure))
		{
		}

		Path path;
		std::string error;
	};

	struct ImportRequested final : public BusEvent
	{
		ImportRequested() = default;
		ImportRequested(std::vector<Path> requestedPaths, std::string requestedTargetSeriesId)
			: paths(std::move(requestedPaths)), targetSeriesId(std::move(requestedTargetSeriesId))
		{
		}

		std::vector<Path> paths;
		std::string targetSeriesId;
	};

	struct ImportProgress final : public BusEvent
	{
		ImportProgress() = default;
		ImportProgress(float completed, float total, std::string progressMessage)
			: completedWork(completed), totalWork(total), message(std::move(progressMessage))
		{
		}

		float completedWork = 0.0f;
		float totalWork = 0.0f;
		std::string message;
	};

	struct ImportCompleted final : public BusEvent
	{
		ImportCompleted() = default;
		ImportCompleted(std::vector<JFMCharacteristic> importedCharacteristics, std::vector<std::string> importWarnings)
			: characteristics(std::move(importedCharacteristics)), warnings(std::move(importWarnings))
		{
		}

		std::vector<JFMCharacteristic> characteristics;
		std::vector<std::string> warnings;
	};

	struct ImportFailed final : public BusEvent
	{
		ImportFailed() = default;
		explicit ImportFailed(std::string failure)
			: error(std::move(failure))
		{
		}

		std::string error;
	};

	struct ExportRawTsvRequested final : public BusEvent
	{
		ExportRawTsvRequested() = default;
		ExportRawTsvRequested(Path requestedPath, JFMCharacteristic selectedCharacteristic)
			: path(std::move(requestedPath)), characteristic(std::move(selectedCharacteristic))
		{
		}

		Path path;
		JFMCharacteristic characteristic;
	};

	struct ExportRawTsvCompleted final : public BusEvent
	{
		ExportRawTsvCompleted() = default;
		explicit ExportRawTsvCompleted(Path exportedPath)
			: path(std::move(exportedPath))
		{
		}

		Path path;
	};

	struct ExportRawTsvFailed final : public BusEvent
	{
		ExportRawTsvFailed() = default;
		ExportRawTsvFailed(Path failedPath, std::string failure)
			: path(std::move(failedPath)), error(std::move(failure))
		{
		}

		Path path;
		std::string error;
	};
} // namespace SST::Prototypes::JFM
