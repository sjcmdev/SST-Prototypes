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

	struct JFMImportPreviewItem
	{
		Path path;
		std::string relativePath;
		std::string folderKey;
		std::string folderLabel;
		std::string text;
		std::vector<JFMCharacteristicRecord> characteristics;
		std::string warning;
	};

	struct JFMImportPreviewPayload
	{
		std::vector<JFMImportPreviewItem> items;
	};

	struct ImportPreviewRequested final : public BusEvent
	{
		ImportPreviewRequested() = default;
		ImportPreviewRequested(std::uint64_t requestedGeneration, std::vector<Path> requestedPaths, bool requestedRecursive, JFMImportOptions requestedOptions)
			: generation(requestedGeneration), paths(std::move(requestedPaths)), recursive(requestedRecursive), options(std::move(requestedOptions))
		{
		}

		std::uint64_t generation = 0;
		std::vector<Path> paths;
		bool recursive = false;
		JFMImportOptions options;
	};

	struct ImportPreviewCompleted final : public BusEvent
	{
		ImportPreviewCompleted() = default;
		ImportPreviewCompleted(std::uint64_t completedGeneration, Ref<const JFMImportPreviewPayload> completedPayload)
			: generation(completedGeneration), payload(std::move(completedPayload))
		{
		}

		std::uint64_t generation = 0;
		Ref<const JFMImportPreviewPayload> payload;
	};

	struct ImportPreviewFailed final : public BusEvent
	{
		ImportPreviewFailed() = default;
		ImportPreviewFailed(std::uint64_t failedGeneration, std::string failure)
			: generation(failedGeneration), error(std::move(failure))
		{
		}

		std::uint64_t generation = 0;
		std::string error;
	};

	struct JFMPlotCurveGroupRequest
	{
		std::string key;
		std::string title;
		std::vector<CharacteristicId> ids;
		std::string groupingId;
		std::string bucketKey;
	};

	struct PlotRequested final : public BusEvent
	{
		PlotRequested() = default;
		PlotRequested(ProjectHandle requestedProject, std::string requestedTitle, std::vector<JFMPlotCurveGroupRequest> requestedGroups)
			: project(requestedProject), title(std::move(requestedTitle)), groups(std::move(requestedGroups))
		{
		}

		ProjectHandle project = InvalidProjectHandle;
		std::string title;
		std::vector<JFMPlotCurveGroupRequest> groups;
	};

	struct ProjectTreeRenameRequested final : public BusEvent
	{
		ProjectTreeRenameRequested() = default;
		explicit ProjectTreeRenameRequested(JFMTreeItemRef requestedItem)
			: item(std::move(requestedItem))
		{
		}

		JFMTreeItemRef item;
	};

	struct ProjectTreePasteRequested final : public BusEvent
	{
		ProjectTreePasteRequested() = default;
		ProjectTreePasteRequested(JFMTreeItemRef requestedTarget, bool duplicateItem)
			: target(std::move(requestedTarget)), duplicate(duplicateItem)
		{
		}

		JFMTreeItemRef target;
		bool duplicate = false;
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
			std::vector<JFMCharacteristicRecord> importedCharacteristics,
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

		std::vector<JFMCharacteristicRecord> characteristics;
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
		ExportRawTsvRequested(Path requestedPath, JFMCharacteristicRecord selectedCharacteristic, ProjectHandle requestedProject = InvalidProjectHandle, JFMOperationId requestedOperation = InvalidJFMOperationId)
			: path(std::move(requestedPath)), characteristic(std::move(selectedCharacteristic)), project(requestedProject), operation(requestedOperation)
		{
		}

		Path path;
		JFMCharacteristicRecord characteristic;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ExportRawTsvCompleted final : public BusEvent
	{
		ExportRawTsvCompleted() = default;
		explicit ExportRawTsvCompleted(Path exportedPath, ProjectHandle exportedProject = InvalidProjectHandle, JFMOperationId completedOperation = InvalidJFMOperationId)
			: path(std::move(exportedPath)), project(exportedProject), operation(completedOperation)
		{
		}

		Path path;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ExportRawTsvFailed final : public BusEvent
	{
		ExportRawTsvFailed() = default;
		ExportRawTsvFailed(Path failedPath, std::string failure, ProjectHandle failedProject = InvalidProjectHandle, JFMOperationId failedOperation = InvalidJFMOperationId)
			: path(std::move(failedPath)), error(std::move(failure)), project(failedProject), operation(failedOperation)
		{
		}

		Path path;
		std::string error;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct JFMExportItem
	{
		Path relativePath;
		JFMCharacteristicRecord characteristic;
	};

	struct ExportRawTsvBatchRequested final : public BusEvent
	{
		ExportRawTsvBatchRequested() = default;
		ExportRawTsvBatchRequested(Path requestedDirectory, std::vector<JFMExportItem> requestedItems, ProjectHandle requestedProject = InvalidProjectHandle, JFMOperationId requestedOperation = InvalidJFMOperationId)
			: outputDirectory(std::move(requestedDirectory)), items(std::move(requestedItems)), project(requestedProject), operation(requestedOperation)
		{
		}

		Path outputDirectory;
		std::vector<JFMExportItem> items;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ExportRawTsvBatchCompleted final : public BusEvent
	{
		ExportRawTsvBatchCompleted() = default;
		ExportRawTsvBatchCompleted(Path requestedDirectory, std::size_t exportedCount, std::vector<std::string> exportWarnings, ProjectHandle exportedProject = InvalidProjectHandle, JFMOperationId completedOperation = InvalidJFMOperationId)
			: outputDirectory(std::move(requestedDirectory)), count(exportedCount), warnings(std::move(exportWarnings)), project(exportedProject), operation(completedOperation)
		{
		}

		Path outputDirectory;
		std::size_t count = 0;
		std::vector<std::string> warnings;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ExportRawTsvBatchFailed final : public BusEvent
	{
		ExportRawTsvBatchFailed() = default;
		ExportRawTsvBatchFailed(Path requestedDirectory, std::string failure, ProjectHandle failedProject = InvalidProjectHandle, JFMOperationId failedOperation = InvalidJFMOperationId)
			: outputDirectory(std::move(requestedDirectory)), error(std::move(failure)), project(failedProject), operation(failedOperation)
		{
		}

		Path outputDirectory;
		std::string error;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};

	struct ProjectDataMaterializationCompleted final : public BusEvent
	{
		ProjectDataMaterializationCompleted() = default;
		ProjectDataMaterializationCompleted(
			std::vector<JFMCharacteristicRecord> materializedCharacteristics,
			std::vector<std::string> materializationWarnings,
			ProjectHandle materializedProject = InvalidProjectHandle,
			JFMOperationId materializationOperation = InvalidJFMOperationId)
			: characteristics(std::move(materializedCharacteristics)),
			  warnings(std::move(materializationWarnings)),
			  project(materializedProject),
			  operation(materializationOperation)
		{
		}

		std::vector<JFMCharacteristicRecord> characteristics;
		std::vector<std::string> warnings;
		ProjectHandle project = InvalidProjectHandle;
		JFMOperationId operation = InvalidJFMOperationId;
	};
} // namespace SST::Prototypes::JFM
