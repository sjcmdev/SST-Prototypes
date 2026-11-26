#include "Core/dspch.hpp"

#include <ctime>
#include <filesystem>
#include <sstream>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Core/Logging/LogRegistrySink.hpp"
#include "Core/Logging/Logger.hpp"
#include "Core/Utils/Path.hpp"
#include "Core/Utils/BoundedText.hpp"
#include "Core/BuildInfo.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include <chrono>
#include <random>
#ifdef _WIN32
#include <windows.h>
#endif
namespace SST
{
	static Ref<spdlog::logger> s_Logger = nullptr;
	namespace {
		const auto s_Start = std::chrono::steady_clock::now();
		const std::string s_Session = fmt::format("{}-{:08x}",
			std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), std::random_device{}());
		Path s_SessionDirectory;
		LoggerOptions s_Options;
		std::recursive_mutex s_StorageMutex;
		thread_local LogContext s_Context;
		thread_local spdlog::sink_ptr s_RunSink;
		std::mutex s_RunMutex;
		std::unordered_map<std::string, std::weak_ptr<spdlog::sinks::sink>> s_RunSinks;
		constexpr char LogOwner[]="SST-LOG-v1\n";
		class BoundedLogger final:public spdlog::logger {
		public:
			BoundedLogger(const std::vector<spdlog::sink_ptr>& sinks,std::size_t limit):spdlog::logger("SST",sinks.begin(),sinks.end()),m_Limit(limit){}
		private:
			void sink_it_(const spdlog::details::log_msg& message) override {
				if(message.payload.size()<=m_Limit) { spdlog::logger::sink_it_(message);return; }
				const auto text=BoundedText(std::string_view(message.payload.data(),message.payload.size()),m_Limit);
				auto bounded=message;bounded.payload=spdlog::string_view_t(text.data(),text.size());
				spdlog::logger::sink_it_(bounded);
			}
			std::size_t m_Limit;
		};
		class LockedFileSink final : public spdlog::sinks::sink {
		public:
			explicit LockedFileSink(const Path& directory, std::size_t bytes, std::size_t files)
			{
				std::filesystem::create_directories(directory.Native());
#ifdef _WIN32
				m_Lock = CreateFileW((directory / "active").wstring().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if(m_Lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Log directory is in use");
				DWORD written=0;
				if(!WriteFile(m_Lock,LogOwner,sizeof(LogOwner)-1,&written,nullptr) || written!=sizeof(LogOwner)-1) {CloseHandle(m_Lock);throw std::runtime_error("Cannot write log ownership marker");}
				try { m_Sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>((directory/"application.log").wstring(),bytes,files); }
				catch(...) { CloseHandle(m_Lock); throw; }
#else
				m_Sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>((directory/"application.log").string(),bytes,files);
#endif
			}
			~LockedFileSink() override {
				m_Sink.reset();
#ifdef _WIN32
				CloseHandle(m_Lock);
#endif
			}
			void log(const spdlog::details::log_msg& msg) override { m_Sink->log(msg); }
			void flush() override { m_Sink->flush(); }
			void set_pattern(const std::string& pattern) override { m_Sink->set_pattern(pattern); }
			void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override { m_Sink->set_formatter(std::move(formatter)); }
			void configure(std::size_t bytes,std::size_t files) { m_Sink->set_max_size(bytes);m_Sink->set_max_files(files); }
		private:
			std::shared_ptr<spdlog::sinks::rotating_file_sink_mt> m_Sink;
#ifdef _WIN32
			HANDLE m_Lock = INVALID_HANDLE_VALUE;
#endif
		};
		std::unordered_map<std::string,std::weak_ptr<LockedFileSink>> s_SessionSinks;

		class ContextFlag final : public spdlog::custom_flag_formatter {
		public:
			void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override {
				const auto& c = Logger::Context();
				fmt::format_to(std::back_inserter(dest), "category={} session={} job={} run={} attempt={} member={:?} operation={:?} engine={:?} build={} code={:?} elapsed_ms={:.3f}",
					ToString(LogCategoryFromSource(msg.source.filename ? std::filesystem::path(msg.source.filename).stem().string() : "")), Logger::SessionId(), c.jobId, c.runId, c.attempt, c.memberId, c.operation.empty() ? "application" : c.operation, c.engine.empty() ? "Core" : c.engine, GetBuildInfo().sourceSha256, c.code, Logger::ElapsedMs());
			}
			std::unique_ptr<custom_flag_formatter> clone() const override { return std::make_unique<ContextFlag>(); }
		};
		std::unique_ptr<spdlog::formatter> fileFormatter() {
			auto formatter = std::make_unique<spdlog::pattern_formatter>(spdlog::pattern_time_type::utc);
			formatter->add_flag<ContextFlag>('*').set_pattern("%Y-%m-%dT%H:%M:%S.%eZ level=%l thread=%t source=%s:%# [%*] %v");
			return formatter;
		}
		class RunSink final : public spdlog::sinks::base_sink<std::mutex> {
			void sink_it_(const spdlog::details::log_msg& msg) override { if (s_RunSink) { s_RunSink->log(msg); if(msg.level >= spdlog::level::err) s_RunSink->flush(); } }
			void flush_() override { if (s_RunSink) s_RunSink->flush(); }
		};
		void retainSessions(const Path& root, std::size_t count) {
#ifndef _WIN32
			// shortcut: non-Windows pruning stays disabled until native exclusive session locks are implemented.
			(void)root; (void)count; return;
#endif
			std::vector<std::filesystem::directory_entry> sessions;
			for(const auto& entry : std::filesystem::directory_iterator(root.Native()))
				if(entry.is_directory() && !entry.is_symlink() && entry.path().filename().string().starts_with("session-")) sessions.push_back(entry);
			std::sort(sessions.begin(), sessions.end(), [](const auto& a, const auto& b){ return a.last_write_time() > b.last_write_time(); });
			for(std::size_t i = count; i < sessions.size(); ++i) {
				if(sessions[i].path().filename()==("session-"+s_Session)) continue;
				// Windows releases the lock even after TerminateProcess; a killed session can be pruned.
#ifdef _WIN32
				HANDLE probe = CreateFileW((sessions[i].path()/"active").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if(probe == INVALID_HANDLE_VALUE) continue;
				char owner[sizeof(LogOwner)]{};DWORD read=0;
				const bool owned=ReadFile(probe,owner,sizeof(LogOwner)-1,&read,nullptr) && read==sizeof(LogOwner)-1 && std::string_view(owner,read)==LogOwner;
				CloseHandle(probe);
				if(!owned) continue;
#endif
				std::error_code error;
				std::filesystem::remove_all(sessions[i].path(), error);
			}
		}
	}
	const std::string& Logger::SessionId() { return s_Session; }
	Path Logger::SessionDirectory() { std::lock_guard lock(s_StorageMutex);return s_SessionDirectory; }
	const LogContext& Logger::Context() { return s_Context; }
	LoggerOptions Logger::EffectiveOptions() { std::lock_guard lock(s_StorageMutex);return s_Options; }
	double Logger::ElapsedMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-s_Start).count(); }
	ScopedLogContext::ScopedLogContext(LogContext context) : m_Previous(std::move(s_Context)), m_PreviousSink(std::move(s_RunSink)) {
		s_Context = std::move(context);
		const auto options=Logger::EffectiveOptions();
		if(options.logToFile && !s_Context.runId.empty()) {
			try {
				const auto requested = s_Context.outputDirectory.Empty() ? Logger::SessionDirectory() / "runs" : s_Context.outputDirectory / "logs";
				const auto root = Path::FromResolved(std::filesystem::absolute(requested.Native()).lexically_normal());
				// Run IDs are correlation values, never paths supplied by a job.
				const auto key = std::hash<std::string>{}(s_Context.runId);
				const auto directory = root / fmt::format("session-{}-{}", s_Session, key);
				std::lock_guard lock(s_RunMutex);
				if(const auto found=s_RunSinks.find(directory.Utf8());found!=s_RunSinks.end()) s_RunSink=found->second.lock();
				if(!s_RunSink) {
					FileSystem::CreateDirectories(root.Native());
					retainSessions(root, 127);
					s_RunSink = std::make_shared<LockedFileSink>(directory, options.maxFileBytes, options.rotatedFiles);
					s_RunSink->set_formatter(fileFormatter());
					for(auto it=s_RunSinks.begin();it!=s_RunSinks.end();) if(it->second.expired()) it=s_RunSinks.erase(it); else ++it;
					s_RunSinks[directory.Utf8()] = s_RunSink;
				}
			} catch(const std::exception& error) { DS_LOG_ERROR("Run logging unavailable: {}. Check output directory permissions and free space.", error.what()); }
		}
	}
	ScopedLogContext::~ScopedLogContext() {
		try { if(s_RunSink) s_RunSink->flush(); }
		catch(const std::exception& error) { std::fprintf(stderr,"Run log flush failed: %s. Check free disk space.\n",error.what()); }
		s_RunSink = std::move(m_PreviousSink);
		s_Context = std::move(m_Previous);
	}
	spdlog::level::level_enum ToSpdlogLevel(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:
				return spdlog::level::trace;
			case LogLevel::Debug:
				return spdlog::level::debug;
			case LogLevel::Info:
				return spdlog::level::info;
			case LogLevel::Warn:
				return spdlog::level::warn;
			case LogLevel::Error:
				return spdlog::level::err;
			case LogLevel::Critical:
				return spdlog::level::critical;
			case LogLevel::Count:
				break;
		}

		return spdlog::level::info;
	}

	const char *ToString(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:
				return "trace";
			case LogLevel::Debug:
				return "debug";
			case LogLevel::Info:
				return "info";
			case LogLevel::Warn:
				return "warn";
			case LogLevel::Error:
				return "error";
			case LogLevel::Critical:
				return "critical";
			case LogLevel::Count:
				break;
		}

		return "info";
	}

	const char *ToString(LogCategory category)
	{
		switch (category)
		{
			case LogCategory::General:
				return "General";
			case LogCategory::Project:
				return "Project";
			case LogCategory::Import:
				return "Import";
			case LogCategory::Export:
				return "Export";
			case LogCategory::JobSystem:
				return "JobSystem";
			case LogCategory::Parsing:
				return "Parsing";
			case LogCategory::UI:
				return "UI";
			case LogCategory::Scripting:
				return "Scripting";
			case LogCategory::Rendering:
				return "Rendering";
			case LogCategory::Capability:
				return "Capability";
			case LogCategory::Config:
				return "Config";
			case LogCategory::Notification:
				return "Notification";
			case LogCategory::Count:
				break;
		}

		return "General";
	}

	std::string LogEntry::Origin() const
	{
		if (!sourceFile.empty())
		{
			std::filesystem::path path(sourceFile);
			std::string origin = path.stem().string();
			if (origin.empty())
				origin = path.filename().string();
			if (sourceLine > 0)
				origin += ":" + std::to_string(sourceLine);
			return origin;
		}

		if (!loggerName.empty())
			return loggerName;
		if (!threadLabel.empty())
			return threadLabel;
		return "-";
	}

	std::string LogEntry::TimestampString() const
	{
		if (timestamp == Time::TimePoint{})
			return "-";

		const std::time_t time = Time::Clock::to_time_t(timestamp);
		std::tm localTime{};
#if defined(_WIN32)
		localtime_s(&localTime, &time);
#else
		localtime_r(&time, &localTime);
#endif

		char buffer[32] = {};
		std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &localTime);
		return std::string(buffer);
	}

	std::string LogEntry::ToString() const
	{
		std::ostringstream stream;
		stream << '[' << TimestampString() << "] "
		       << '[' << SST::ToString(level) << "] "
		       << '[' << SST::ToString(category) << "] "
		       << Origin() << ": "
		       << message;
		return stream.str();
	}

	void ResetLoggerStorage()
	{
		if (s_Logger != nullptr)
		{
			s_Logger->flush();
			spdlog::drop(s_Logger->name());
			s_Logger.reset();
		}
	}


	void Logger::Initialize(const LoggerOptions &options)
	{
		std::lock_guard lock(s_StorageMutex);
		const spdlog::level::level_enum logLevel = ToSpdlogLevel(options.level);
		ResetLoggerStorage();
		s_Options = options;

		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(CreateRef<spdlog::sinks::stdout_color_sink_mt>());
		sinks.push_back(CreateRef<RunSink>());
		if (options.logRegistry != nullptr)
			sinks.push_back(CreateRef<LogRegistrySink>(options.logRegistry));

		std::string fileLoggingError;
		std::string resolvedLogFilePath;
		if (options.logToFile)
		{
			Path logFilePath = options.logFilePath;
			try
			{
					if (logFilePath.empty() || logFilePath.Utf8()=="logs/SST.log") logFilePath = DiagnosticsDirectory()/"logs"/"SST.log";
					const auto root = Path::FromResolved(std::filesystem::absolute((logFilePath.parent_path()/"sessions").Native()).lexically_normal());
					FileSystem::CreateDirectories(root.Native());
					retainSessions(root, std::max(std::size_t{1}, options.retainedSessions)-1);
					s_SessionDirectory = root / ("session-" + s_Session);
				logFilePath = s_SessionDirectory / "application.log";
				resolvedLogFilePath = logFilePath.Utf8();
				if (!logFilePath.parent_path().empty())
					FileSystem::CreateDirectories(logFilePath.parent_path().Native());
				const auto found=s_SessionSinks.find(s_SessionDirectory.Utf8());
				auto sink=found==s_SessionSinks.end() ? std::shared_ptr<LockedFileSink>{} : found->second.lock();
				if(sink) sink->configure(options.maxFileBytes,options.rotatedFiles);
				else sink=CreateRef<LockedFileSink>(s_SessionDirectory, options.maxFileBytes, options.rotatedFiles);
				for(auto it=s_SessionSinks.begin();it!=s_SessionSinks.end();) if(it->second.expired()) it=s_SessionSinks.erase(it);else ++it;
				s_SessionSinks[s_SessionDirectory.Utf8()]=sink;
				sinks.push_back(std::move(sink));
			}
			catch (const std::exception &exception)
			{
				fileLoggingError = exception.what();
			}
		}

		s_Logger = CreateRef<BoundedLogger>(sinks,std::min(std::size_t{4096},std::max(std::size_t{64},options.maxFileBytes/4)));
		spdlog::set_default_logger(s_Logger);
		spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
		for(auto& sink : sinks) sink->set_formatter(fileFormatter());
		spdlog::set_level(logLevel);
		spdlog::flush_on(spdlog::level::err);
		s_Logger->set_level(logLevel);
		s_Logger->flush_on(spdlog::level::err);

		if (!fileLoggingError.empty())
		{
			s_Options.logToFile=false;
			s_Logger->error("File logging disabled [{}]: {}. Choose a writable log directory and check free disk space.", resolvedLogFilePath, fileLoggingError);
		}
		else if (options.logToFile)
			s_Logger->info("File logging active: {}", resolvedLogFilePath.empty() ? "logs/SST.log" : resolvedLogFilePath);
	}

	void Logger::Shutdown()
	{
		std::lock_guard lock(s_StorageMutex);
		Flush();
		spdlog::shutdown();
		s_Logger.reset();
		s_Options.logRegistry.reset();
	}

	void Logger::Flush()
	{
		std::lock_guard lock(s_StorageMutex);
		if (s_Logger != nullptr)
			s_Logger->flush();
		spdlog::apply_all([](const std::shared_ptr<spdlog::logger> &logger) {
			if (logger != nullptr)
				logger->flush();
		});
	}

	Ref<spdlog::logger> Logger::Get()
	{
		std::lock_guard lock(s_StorageMutex);
		if (s_Logger == nullptr)
		{
			LoggerOptions defaultOptions;
			defaultOptions.logToFile = true;
			Initialize(defaultOptions);
		}

		return s_Logger;
	}
} // namespace SST
