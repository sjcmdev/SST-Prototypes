#include <gtest/gtest.h>
#include "Core/App/ApplicationInternals.hpp"
#include "Core/BuildInfo.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include "Core/Diagnostics/DiagnosticsCommand.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/JobSystem/JobContext.hpp"
#include "Core/Logging/LogRegistry.hpp"
#include <fstream>
#include <thread>

using namespace SST;
namespace {
ApplicationSpecification parse(std::initializer_list<const char*> args) {
    std::vector<char*> argv{const_cast<char*>("SST")};
    for(auto arg:args) argv.push_back(const_cast<char*>(arg));
    return ApplicationDetail::ParseApplicationArguments(static_cast<int>(argv.size()),argv.data());
}
Ref<LogRegistry> configure(std::size_t bytes=4096) {
    auto registry=CreateRef<LogRegistry>();
    LoggerOptions options;
    options.logToFile=true; options.maxFileBytes=bytes; options.rotatedFiles=2;
    options.logRegistry=registry;
    Logger::Initialize(options);
    return registry;
}
std::string load(const Path& path) {
    std::string result,error;
    EXPECT_TRUE(TextFileIO::Load(path,result,error))<<error;
    return result;
}
bool finished(JobSystem& jobs,JobId id) {
    for(int i=0;i<500;++i) {
        const auto s=jobs.GetJob(id);
        if(s && (s->status==JobStatus::Completed || s->status==JobStatus::Failed || s->status==JobStatus::Cancelled)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
struct LoggedJob final:IJob {
    std::string GetName() const override {return "logged job";}
    std::string GetType() const override {return "professional-test";}
    LogContext GetLogContext() const override {
        LogContext c; c.engine="test-engine"; c.outputDirectory=DiagnosticsDirectory()/"job-output"; return c;
    }
    void Execute(JobContext& c) override {c.LogInfo("run-correlation-sentinel");}
};
}
TEST(ProfessionalCli, RejectsInvalidSyntaxBeforeBootstrap) {
    for(const auto* option:{"--open","--screenshot","--screenshot-after-frames","--export-diagnostics"}) EXPECT_THROW(parse({option}),std::invalid_argument)<<option;
    for(const auto* option:{"--unknown","--log-level=","--log-level=typo","--log-file=","--log-level","stray"}) EXPECT_THROW(parse({option}),std::invalid_argument)<<option;
    EXPECT_THROW(parse({"--open","--version"}),std::invalid_argument);
    EXPECT_THROW(parse({"--open","-h"}),std::invalid_argument);
    EXPECT_THROW(parse({"--screenshot","image.jpg"}),std::invalid_argument);
    for(const auto* frames:{"0","-1","1x","4294967296"}) EXPECT_THROW(parse({"--screenshot","image.png","--screenshot-after-frames",frames}),std::invalid_argument);
    EXPECT_THROW(parse({"--screenshot-after-frames","30"}),std::invalid_argument);
    EXPECT_THROW(parse({"--exit-after-screenshot"}),std::invalid_argument);
    EXPECT_THROW(parse({"--version","--help"}),std::invalid_argument);
    EXPECT_THROW(parse({"--version","--open","file"}),std::invalid_argument);
}
TEST(ProfessionalCli, KeepsValidOptionsTyped) {
    const auto spec=parse({"--open","path with spaces.sst","--screenshot","capture.png","--screenshot-after-frames","7","--exit-after-screenshot","--log-level=debug","--trace-events"});
    EXPECT_EQ(spec.openPath.Utf8(),"path with spaces.sst"); EXPECT_EQ(spec.screenshotAfterFrames,7u);
    EXPECT_EQ(spec.logLevel,LogLevel::Debug); EXPECT_TRUE(spec.traceEvents); EXPECT_TRUE(spec.exitAfterScreenshot);
    EXPECT_TRUE(parse({"--version"}).showVersion); EXPECT_TRUE(parse({"--help"}).showHelp);
}
TEST(ProfessionalLogging, RotatesAndSurvivesReinitialization) {
    configure(1024);
    const auto directory=Logger::SessionDirectory();
    for(int i=0;i<30;++i) DS_LOG_INFO("rotation record {} {}",i,std::string(150,'x'));
    Logger::Flush();
    std::size_t files=0;
    for(const auto& entry:std::filesystem::directory_iterator(directory.Native())) if(entry.path().extension()==".log") {++files;EXPECT_LE(entry.file_size(),1024u);}
    EXPECT_EQ(files,3u);
    configure(1024);
    EXPECT_EQ(Logger::SessionDirectory(),directory);
    EXPECT_TRUE(std::filesystem::exists((directory/"application.2.log").Native()));
}
TEST(ProfessionalLogging, KeepsTypedConcurrentContextAndRunFiles) {
    auto registry=configure(16384);
    std::vector<std::jthread> threads;
    for(unsigned int i=1;i<=4;++i) threads.emplace_back([i]{LogContext c;c.runId="run-"+std::to_string(i);c.jobId=i;c.attempt=2;c.memberId="member";c.code="test";c.operation="operation";ScopedLogContext guard(c);DS_LOG_WARN("correlation-sentinel");});
    threads.clear();
    unsigned int count=0;
    for(const auto& e:registry->Snapshot()) if(e.message=="correlation-sentinel") {++count;EXPECT_EQ(e.context.runId,"run-"+std::to_string(e.context.jobId));EXPECT_EQ(e.context.attempt,2u);EXPECT_EQ(e.sessionId,Logger::SessionId());EXPECT_GE(e.elapsedMs,0);}
    EXPECT_EQ(count,4u);
    EXPECT_TRUE(Logger::Context().runId.empty());
    std::size_t runs=0;
    for(const auto& entry:std::filesystem::recursive_directory_iterator((Logger::SessionDirectory()/"runs").Native())) if(entry.path().filename()=="application.log") {
        const auto text=load(Path::FromResolved(entry.path()));
        if(text.find("correlation-sentinel")!=std::string::npos) {++runs;EXPECT_NE(text.find("Z level=warning thread="),std::string::npos);EXPECT_NE(text.find("attempt=2 member=\"member\""),std::string::npos);}
    }
    EXPECT_EQ(runs,4u);
}
TEST(ProfessionalLogging, JobLogsPersistWithDistinctRetryAttempts) {
    auto registry=configure(16384);
    JobSystem jobs({},2); auto job=CreateRef<LoggedJob>(); const auto id=jobs.Submit(job);
    ASSERT_TRUE(finished(jobs,id)); ASSERT_TRUE(jobs.Retry(id)); ASSERT_TRUE(finished(jobs,id)); jobs.Shutdown();
    std::vector<std::uint32_t> attempts;
    std::string run;
    for(const auto& entry:registry->Snapshot()) if(entry.message=="run-correlation-sentinel") {
        EXPECT_EQ(entry.context.jobId,id); EXPECT_EQ(entry.context.engine,"test-engine");
        if(run.empty()) run=entry.context.runId; EXPECT_EQ(run,entry.context.runId); attempts.push_back(entry.context.attempt);
    }
    EXPECT_EQ(attempts,(std::vector<std::uint32_t>{1,2}));
    bool found=false;
    for(const auto& entry:std::filesystem::recursive_directory_iterator((DiagnosticsDirectory()/"job-output"/"logs").Native())) if(entry.path().filename()=="application.log") {
        const auto text=load(Path::FromResolved(entry.path())); if(text.find("run-correlation-sentinel")!=std::string::npos) {found=true;EXPECT_NE(text.find("attempt=2"),std::string::npos);}
    }
    EXPECT_TRUE(found);
}
TEST(ProfessionalLogging, ReconfigurationKeepsInFlightLoggerAndHonorsNewRoot) {
    configure();auto inFlight=Logger::Get();const auto oldDirectory=Logger::SessionDirectory();
    auto registry=configure();
    inFlight->info("in-flight logger remains valid");DS_LOG_INFO("new logger remains valid");Logger::Flush();
    for(const auto& entry:registry->Snapshot()) EXPECT_EQ(entry.message.find("File logging disabled"),std::string::npos);
    LoggerOptions options;options.logToFile=true;options.logRegistry=registry;options.logFilePath=DiagnosticsDirectory()/"alternate"/"custom.log";
    Logger::Initialize(options);EXPECT_NE(Logger::SessionDirectory(),oldDirectory);
    DS_LOG_INFO("alternate-root-sentinel");Logger::Flush();
    EXPECT_NE(load(Logger::SessionDirectory()/"application.log").find("alternate-root-sentinel"),std::string::npos);
    configure();
}
TEST(ProfessionalLogging, OversizedRecordsAreBoundedBeforeEverySink) {
    auto registry=configure(16384);
    DS_LOG_WARN("{}",std::string(20000,'x')+"-tail");Logger::Flush();
    const auto entries=registry->Snapshot();ASSERT_FALSE(entries.empty());
    const auto& text=entries.back().message;
    EXPECT_LE(text.size(),4096u);EXPECT_TRUE(text.starts_with("[truncated] "));EXPECT_TRUE(text.ends_with("-tail"));
    EXPECT_EQ(JobHistoryText(std::string(20000,'x')),BoundedText(std::string(20000,'x'),JobTextLimit));
    EXPECT_EQ(BoundedText("abc",2),"[t");
}
TEST(ProfessionalLogging, UnwritableRootKeepsRegistryAndReportsEffectiveState) {
    auto registry=configure();const auto file=DiagnosticsDirectory()/"not-a-directory";std::string error;
    ASSERT_TRUE(TextFileIO::Save(file,"owned test file",error))<<error;
    LoggerOptions options;options.logToFile=true;options.logRegistry=registry;options.logFilePath=file/"application.log";
    Logger::Initialize(options);EXPECT_FALSE(Logger::EffectiveOptions().logToFile);
    DS_LOG_INFO("registry remains available");bool reported=false;
    for(const auto& entry:registry->Snapshot()) if(entry.message.find("File logging disabled")!=std::string::npos && entry.message.find("writable log directory")!=std::string::npos) reported=true;
    EXPECT_TRUE(reported);configure();
}
TEST(ProfessionalDiagnostics, ExportAllowlistExcludesFreeTextAndRawData) {
    LogEntry entry; entry.message="SECRET=password partner.csv D:/private/name"; entry.sourceFile="PRIVATE-SOURCE";
    entry.context.operation="SECRET-OPERATION";entry.context.runId="SECRET-RUN"; entry.context.jobId=42;
    const auto preview=DiagnosticPreview({entry});
    for(const auto* secret:{"SECRET","password","partner.csv","D:/private","PRIVATE-SOURCE"}) EXPECT_EQ(preview.find(secret),std::string::npos);
    EXPECT_NE(preview.find("job=42"),std::string::npos);
    std::string error;const auto file=DiagnosticsDirectory()/"export-test.txt";
    ASSERT_TRUE(ExportDiagnostics(file,preview,error))<<error; EXPECT_EQ(load(file),preview);
    ASSERT_FALSE(ExportDiagnostics(DiagnosticsDirectory(),preview,error)); EXPECT_FALSE(error.empty());
}
TEST(ProfessionalDiagnostics, LiveMarkerIsIgnoredAndCleanExitRemovesIt) {
    InitializeLocalDiagnostics();
    for(const auto& session:PreviousInterruptedSessions()) EXPECT_NE(session.filename().Utf8(),"session-"+Logger::SessionId());
    const auto marker=DiagnosticsDirectory()/"crashes"/("session-"+Logger::SessionId())/"pending";
    EXPECT_TRUE(std::filesystem::exists(marker.Native())); FinishLocalDiagnostics(); EXPECT_FALSE(std::filesystem::exists(marker.Native()));
}
TEST(ProfessionalDiagnostics, ExportUsesCommandJobAndReturnsTypedErrors) {
    auto jobs=CreateRef<JobSystem>(Ref<EventBus>{},1); auto commands=CreateRef<CommandRegistry>(); RegisterDiagnosticsCommand(commands,jobs);
    const auto text=DiagnosticPreview({});const auto path=DiagnosticsDirectory()/"command-export.txt";
    const auto result=ExecuteDiagnosticsExport(*commands,{path,text}); ASSERT_TRUE(result)<<result.Error().technicalDetails;
    EXPECT_EQ(result.Value().id.value,"app.diagnostics.export"); EXPECT_EQ(load(path),text);
    EXPECT_FALSE(jobs->GetAllJobs().empty());
    const auto bad=ExecuteDiagnosticsExport(*commands,{DiagnosticsDirectory(),text});
    ASSERT_FALSE(bad); EXPECT_EQ(bad.Error().code,"diagnostics.write_failed");
    FilePath::string_type nulPath(1,static_cast<FilePath::value_type>('x'));nulPath.push_back(FilePath::value_type{});nulPath.push_back(static_cast<FilePath::value_type>('y'));
    const auto invalid=ExecuteDiagnosticsExport(*commands,{Path::FromResolved(FilePath(nulPath)),text});
    ASSERT_FALSE(invalid);EXPECT_EQ(invalid.Error().category,ErrorCategory::Validation);jobs->Shutdown();
}
TEST(ProfessionalBuildInfo, ContainsVersionAndContentHashesWithoutPaths) {
    const auto& b=GetBuildInfo(); EXPECT_FALSE(b.version.empty()); EXPECT_EQ(b.sourceSha256.size(),64u);
    EXPECT_NE(b.builtUtc.find('T'),std::string_view::npos); EXPECT_TRUE(b.builtUtc.ends_with('Z'));
    EXPECT_NE(b.dependencySha256.find("spdlog="),std::string_view::npos); EXPECT_NE(b.dependencySha256.find("uv.lock="),std::string_view::npos);
    EXPECT_EQ(BuildDescription().find("D:/"),std::string::npos);
}
