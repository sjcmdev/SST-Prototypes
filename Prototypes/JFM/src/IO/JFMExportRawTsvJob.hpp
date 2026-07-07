#pragma once

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

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
} // namespace SST::Prototypes::JFM
