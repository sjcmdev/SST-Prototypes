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
		JFMExportRawTsvJob(Ref<EventBus> eventBus, Path path, JFMCharacteristic characteristic);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_Path;
		JFMCharacteristic m_Characteristic;
	};

	class JFMExportRawTsvBatchJob final : public IJob
	{
	public:
		JFMExportRawTsvBatchJob(Ref<EventBus> eventBus, Path outputDirectory, std::vector<JFMExportItem> items);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		Path m_OutputDirectory;
		std::vector<JFMExportItem> m_Items;
	};
} // namespace SST::Prototypes::JFM
