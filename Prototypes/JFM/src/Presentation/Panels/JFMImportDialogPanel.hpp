#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "App/JFMUserSettings.hpp"
#include "Events/JFMEvents.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST
{
	class CommandRegistry;
	class UndoStack;
}

namespace SST::Prototypes::JFM
{
	struct ImportPreviewCompleted;
	struct ImportPreviewFailed;
	struct ImportCompleted;
	struct ImportFailed;
	struct JFMImportPreviewItem;
	struct JFMImportPreviewPayload;

	struct JFMImportDialogRequest
	{
		std::vector<Path> paths;
		std::string targetSeriesId;
		bool recursive = false;
		ProjectHandle project = InvalidProjectHandle;
		std::string targetGroupId;
		JFMImportPlacement placement = JFMImportPlacement::NewSeries;
	};

	class JFMImportDialogState
	{
	public:
		void Open(JFMImportDialogRequest request);
		[[nodiscard]] std::optional<JFMImportDialogRequest> ConsumeOpenRequest();

	private:
		std::mutex m_Mutex;
		std::optional<JFMImportDialogRequest> m_Request;
	};

	class JFMImportDialogPanel final : public IPanel, public EventReceiver
	{
	public:
		JFMImportDialogPanel(
			Ref<EventBus> eventBus,
			Ref<JFMWorkspaceStore> store,
			Ref<JFMImportDialogState> state,
			Ref<JFMUserSettingsStore> settings,
			WeakRef<CommandRegistry> commandRegistry,
			WeakRef<UndoStack> undoStack);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		struct VariableRuleDraft
		{
			std::array<char, 32> name = {};
			int source = 0;
			int tokenIndex = -1;
			int substringStart = 0;
			int substringLength = -1;
		};

		struct StagedSeries
		{
			std::string targetSeriesId;
			std::vector<std::string> groupPath;
			std::string seriesName;
			std::vector<CharacteristicId> characteristicIds;
		};

		void loadRequest(JFMImportDialogRequest request);
		void requestPreview();
		void handleImportCompleted(const ImportCompleted &event);
		void handleImportFailed(const ImportFailed &event);
		void handlePreviewCompleted(const ImportPreviewCompleted &event);
		void handlePreviewFailed(const ImportPreviewFailed &event);
		void resetStaging();
		void buildStagingProject();
		[[nodiscard]] std::vector<ImportCompleted> buildApplyBatchesFromStaging() const;
		void refreshPreviewFiles();
		void applyPreviewFolderFilter();
		void renderCustomVariables();
		void renderVariablePreviewTable() const;
		void renderPreviewPlot(const char *plotId, float plotHeight) const;
		void renderPreviewWindow();
		void renderAnalysisTab();
		void renderAnalysisTree(const JFMProject &project);
		void renderAnalysisGroup(const JFMProject &project, const JFMGroup &group, const std::vector<CharacteristicId> &visibleIds);
		void renderAnalysisSeries(const JFMSeries &series, const std::vector<CharacteristicId> &visibleIds);
		void selectAnalysisIds(std::vector<CharacteristicId> ids, CharacteristicId primary, std::string seriesId = {});
		void selectAnalysisCharacteristic(const JFMSeries &series, CharacteristicId id, const std::vector<CharacteristicId> &visibleIds);
		void renderAnalysisPlot(ProjectHandle project, const std::vector<CharacteristicId> &ids, CharacteristicId active);
		void renderPrefitParameters(const std::optional<JFMPrefitState> &prefit) const;
		void requestPrefit(ProjectHandle project, std::vector<CharacteristicId> ids) const;
		void finishRangeUndoGroup(bool cancel = false);
		[[nodiscard]] JFMImportOptions buildOptions() const;
		[[nodiscard]] std::vector<JFMNameVariableRule> buildVariableRules() const;
		[[nodiscard]] std::string previewName() const;
		[[nodiscard]] std::string previewShortName() const;
		[[nodiscard]] Path selectedPreviewPath() const;
		[[nodiscard]] std::vector<Path> selectedPreviewPaths() const;
		[[nodiscard]] const JFMImportPreviewItem *previewItem(const Path &path) const;
		[[nodiscard]] std::string previewOptionsKey() const;

	private:
		Ref<EventBus> m_EventBus;
		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMImportDialogState> m_State;
		Ref<JFMUserSettingsStore> m_Settings;
		WeakRef<CommandRegistry> m_CommandRegistry;
		WeakRef<UndoStack> m_UndoStack;
		JFMImportDialogRequest m_Request;
		bool m_Open = false;
		bool m_PreviewOpen = false;
		bool m_PlotAfterImport = false;
		bool m_ImportSubmitted = false;
		bool m_SelectImportTab = false;
		bool m_SelectAnalysisTab = false;
		bool m_ShowOnlyActive = false;
		bool m_DataAddedToProject = false;
		bool m_AnalysisAutoFit = true;
		bool m_AnalysisShowLegend = false;
		bool m_AnalysisShowCurves = true;
		bool m_AnalysisShowPrefitCurves = true;
		int m_AnalysisScale = 0;
		int m_AnalysisTreeOpenRequest = 0;
		float m_AnalysisFitPadding = 0.08f;
		std::string m_AnalysisOnlySeriesId;
		JFMOperationId m_NextImportOperation = 1;
		JFMOperationId m_ImportOperation = InvalidJFMOperationId;
		ProjectHandle m_StagingProject = InvalidProjectHandle;
		CharacteristicId m_RangeCharacteristic = InvalidCharacteristicId;
		double m_RangeLower = 0.0;
		double m_RangeUpper = 0.0;
		bool m_RangeInitialized = false;
		bool m_RangeUndoGroupOpen = false;
		bool m_RangeHeldLastFrame = false;
		int m_Parser = 0;
		int m_DecimalSeparator = 0;
		int m_TemperatureExtraction = 0;
		int m_PreviewFileIndex = 0;
		int m_PreviewScale = 0;
		int m_PreviewFolderIndex = 0;
		std::uint64_t m_PreviewGeneration = 0;
		bool m_PreviewLoading = false;
		std::string m_PreviewError;
		std::string m_LastPreviewOptionsKey;
		Ref<const JFMImportPreviewPayload> m_PreviewPayload;
		std::vector<ImportCompleted> m_StagedImportBatches;
		std::vector<StagedSeries> m_StagedSeries;
		std::vector<Path> m_AllPreviewFiles;
		std::vector<Path> m_PreviewFiles;
		std::vector<std::string> m_PreviewFolders;
		std::vector<std::string> m_PreviewFolderLabels;
		std::vector<int> m_SelectedPreviewFileIndices;
		std::vector<VariableRuleDraft> m_CustomVariables;
		std::array<char, 160> m_NamePattern = {};
		std::array<char, 160> m_ShortNamePattern = {};
		std::array<char, 32> m_VariableDelimiter = {};
		std::array<char, 32> m_Delimiter = {};
		std::array<char, 320> m_CustomScriptPath = {};
	};
} // namespace SST::Prototypes::JFM
