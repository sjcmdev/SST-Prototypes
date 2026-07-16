#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <mutex>
#include <optional>
#include <vector>

namespace SST::Prototypes::JFM
{
	struct JFMPrefitWorkItem
	{
		JFMCharacteristicRef ref;
		std::uint64_t generation = 0;
		JFMCharacteristicRecord characteristic;
	};

	class JFMPrefitWorkQueue
	{
	public:
		[[nodiscard]] bool Enqueue(std::vector<JFMPrefitWorkItem> items);
		[[nodiscard]] std::optional<std::vector<JFMPrefitWorkItem>> TakeNext();
		void Reset();

	private:
		std::mutex m_Mutex;
		std::vector<JFMPrefitWorkItem> m_Pending;
		bool m_JobScheduled = false;
	};

	class JFMPrefitJob final : public IJob
	{
	public:
		JFMPrefitJob(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Path pythonRoot, Ref<JFMPrefitWorkQueue> queue);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		[[nodiscard]] bool UsesPythonRuntime() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Ref<JFMWorkspaceStore> m_Store;
		Path m_PythonRoot;
		Ref<JFMPrefitWorkQueue> m_Queue;
	};
}
