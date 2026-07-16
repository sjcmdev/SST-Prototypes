#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"
#include "Events/JFMEvents.hpp"

#include <vector>

namespace SST::Prototypes::JFM
{
	class JFMExportRawTsvJob final : public IJob
	{
	public:
		JFMExportRawTsvJob(Ref<EventBus> eventBus, Path path, JFMCharacteristicRecord characteristic, ProjectHandle project = InvalidProjectHandle, JFMOperationId operation = InvalidJFMOperationId);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
		JFMCharacteristicRecord m_Characteristic;
		ProjectHandle m_Project = InvalidProjectHandle;
		JFMOperationId m_Operation = InvalidJFMOperationId;
	};

	class JFMExportRawTsvBatchJob final : public IJob
	{
	public:
		JFMExportRawTsvBatchJob(Ref<EventBus> eventBus, Path outputDirectory, std::vector<JFMExportItem> items, ProjectHandle project = InvalidProjectHandle, JFMOperationId operation = InvalidJFMOperationId);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_OutputDirectory;
		std::vector<JFMExportItem> m_Items;
		ProjectHandle m_Project = InvalidProjectHandle;
		JFMOperationId m_Operation = InvalidJFMOperationId;
	};
} // namespace SST::Prototypes::JFM
