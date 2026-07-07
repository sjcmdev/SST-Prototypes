#pragma once

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Storage/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	struct ImportCompleted;
	struct ImportFailed;
	struct ImportProgress;
	struct ImportRequested;
	struct ExportRawTsvCompleted;
	struct ExportRawTsvFailed;
	struct ExportRawTsvRequested;

	class JFMIOLayer final : public Layer, public EventReceiver
	{
	public:
		explicit JFMIOLayer(Ref<JFMWorkspaceStore> store);

		void OnAttach() override;
		void OnDetach() override;

	private:
		void onImportRequested(const ImportRequested &event);
		void onImportProgress(const ImportProgress &event);
		void onImportCompleted(const ImportCompleted &event);
		void onImportFailed(const ImportFailed &event);
		void onExportRawTsvRequested(const ExportRawTsvRequested &event);
		void onExportRawTsvCompleted(const ExportRawTsvCompleted &event);
		void onExportRawTsvFailed(const ExportRawTsvFailed &event);

	private:
		Ref<JFMWorkspaceStore> m_Store;
		std::string m_TargetSeriesId;
		bool m_ImportRunning = false;
	};
} // namespace SST::Prototypes::JFM
