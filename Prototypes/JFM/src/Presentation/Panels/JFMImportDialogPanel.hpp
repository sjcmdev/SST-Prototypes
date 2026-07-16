#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "App/JFMUserSettings.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
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

	class JFMImportDialogPanel final : public IPanel
	{
	public:
		JFMImportDialogPanel(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMImportDialogState> state, Ref<JFMUserSettingsStore> settings);

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

		void loadRequest(JFMImportDialogRequest request);
		void refreshPreviewFiles();
		void applyPreviewFolderFilter();
		void renderCustomVariables();
		void renderVariablePreviewTable() const;
		void renderPreviewWindow();
		[[nodiscard]] JFMImportOptions buildOptions() const;
		[[nodiscard]] std::vector<JFMNameVariableRule> buildVariableRules() const;
		[[nodiscard]] std::string previewName() const;
		[[nodiscard]] std::string previewShortName() const;
		[[nodiscard]] std::string previewFileText() const;
		[[nodiscard]] Path selectedPreviewPath() const;
		[[nodiscard]] std::vector<Path> selectedPreviewPaths() const;
		[[nodiscard]] bool buildPreviewCurve(const Path &path, std::vector<double> &x, std::vector<double> &y) const;
		[[nodiscard]] Path importRoot() const;
		[[nodiscard]] std::string relativeLabel(const Path &path) const;

	private:
		Ref<EventBus> m_EventBus;
		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMImportDialogState> m_State;
		Ref<JFMUserSettingsStore> m_Settings;
		JFMImportDialogRequest m_Request;
		bool m_Open = false;
		bool m_PreviewOpen = false;
		bool m_PlotAfterImport = false;
		int m_Parser = 0;
		int m_DecimalSeparator = 0;
		int m_TemperatureExtraction = 0;
		int m_PreviewFileIndex = 0;
		int m_PreviewScale = 0;
		int m_PreviewFolderIndex = 0;
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
