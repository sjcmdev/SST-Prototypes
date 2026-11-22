#pragma once

#include "App/JFMUserSettings.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"
#include "Workspace/JFMWorkspaceStore.hpp"

#include <mutex>
#include <optional>
#include <vector>

namespace SST { class CommandRegistry; class JobSystem; }

namespace SST::Prototypes::JFM
{
    void ConfigureJFMFitServices(WeakRef<CommandRegistry> commands, WeakRef<JobSystem> jobs);
	struct JFMFitWorkItem
	{
		JFMCharacteristicRef ref;
		std::uint64_t generation = 0;
		JFMCharacteristicRecord characteristic;
		JFMFitMonteCarloSettings settings;
		bool useTunedStart = false;
	};

	class JFMFitWorkQueue
	{
	public:
		[[nodiscard]] bool Enqueue(std::vector<JFMFitWorkItem> items);
		[[nodiscard]] std::optional<std::vector<JFMFitWorkItem>> TakeNext();
		void Reset();

	private:
		std::mutex m_Mutex;
		std::vector<JFMFitWorkItem> m_Pending;
		bool m_JobScheduled = false;
	};

	class JFMFitJob final : public IJob
	{
	public:
		JFMFitJob(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMFitWorkQueue> queue);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMFitWorkQueue> m_Queue;
	};

	struct JFMMonteCarloWorkItem
	{
		JFMCharacteristicRef ref;
		std::uint64_t generation = 0;
		JFMCharacteristicRecord characteristic;
		JFMPrefitState prefit;
		JFMFitState fit;
		JFMFitMonteCarloSettings settings;
		Path outputRoot;
	};

	class JFMMonteCarloWorkQueue
	{
	public:
		[[nodiscard]] bool Enqueue(std::vector<JFMMonteCarloWorkItem> items);
		[[nodiscard]] std::optional<std::vector<JFMMonteCarloWorkItem>> TakeNext();
		void Reset();

	private:
		std::mutex m_Mutex;
		std::vector<JFMMonteCarloWorkItem> m_Pending;
		bool m_JobScheduled = false;
	};

	class JFMMonteCarloJob final : public IJob
	{
	public:
		JFMMonteCarloJob(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMMonteCarloWorkQueue> queue);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMMonteCarloWorkQueue> m_Queue;
	};
} // namespace SST::Prototypes::JFM
