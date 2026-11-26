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

    class JFMMonteCarloSaveJob final : public IJob {
    public:
        JFMMonteCarloSaveJob(Ref<EventBus> bus, JFMCharacteristicRef ref, std::uint64_t generation,
            Path directory, Ref<const JFMMonteCarloResult> result, bool tsv = false, double noisePercent = 0.0);
        ~JFMMonteCarloSaveJob() override;
        std::string GetName() const override { return m_Tsv ? "Export MC samples TSV" : "Save MC samples HDF5"; }
        std::string GetType() const override { return m_Tsv ? "jfm.mc.export" : "jfm.mc.save"; }
        void Execute(JobContext& context) override;
    private:
        void Finish(std::string status, std::string error = {});
        Ref<EventBus> m_Bus;
        JFMCharacteristicRef m_Ref;
        std::uint64_t m_Generation;
        Path m_Directory;
        Ref<const JFMMonteCarloResult> m_Result;
        bool m_Tsv, m_Finished = false;
        double m_NoisePercent;
    };

	class JFMMonteCarloJob final : public IJob
	{
	public:
		JFMMonteCarloJob(Ref<EventBus> eventBus, Ref<JFMWorkspaceStore> store, Ref<JFMMonteCarloWorkQueue> queue);
		~JFMMonteCarloJob() override;

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Ref<JFMWorkspaceStore> m_Store;
		Ref<JFMMonteCarloWorkQueue> m_Queue;
		bool m_Executed = false;
	};
} // namespace SST::Prototypes::JFM
