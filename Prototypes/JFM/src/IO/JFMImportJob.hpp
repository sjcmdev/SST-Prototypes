#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Domain/JFMTypes.hpp"
#include "IO/JFMImportSources.hpp"

namespace SST::Prototypes::JFM
{
	class JFMImportJob final : public IJob
	{
	public:
		JFMImportJob(Ref<EventBus> eventBus, std::vector<Path> paths, Path pythonRoot, JFMImportOptions options = {}, ProjectHandle project = InvalidProjectHandle, JFMOperationId operation = InvalidJFMOperationId);
		JFMImportJob(
			Ref<EventBus> eventBus,
			std::vector<Path> paths,
			bool recursive,
			JFMImportPlacement placement,
			std::string targetSeriesId,
			Path pythonRoot,
			JFMImportOptions options = {},
			ProjectHandle project = InvalidProjectHandle,
			JFMOperationId operation = InvalidJFMOperationId);

		[[nodiscard]] std::string GetName() const override;
		[[nodiscard]] std::string GetType() const override;
		[[nodiscard]] bool UsesPythonRuntime() const override;
		void Execute(JobContext &context) override;

	private:
		void reportProgress(JobContext &context, std::size_t completed, std::size_t total, const std::string &message) const;

	private:
		Ref<EventBus> m_EventBus;
		std::vector<Path> m_Paths;
		bool m_Recursive = false;
		JFMImportPlacement m_Placement = JFMImportPlacement::NewSeries;
		std::string m_TargetSeriesId;
		Path m_PythonRoot;
		JFMImportOptions m_Options;
		ProjectHandle m_Project = InvalidProjectHandle;
		JFMOperationId m_Operation = InvalidJFMOperationId;
	};
} // namespace SST::Prototypes::JFM
