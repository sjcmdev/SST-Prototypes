#include <gtest/gtest.h>
#include "Core/App/Managers/ConfigManager.hpp"
#include "Core/App/Managers/ConfigProfileStore.hpp"
#include "Core/App/Managers/SettingsReset.hpp"
#include "Core/App/Serialization/YamlConfigSerializer.hpp"
#include "Core/Diagnostics/LocalDiagnostics.hpp"
#include "Core/IO/ProjectRecovery.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/JobSystem/JobOutcome.hpp"
#include "Core/JobSystem/JobSystem.hpp"
#include "Core/ProgressTrackingSystem/ProgressTracker.hpp"
#include "Core/EventSystem/BusEventSystem/EventBus.hpp"
#include "Core/ScientificRuntime/Python/OperationErrorJson.hpp"
#include <sst/plotlab/Engine.hpp>
#include <Windows.h>
#undef GetJob
#include <fstream>
#include <thread>

using namespace SST;
namespace {
Path scratch(const char* name) { return DiagnosticsDirectory()/"pro-core2-tests"/name; }
void save(const Path& path,std::string_view text) {std::string error;ASSERT_TRUE(TextFileIO::Save(path,text,error))<<error;}
std::string load(const Path& path) {std::string text,error;EXPECT_TRUE(TextFileIO::Load(path,text,error))<<error;return text;}
struct FailingJob final : IJob {
    ErrorCategory category;
    explicit FailingJob(ErrorCategory category):category(category){}
    std::string GetName() const override{return "Outcome test";}
    std::string GetType() const override{return "outcome.test";}
    void Execute(JobContext&) override {throw OperationException(MakeOperationError(category,"member 2 failed","test"));}
};
}
TEST(CoreOutcome, ExitContractNeverTreatsTerminalFailureAsSuccess) {
    EXPECT_EQ(CliExitContractVersion,1u); EXPECT_EQ(CliExitCode(VoidResult{}),0);
    for(const auto& [category,code]:{std::pair{ErrorCategory::Validation,2}, {ErrorCategory::UnsupportedVersion,2},
        {ErrorCategory::Numerical,3},{ErrorCategory::Partial,3},{ErrorCategory::IO,4},{ErrorCategory::Internal,5},
        {ErrorCategory::Capability,5},{ErrorCategory::ResourceLimit,5},{ErrorCategory::OutOfMemory,5},
        {ErrorCategory::Cancelled,130},{ErrorCategory::TimedOut,124}}) {
        const auto error=MakeOperationError(category,"detail","operation");
        EXPECT_EQ(CliExitCode(error),code); EXPECT_FALSE(error.code.empty()); EXPECT_FALSE(error.suggestion.empty());
        const auto python=PythonBoundary::Failure(error); EXPECT_FALSE(python.at("ok").get<bool>());
        EXPECT_EQ(python.at("error").at("category"),ErrorCategoryName(category));
    }
}
TEST(CoreOutcome, JobPythonAndUiRetainTypedTerminalCause) {
    auto bus=CreateRef<EventBus>(); ProgressTracker tracker(bus); JobSystem jobs(bus,1);
    for(const auto category:{ErrorCategory::Cancelled,ErrorCategory::TimedOut,ErrorCategory::Partial,ErrorCategory::IO,ErrorCategory::Validation}) {
        auto job=CreateRef<FailingJob>(category);const auto id=jobs.Submit(job);
        bool done=false;
        for(unsigned i=0;i<1000;++i) {const auto s=jobs.GetJob(id);if(s && s->finishedAt!=Time::TimePoint{}){done=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ASSERT_TRUE(done);bus->ProcessQueue();const auto snapshot=jobs.GetJob(id);ASSERT_TRUE(snapshot);ASSERT_TRUE(snapshot->error);
        EXPECT_EQ(snapshot->error->category,category);EXPECT_EQ(snapshot->error->jobId,id);
        const auto python=PythonBoundary::Failure(*snapshot);EXPECT_EQ(python.at("error").at("job_id"),id);
        EXPECT_EQ(python.at("status"),JobOutcomeName(*snapshot));
    }
    jobs.Shutdown();
}
TEST(CoreSettings, EveryDocumentKindRejectsFutureSchemaWithoutMutation) {
    YamlConfigSerializer codec;std::string error;auto config=ConfigManager::CreateDefaultConfig(scratch("settings"));config.window.title="keep-me";
    const auto before=config;
    for(auto load:{&YamlConfigSerializer::DeserializeDefaultConfig,&YamlConfigSerializer::DeserializeUserSettings,&YamlConfigSerializer::DeserializeConfigProfile}) {
        EXPECT_FALSE((codec.*load)("schema_version: 99\nwindow: {title: changed}\n",config,error));EXPECT_EQ(config,before);EXPECT_NE(error.find("schema_version"),std::string::npos);
    }
    auto appearance=config.appearance;EXPECT_FALSE(codec.DeserializeAppearanceTheme("schema_version: 99",appearance,error));EXPECT_EQ(appearance,before.appearance);
}
TEST(CoreSettings, InvalidLateFieldCannotPartiallyApplyAndMissingFieldsKeepDefaults) {
    YamlConfigSerializer codec;std::string error;auto config=ConfigManager::CreateDefaultConfig(scratch("settings"));const auto before=config;
    for(const char* text:{"window: {title: changed}\nui: {font_scale: invalid}","ui: {font_scale: .nan}","window: {width: 10}","log: {level: typo}","jobs: {default_worker_threads: 0}","event_queue: {growth_step: -1}","ui: []","appearance: {clear_color: [1, 0]}","appearance: {clear_color: [1, 0, 0, 2]}"}) {
        EXPECT_FALSE(codec.DeserializeConfigProfile(text,config,error))<<text;EXPECT_EQ(config,before)<<text;EXPECT_FALSE(error.empty());
    }
    EXPECT_TRUE(codec.DeserializeUserSettings("window: {width: 900}",config,error))<<error;
    EXPECT_EQ(config.window.width,900);EXPECT_EQ(config.ui,before.ui);
    EXPECT_FALSE(config==before); // Typed comparisons must include fields omitted from old YAML fingerprints.
    auto copy=config;copy.ui.fontScaleMin=0.8f;EXPECT_FALSE(copy==config);
}
TEST(CoreSettings, ProfileFailurePreservesCallerAndTypedSetterValidatesBeforeCommit) {
    auto manager=CreateRef<ConfigManager>(scratch("profile-install"));std::string error;
    ASSERT_TRUE(manager->Initialize(error))<<error;ConfigProfileStore profiles(manager);
    const auto bad=scratch("bad-profile.yaml");save(bad,"schema_version: 77\n");
    auto config=manager->GetConfig();config.window.title="retained";const auto before=config;
    EXPECT_FALSE(profiles.Load(bad,config,error));EXPECT_EQ(config,before);
    auto invalid=manager->GetConfig();invalid.ui.fontScale=0;
    EXPECT_THROW(manager->SetConfig(invalid),std::invalid_argument);EXPECT_NE(manager->GetConfig().ui.fontScale,0);
}
TEST(CoreSettings, UserRootIsSeparateAndPortableRootIsExplicit) {
    const auto install=scratch("read-only-install");
    const auto* current=_wgetenv(L"SST_PORTABLE_ROOT");const std::wstring previous=current?current:L"";
    _wputenv_s(L"SST_PORTABLE_ROOT",L"");
    const auto installed=ConfigManager::ResolvePortablePaths(install);
    EXPECT_NE(installed.userConfigDirectory,installed.appConfigDirectory);
    EXPECT_NE(installed.userConfigDirectory.Utf8().find("SemiconductorStudio"),std::string::npos);
    const auto portable=scratch("explicit-portable");_wputenv_s(L"SST_PORTABLE_ROOT",portable.wstring().c_str());
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const auto paths=ConfigManager::ResolvePortablePaths(install);EXPECT_EQ(paths.userConfigDirectory,portable/Path::FromResolved(std::filesystem::path(executable).stem())/"config");
    _wputenv_s(L"SST_PORTABLE_ROOT",previous.c_str());
}
TEST(CoreRecovery, LiveLeaseIsExcludedAndFailedGenerationCannotReplaceCompletedSnapshot) {
    const auto root=scratch("recovery");
    { ProjectRecovery store(root); store.Begin();
      store.Save(1,1,Path{},".txt",[](const Path& p){save(p,"complete");}); store.End();
      EXPECT_TRUE(ProjectRecovery::Candidates(root).empty());
      store.Begin();EXPECT_THROW(store.Save(1,2,Path{},".txt",[](const Path& p){save(p,"partial");throw std::runtime_error("interrupted");}),std::runtime_error);
    }
    auto candidates=ProjectRecovery::Candidates(root);ASSERT_EQ(candidates.size(),1u);EXPECT_EQ(load(candidates.front().snapshot),"complete");
    ProjectRecovery::Discard(candidates.front());EXPECT_TRUE(ProjectRecovery::Candidates(root).empty());
}
TEST(CoreRecovery, RetainsFiveGenerationsAndRemovesExplicitlyCleanDocuments) {
    const auto root=scratch("retention");
    { ProjectRecovery store(root);
      for(unsigned i=0;i<8;++i) {store.Begin();store.Save(1,i,Path{},".txt",[i](const Path& p){save(p,std::to_string(i));});store.End();}
    }
    auto candidates=ProjectRecovery::Candidates(root);ASSERT_EQ(candidates.size(),1u);EXPECT_EQ(load(candidates.front().snapshot),"7");
    std::size_t count=0;for(const auto& e:std::filesystem::directory_iterator(candidates.front().directory.Native()))if(std::filesystem::exists(e.path()/"ready"))++count;
    EXPECT_EQ(count,5u);ProjectRecovery::Discard(candidates.front());
    { ProjectRecovery store(root);store.Begin();store.Save(2,1,Path{},".txt",[](const Path& p){save(p,"explicitly saved");});store.End();store.Begin();store.End(); }
    EXPECT_TRUE(ProjectRecovery::Candidates(root).empty());
}
TEST(CoreRecoveryProcess, KillProbe) {
    const auto* mode=std::getenv("SST_RECOVERY_TEST_MODE");if(!mode)return;
    const auto root=scratch("kill-restart");
    if(std::string_view(mode)=="write") {
        ProjectRecovery recovery(root);recovery.Begin();recovery.Save(1,1,Path{},".txt",[](const Path& p){save(p,"committed before kill");});recovery.End();
        recovery.Begin();recovery.Save(1,2,Path{},".txt",[&](const Path& p){save(p,"unfinished");save(root/"kill-ready","ready");for(;;)Sleep(100);});
    } else {
        const auto candidates=ProjectRecovery::Candidates(root);ASSERT_EQ(candidates.size(),1u);EXPECT_EQ(load(candidates.front().snapshot),"committed before kill");
        ProjectRecovery restarted(root);restarted.Begin();restarted.Save(2,1,Path{},".txt",[&](const Path& p){save(p,load(candidates.front().snapshot));});restarted.End();
        ProjectRecovery::Discard(candidates.front());
    }
}
TEST(CoreRecoveryProcess, PlotSaveKillProbe) {
    const auto* mode=std::getenv("SST_PLOT_SAVE_TEST_MODE");if(!mode)return;
    namespace pl=sst::plotlab;
    const auto target=scratch("kill-reservation.plotlab");
    if(std::string_view(mode)=="write") {
        std::filesystem::create_directories(target.parent_path().Native());
        pl::Project("before-kill").save(target.Native());
        save(scratch("plot-baseline-ready"),"ready");
        pl::Project(std::string(16*1024*1024,'x')).save(target.Native());
    } else {
        auto previous=pl::Project::load(target.Native());EXPECT_EQ(previous.object(previous.root()).name,"before-kill");
        pl::Project("after-restart").save(target.Native());
        auto restored=pl::Project::load(target.Native());EXPECT_EQ(restored.object(restored.root()).name,"after-restart");
    }
}
TEST(CoreRecovery, PlotLabOwnedStaleWritingReservationIsRecoverableButUnknownDataIsPreserved) {
    namespace pl=sst::plotlab;
    const auto target=scratch("reservation.plotlab");std::filesystem::create_directories(target.parent_path().Native());
    auto writing=target.Native();writing+=L".writing";
    std::filesystem::create_directories(writing);save(Path::FromResolved(writing/"owner"),"SST-PLOTLAB-SAVE-v1\n");save(Path::FromResolved(writing/"project.tmp"),"unfinished");
    pl::Project project("recovered-save");
    EXPECT_NO_THROW(project.save(target.Native()));
    EXPECT_NO_THROW(pl::Project::load(target.Native()));
    EXPECT_FALSE(std::filesystem::exists(writing));
    std::filesystem::create_directory(writing);save(Path::FromResolved(writing/"foreign"),"keep");
    EXPECT_THROW(project.save(target.Native()),std::runtime_error);EXPECT_EQ(load(Path::FromResolved(writing/"foreign")),"keep");
    std::filesystem::remove_all(writing);
}
TEST(CoreSettings, ResetUsesCommandJobAndPreservesOtherSections) {
    CommandRegistry commands;JobSystem jobs({},1);
    auto candidate=ConfigManager::CreateDefaultConfig(scratch("reset"));
    candidate.ui.fontScale=2;candidate.window.title="retain";candidate.jobs.defaultWorkerThreadCount=4;
    const auto result=ResetSettingsSection(commands,jobs,candidate,SettingsSection::Interface);
    ASSERT_TRUE(result);EXPECT_EQ(result->ui.fontScale,1);EXPECT_EQ(result->window.title,"retain");EXPECT_EQ(result->jobs.defaultWorkerThreadCount,4);
    EXPECT_EQ(candidate.ui.fontScale,2);EXPECT_FALSE(jobs.GetAllJobs().empty());jobs.Shutdown();
}
TEST(CoreSettings, UserPreferencesRoundTripSystemAndLimits) {
    auto original=ConfigManager::CreateDefaultConfig(scratch("roundtrip"));
    original.log.level=LogLevel::Debug;original.jobs.defaultWorkerThreadCount=4;original.ui.fontScaleMin=0.8f;
    original.eventQueue.growthStep=64;
    YamlConfigSerializer codec;std::string text,error;
    ASSERT_TRUE(codec.SerializeUserSettings(original,text,error));
    auto loaded=ConfigManager::CreateDefaultConfig(original.directory);
    ASSERT_TRUE(codec.DeserializeUserSettings(text,loaded,error))<<error;
    EXPECT_EQ(loaded.jobs,original.jobs);EXPECT_EQ(loaded.log.level,original.log.level);EXPECT_EQ(loaded.ui,original.ui);EXPECT_EQ(loaded.eventQueue,original.eventQueue);
}
