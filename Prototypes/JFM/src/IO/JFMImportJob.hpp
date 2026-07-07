#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"

namespace SST::Prototypes::JFM
{
	class JFMImportJob final : public IJob
	{
	public:
		JFMImportJob(Ref<EventBus> eventBus, std::vector<Path> paths, Path pythonRoot);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		void Execute(JobContext &context) override;

	private:
		void reportProgress(JobContext &context, std::size_t completed, std::size_t total, const std::string &message) const;

	private:
		Ref<EventBus> m_EventBus;
		std::vector<Path> m_Paths;
		Path m_PythonRoot;
	};
} // namespace SST::Prototypes::JFM
