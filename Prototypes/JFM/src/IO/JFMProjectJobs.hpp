#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST { class ProjectRecoveryPlan; }

namespace SST::Prototypes::JFM
{
    class JFMWorkspaceStore;
    void PrepareJFMRecovery(const Ref<JFMWorkspaceStore>& workspace, ProjectRecoveryPlan& recovery);

	class SaveProjectJob final : public IJob
	{
	public:
		SaveProjectJob(
			Ref<EventBus> eventBus,
			Path path,
			JFMProject snapshot,
			ProjectHandle project = InvalidProjectHandle,
			JFMOperationId operation = InvalidJFMOperationId,
			std::uint64_t revision = 0, std::vector<JFMMonteCarloBundleRecord> samples = {});

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
		JFMProject m_Snapshot;
        std::vector<JFMMonteCarloBundleRecord> m_Samples;
		ProjectHandle m_Project = InvalidProjectHandle;
		JFMOperationId m_Operation = InvalidJFMOperationId;
		std::uint64_t m_Revision = 0;
	};

	class LoadProjectJob final : public IJob
	{
	public:
		LoadProjectJob(Ref<EventBus> eventBus, Path path, JFMOperationId operation = InvalidJFMOperationId);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
		JFMOperationId m_Operation = InvalidJFMOperationId;
	};

	class SaveProjectBundleJob final : public IJob
	{
	public:
		SaveProjectBundleJob(
			Ref<EventBus> eventBus,
			Path path,
			JFMProjectBundleSnapshot snapshot,
			ProjectHandle project = InvalidProjectHandle,
			JFMOperationId operation = InvalidJFMOperationId,
			std::uint64_t revision = 0);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
		JFMProjectBundleSnapshot m_Snapshot;
		ProjectHandle m_Project = InvalidProjectHandle;
		JFMOperationId m_Operation = InvalidJFMOperationId;
		std::uint64_t m_Revision = 0;
	};

	class LoadProjectBundleJob final : public IJob
	{
	public:
		LoadProjectBundleJob(Ref<EventBus> eventBus, Path path, JFMOperationId operation = InvalidJFMOperationId);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
		JFMOperationId m_Operation = InvalidJFMOperationId;
	};

	class MaterializeProjectDataJob final : public IJob
	{
	public:
		MaterializeProjectDataJob(
			Ref<EventBus> eventBus,
			JFMProject project,
			Path pythonRoot,
			ProjectHandle projectHandle = InvalidProjectHandle,
			JFMOperationId operation = InvalidJFMOperationId);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		[[nodiscard]] bool UsesPythonRuntime() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		JFMProject m_Project;
		Path m_PythonRoot;
		ProjectHandle m_ProjectHandle = InvalidProjectHandle;
		JFMOperationId m_Operation = InvalidJFMOperationId;
	};
} // namespace SST::Prototypes::JFM
