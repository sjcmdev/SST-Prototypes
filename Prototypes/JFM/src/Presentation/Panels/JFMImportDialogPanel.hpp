#pragma once

#include <array>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Presentation/Panels/IPanel.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	struct JFMImportDialogRequest
	{
		std::vector<Path> paths;
		std::string targetSeriesId;
		bool recursive = false;
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
		JFMImportDialogPanel(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMImportDialogState> state);

		[[nodiscard]] Ref<IPanel> Clone() const override;
		void Render() override;

	private:
		void loadRequest(JFMImportDialogRequest request);
		void refreshPreviewFiles();
		void renderPreviewWindow();
		[[nodiscard]] JFMImportOptions buildOptions() const;
		[[nodiscard]] std::string previewName() const;
		[[nodiscard]] std::string previewFileText() const;
		[[nodiscard]] Path selectedPreviewPath() const;
		[[nodiscard]] bool buildPreviewCurve(std::vector<double> &x, std::vector<double> &y) const;

	private:
		Ref<EventBus> m_EventBus;
		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMImportDialogState> m_State;
		JFMImportDialogRequest m_Request;
		bool m_Open = false;
		bool m_PreviewOpen = false;
		int m_Parser = 0;
		int m_DecimalSeparator = 0;
		int m_TemperatureExtraction = 0;
		int m_PreviewFileIndex = 0;
		std::vector<Path> m_PreviewFiles;
		std::array<char, 160> m_NamePattern = {};
		std::array<char, 32> m_Delimiter = {};
		std::array<char, 320> m_CustomScriptPath = {};
	};
} // namespace SST::Prototypes::JFM
