#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "Core/Utils/Memory.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/Time.hpp"
#include "Core/Logging/LogContext.hpp"

#include <spdlog/common.h>

namespace SST
{
	class LogRegistry;

	enum class LogLevel
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,
		Critical,
		Count
	};

	enum class LogCategory
	{
		General,
		Project,
		Import,
		Export,
		JobSystem,
		Parsing,
		UI,
		Scripting,
		Rendering,
		Capability,
		Config,
		Notification,
		Count
	};

	struct LogEntry
	{
		LogLevel level = LogLevel::Info;
		LogCategory category = LogCategory::General;
		std::string loggerName;
		std::string message;
		std::string sourceFile;
		std::string sourceFunction;
		std::uint32_t sourceLine = 0;
		std::thread::id threadId{};
		std::string threadLabel;
		Time::TimePoint timestamp{};
		LogContext context;
		std::string sessionId;
		double elapsedMs = 0;

		[[nodiscard]] std::string Origin() const;
		[[nodiscard]] std::string TimestampString() const;
		[[nodiscard]] std::string ToString() const;
	};

	struct LoggerOptions
	{
		LogLevel level = LogLevel::Info;
		bool logToFile = false;
		Path logFilePath;
		Ref<LogRegistry> logRegistry;
		std::size_t maxFileBytes = 4 * 1024 * 1024;
		std::size_t rotatedFiles = 2;
		std::size_t retainedSessions = 20;
	};

	const char *ToString(LogLevel level);
	const char *ToString(LogCategory category);
	LogCategory LogCategoryFromSource(std::string_view file);

	class Logger
	{
	public:
		static void Initialize(const LoggerOptions &options);
		static void Shutdown();
		static void Flush();
		static Ref<spdlog::logger> Get();
		[[nodiscard]] static const std::string &SessionId();
		[[nodiscard]] static Path SessionDirectory();
		[[nodiscard]] static const LogContext &Context();
		[[nodiscard]] static double ElapsedMs();
		[[nodiscard]] static LoggerOptions EffectiveOptions();
	};

	class ScopedLogContext final {
	public:
		explicit ScopedLogContext(LogContext context);
		~ScopedLogContext();
		ScopedLogContext(const ScopedLogContext&) = delete;
		ScopedLogContext& operator=(const ScopedLogContext&) = delete;
	private:
		LogContext m_Previous;
		spdlog::sink_ptr m_PreviousSink;
	};
} // namespace SST

#define DS_LOG_TRACE(...) ::SST::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::trace, __VA_ARGS__)
#define DS_LOG_DEBUG(...) ::SST::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::debug, __VA_ARGS__)
#define DS_LOG_INFO(...) ::SST::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::info, __VA_ARGS__)
#define DS_LOG_WARN(...) ::SST::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::warn, __VA_ARGS__)
#define DS_LOG_ERROR(...) ::SST::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::err, __VA_ARGS__)
#define DS_LOG_CRITICAL(...) ::SST::Logger::Get()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::critical, __VA_ARGS__)
