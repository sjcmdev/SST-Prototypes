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
		ProjectSaveRequested(
			Path requestedPath,
			JFMProject requestedSnapshot,
			ProjectHandle requestedProject = InvalidProjectHandle,
			JFMOperationId requestedOperation = InvalidJFMOperationId,
			std::uint64_t requestedRevision = 0)
			: path(std::move(requestedPath)), snapshot(std::move(requestedSnapshot)), project(requestedProject), operation(requestedOperation), revision(requestedRevision)
		{
		}

		Path path;
		JFMProject snapshot;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
		std::uint64_t revision = 0;
	};

	struct ProjectSaveCompleted final : public BusEvent
	{
		ProjectSaveCompleted() = default;
		explicit ProjectSaveCompleted(
			Path savedPath,
			ProjectHandle savedProject = InvalidProjectHandle,
			JFMOperationId completedOperation = InvalidJFMOperationId,
			std::uint64_t savedRevision = 0)
			: path(std::move(savedPath)), project(savedProject), operation(completedOperation), revision(savedRevision)
		{
		}

		Path path;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
		std::uint64_t revision = 0;
	};

	struct ProjectSaveFailed final : public BusEvent
	{
		ProjectSaveFailed() = default;
		ProjectSaveFailed(
			Path failedPath,
			std::string failure,
			ProjectHandle failedProject = InvalidProjectHandle,
			JFMOperationId failedOperation = InvalidJFMOperationId)
			: path(std::move(failedPath)), error(std::move(failure)), project(failedProject), operation(failedOperation)
		{
		}

		Path path;
		std::string error;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ProjectLoadRequested final : public BusEvent
	{
		ProjectLoadRequested() = default;
		explicit ProjectLoadRequested(Path requestedPath, JFMOperationId requestedOperation = InvalidJFMOperationId)
			: path(std::move(requestedPath)), operation(requestedOperation)
		{
		}

		Path path;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ProjectLoadCompleted final : public BusEvent
	{
		ProjectLoadCompleted() = default;
		ProjectLoadCompleted(Path loadedPath, JFMProject loadedProject, JFMOperationId completedOperation = InvalidJFMOperationId)
			: path(std::move(loadedPath)), project(std::move(loadedProject)), operation(completedOperation)
		{
		}

		Path path;
		JFMProject project;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ProjectLoadFailed final : public BusEvent
	{
		ProjectLoadFailed() = default;
		ProjectLoadFailed(Path failedPath, std::string failure, JFMOperationId failedOperation = InvalidJFMOperationId)
			: path(std::move(failedPath)), error(std::move(failure)), operation(failedOperation)
		{
		}

		Path path;
		std::string error;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ImportRequested final : public BusEvent
	{
		ImportRequested() = default;
		ImportRequested(
			std::vector<Path> requestedPaths,
			std::string requestedTargetSeriesId,
			bool requestedRecursive = false,
			JFMImportOptions requestedOptions = {},
			bool requestedPlotAfterImport = false,
			ProjectHandle requestedProject = InvalidProjectHandle,
			JFMOperationId requestedOperation = InvalidJFMOperationId,
			std::string requestedTargetGroupId = {},
			JFMImportPlacement requestedPlacement = JFMImportPlacement::NewSeries)
			: paths(std::move(requestedPaths)),
			  targetSeriesId(std::move(requestedTargetSeriesId)),
			  recursive(requestedRecursive),
			  options(std::move(requestedOptions)),
			  plotAfterImport(requestedPlotAfterImport),
			  project(requestedProject),
			  operation(requestedOperation),
			  targetGroupId(std::move(requestedTargetGroupId)),
			  placement(requestedPlacement)
		{
		}

		std::vector<Path> paths;
		std::string targetSeriesId;
		bool recursive = false;
		JFMImportOptions options;
		bool plotAfterImport = false;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
		std::string targetGroupId;
		JFMImportPlacement placement = JFMImportPlacement::NewSeries;
	};

	struct PlotCharacteristicsRequested final : public BusEvent
	{
		PlotCharacteristicsRequested() = default;
		PlotCharacteristicsRequested(std::vector<CharacteristicId> requestedIds, std::string requestedTitle, ProjectHandle requestedProject = InvalidProjectHandle)
			: ids(std::move(requestedIds)), title(std::move(requestedTitle)), project(requestedProject)
		{
		}

		std::vector<CharacteristicId> ids;
		std::string title;
		ProjectHandle project = InvalidProjectHandle;
	};

	struct ImportProgress final : public BusEvent
	{
		ImportProgress() = default;
		ImportProgress(float completed, float total, std::string progressMessage, ProjectHandle targetProject = InvalidProjectHandle, JFMOperationId targetOperation = InvalidJFMOperationId)
			: completedWork(completed), totalWork(total), message(std::move(progressMessage)), project(targetProject), operation(targetOperation)
		{
		}

		float completedWork = 0.0f;
		float totalWork = 0.0f;
		std::string message;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ImportCompleted final : public BusEvent
	{
		ImportCompleted() = default;
		ImportCompleted(
			std::vector<JFMCharacteristic> importedCharacteristics,
			std::vector<std::string> importWarnings,
			std::string completedTargetSeriesId = {},
			std::vector<std::string> completedGroupPath = {},
			std::string completedSeriesName = {},
			bool completedFinalBatch = true,
			ProjectHandle completedProject = InvalidProjectHandle,
			JFMOperationId completedOperation = InvalidJFMOperationId)
			: characteristics(std::move(importedCharacteristics)),
				warnings(std::move(importWarnings)),
				targetSeriesId(std::move(completedTargetSeriesId)),
				groupPath(std::move(completedGroupPath)),
				seriesName(std::move(completedSeriesName)),
				finalBatch(completedFinalBatch),
				project(completedProject),
				operation(completedOperation)
		{
		}

		std::vector<JFMCharacteristic> characteristics;
		std::vector<std::string> warnings;
		std::string targetSeriesId;
		std::vector<std::string> groupPath;
		std::string seriesName;
		bool finalBatch = true;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ImportFailed final : public BusEvent
	{
		ImportFailed() = default;
		explicit ImportFailed(std::string failure, ProjectHandle failedProject = InvalidProjectHandle, JFMOperationId failedOperation = InvalidJFMOperationId)
			: error(std::move(failure)), project(failedProject), operation(failedOperation)
		{
		}

		std::string error;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
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

	struct JFMExportItem
	{
		Path relativePath;
		JFMCharacteristic characteristic;
	};

	struct ExportRawTsvBatchRequested final : public BusEvent
	{
		ExportRawTsvBatchRequested() = default;
		ExportRawTsvBatchRequested(Path requestedDirectory, std::vector<JFMExportItem> requestedItems)
			: outputDirectory(std::move(requestedDirectory)), items(std::move(requestedItems))
		{
		}

		Path outputDirectory;
		std::vector<JFMExportItem> items;
	};

	struct ExportRawTsvBatchCompleted final : public BusEvent
	{
		ExportRawTsvBatchCompleted() = default;
		ExportRawTsvBatchCompleted(Path requestedDirectory, std::size_t exportedCount, std::vector<std::string> exportWarnings)
			: outputDirectory(std::move(requestedDirectory)), count(exportedCount), warnings(std::move(exportWarnings))
		{
		}

		Path outputDirectory;
		std::size_t count = 0;
		std::vector<std::string> warnings;
	};

	struct ExportRawTsvBatchFailed final : public BusEvent
	{
		ExportRawTsvBatchFailed() = default;
		ExportRawTsvBatchFailed(Path requestedDirectory, std::string failure)
			: outputDirectory(std::move(requestedDirectory)), error(std::move(failure))
		{
		}

		Path outputDirectory;
		std::string error;
	};

	struct ProjectDataMaterializationCompleted final : public BusEvent
	{
		ProjectDataMaterializationCompleted() = default;
		ProjectDataMaterializationCompleted(
			std::vector<JFMCharacteristic> materializedCharacteristics,
			std::vector<std::string> materializationWarnings,
			ProjectHandle materializedProject = InvalidProjectHandle,
			JFMOperationId materializationOperation = InvalidJFMOperationId)
			: characteristics(std::move(materializedCharacteristics)),
			  warnings(std::move(materializationWarnings)),
			  project(materializedProject),
			  operation(materializationOperation)
		{
		}

		std::vector<JFMCharacteristic> characteristics;
		std::vector<std::string> warnings;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};
} // namespace SST::Prototypes::JFM
