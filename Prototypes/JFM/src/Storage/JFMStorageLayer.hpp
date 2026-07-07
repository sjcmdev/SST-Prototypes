#pragma once

#include "Core/EventSystem/BusEventSystem/EventReceiver.hpp"
#include "Core/Layer.hpp"
#include "Core/Utils/Memory.hpp"
#include "Storage/JFMWorkspaceStore.hpp"

namespace SST::Prototypes::JFM
{
	struct ProjectLoadCompleted;
	struct ProjectLoadFailed;
	struct ProjectLoadRequested;
	struct ProjectSaveCompleted;
	struct ProjectSaveFailed;
	struct ProjectSaveRequested;

	class JFMStorageLayer final : public Layer, public EventReceiver
	{
	public:
		explicit JFMStorageLayer(Ref<JFMWorkspaceStore> store);

		void OnAttach() override;
		void OnDetach() override;

	private:
		void onProjectSaveRequested(const ProjectSaveRequested &event);
		void onProjectSaveCompleted(const ProjectSaveCompleted &event);
		void onProjectSaveFailed(const ProjectSaveFailed &event);
		void onProjectLoadRequested(const ProjectLoadRequested &event);
		void onProjectLoadCompleted(const ProjectLoadCompleted &event);
		void onProjectLoadFailed(const ProjectLoadFailed &event);

	private:
		Ref<JFMWorkspaceStore> m_Store;
	};
} // namespace SST::Prototypes::JFM
