#pragma once

#include <cstdint>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"

namespace SST::Prototypes::JFM
{
	class JFMImportPreviewJob final : public IJob
	{
	public:
		JFMImportPreviewJob(
			Ref<EventBus> eventBus,
			std::uint64_t generation,
			std::vector<Path> paths,
			bool recursive,
			Path pythonRoot,
			JFMImportOptions options);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		[[nodiscard]] bool UsesPythonRuntime() const override;
		void Execute(JobContext &context) override;

	private:
		Ref<EventBus> m_EventBus;
		std::uint64_t m_Generation = 0;
		std::vector<Path> m_Paths;
		bool m_Recursive = false;
		Path m_PythonRoot;
		JFMImportOptions m_Options;
	};
} // namespace SST::Prototypes::JFM
