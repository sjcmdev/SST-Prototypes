#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	class SaveProjectJob final : public IJob
	{
	public:
		SaveProjectJob(Ref<EventBus> eventBus, Path path, JFMProject snapshot);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
		JFMProject m_Snapshot;
	};

	class LoadProjectJob final : public IJob
	{
	public:
		LoadProjectJob(Ref<EventBus> eventBus, Path path);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
	};
} // namespace SST::Prototypes::JFM
