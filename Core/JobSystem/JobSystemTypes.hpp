#pragma once
#include <stop_token>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Time.hpp"
#include "Core/Logging/LogContext.hpp"
#include "Core/Utils/BoundedText.hpp"

namespace SST
{
	using JobId = std::uint64_t;

	enum class JobStatus
	{
		Queued,
		Running,
		Completed,
		Failed,
		Cancelled
	};

	enum class JobPriority
	{
		Lowest,
		Low,
		Normal,
		High,
		Highest
	};

	enum class JobLogLevel
	{
		Debug,
		Info,
		Warning,
		Error
	};

	struct JobLogEntry
	{
		JobLogLevel level = JobLogLevel::Info;
		std::string message;
		Time::TimePoint timestamp{};
	};

	
	class JobContext;
	class IJob;
	
	struct DelayedSubmission
	{
		JobId id = 0;
		Ref<IJob> job;
		JobPriority priority = JobPriority::Normal;
		Time::SteadyTimePoint dueAt{};
		
		// Scheduling order in delayed queue: earliest dueAt first.
		[[nodiscard]] bool operator<(const DelayedSubmission &other) const noexcept
		{
			return dueAt < other.dueAt;
		}
	};
	
	struct JobSnapshot
	{
		JobId id = 0;
		JobId parentId = 0;
		std::string name;
		std::string type;
		JobStatus status = JobStatus::Queued;
		JobPriority priority = JobPriority::Normal;
		float totalWork = 0.0f;
		float completedWork = 0.0f;
		std::string currentStage;
		std::string currentMessage;
		std::string errorMessage;
		bool cancellationRequested = false;
		Time::TimePoint createdAt{};
		Time::TimePoint startedAt{};
		Time::TimePoint finishedAt{};

		// Natural ordering in UI/query collections: by stable JobId.
		[[nodiscard]] bool operator<(const JobSnapshot &other) const noexcept
		{
			return id < other.id;
		}
	};
	// Unowned finished metadata is capped independently of active work/adoption; each text field is at most 4 KiB.
	inline constexpr std::size_t JobHistoryLimit = 256;
	inline constexpr std::size_t JobTextLimit = 4096;
	inline std::string JobHistoryText(const std::string &text)
	{
        return BoundedText(text,JobTextLimit);
	}

	struct JobRecord
	{
		JobSnapshot snapshot;
		std::vector<JobLogEntry> logs;
		Ref<std::stop_source> cancellationRequested = CreateRef<std::stop_source>();
        Ref<std::atomic_bool> executionClaimed;
		WeakRef<IJob> job; // Retry/Resume require a live consumer-owned request.
		LogContext logContext;
	};

	class JobCancelledException final : public std::runtime_error
	{
	public:
		explicit JobCancelledException(const std::string &message = "Job cancelled")
			: std::runtime_error(message) {}
	};

	class IJobExecutionGuard
	{
	public:
		virtual ~IJobExecutionGuard() = default;
	};

	class IJob
	{
	public:
		virtual ~IJob() = default;

		[[nodiscard]] virtual std::string GetName() const = 0;
		[[nodiscard]] virtual std::string GetType() const = 0;
		[[nodiscard]] virtual bool UsesPythonRuntime() const { return false; }
		[[nodiscard]] virtual LogContext GetLogContext() const { return {}; }
		virtual void Execute(JobContext &context) = 0;
	};
} // namespace SST
