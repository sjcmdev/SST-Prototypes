#include <gtest/gtest.h>
#include "IO/PrototypeWorkflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "CapLabCommands.hpp"
#include "TestProject.hpp"
namespace SST::Prototypes::CapLab {
static Path Temp(const char *name) { return Path{FileSystem::TempDirectoryPath()/name}; }
TEST(CapLabDocument, OlderSaveAndStaleOpenKeepNewerRevision) {
    CapLabDocument d; d.project=FixtureProject(); d.Changed(); const auto revision=d.revision;
    d.Changed(); d.Saved(Temp("caplab-saved.json"),revision);
    EXPECT_TRUE(d.dirty);
    EXPECT_FALSE(d.Opened(Project{},Temp("caplab-open.json"),revision)); EXPECT_EQ(d.project.sweeps.size(),1);
    d.Saved(d.path,d.revision); EXPECT_FALSE(d.dirty);
}
TEST(CapLabWorkflow, SaveOpenRoundtripWithoutSource) {
    const auto path=Temp("caplab-roundtrip.json");
    JobContext ctx({}, {}, {}, {}, [] {return false;});
    const auto text=Serialize(FixtureProject());
    DocumentJob save(Operation::Save,text,path); save.Execute(ctx); EXPECT_TRUE(save.saveCommitted);
    DocumentJob open(Operation::Open,"",path); open.Execute(ctx);
    ASSERT_TRUE(open.opened); EXPECT_EQ(Json::parse(Serialize(*open.opened)),Json::parse(text));
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST(CapLabWorkflow, CancelAndSaveFailurePreserveDestination) {
    const auto path=Temp("caplab-cancel.json"); std::string error,text;
    ASSERT_TRUE(TextFileIO::Save(path,"original",error));
    JobContext cancel({}, {}, {}, {}, [] {return true;});
    DocumentJob save(Operation::Save,Serialize(FixtureProject()),path);
    EXPECT_THROW(save.Execute(cancel),JobCancelledException);
    ASSERT_TRUE(TextFileIO::Load(path,text,error)); EXPECT_EQ(text,"original");
    JobContext ctx({}, {}, {}, {}, [] {return false;});
    EXPECT_THROW(save.Execute(ctx),std::exception); EXPECT_FALSE(save.saveCommitted);
    ASSERT_TRUE(TextFileIO::Load(path,text,error)); EXPECT_EQ(text,"original");
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
    DocumentJob invalid(Operation::Save,Serialize(FixtureProject()),Temp(""));
    EXPECT_THROW(invalid.Execute(ctx),std::exception); EXPECT_FALSE(invalid.saveCommitted);
}
TEST(CapLabWorkflow, NewerSchemaCannotBeOverwritten) {
    const auto path=Temp("caplab-future.json"); std::string error,text;
    ASSERT_TRUE(TextFileIO::Save(path,"{\"schema_version\":2}",error));
    JobContext ctx({}, {}, {}, {}, [] {return false;});
    DocumentJob save(Operation::Save,Serialize(FixtureProject()),path);
    EXPECT_THROW(save.Execute(ctx),std::exception);
    ASSERT_TRUE(TextFileIO::Load(path,text,error)); EXPECT_EQ(text,"{\"schema_version\":2}");
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST(CapLabWorkflow, FailedOpenLeavesDocumentAndUndoUnchanged) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto undo=CreateRef<UndoStack>();
    CapLabWorkflow w(jobs,{},undo); w.document.project=FixtureProject();
    const auto before=Serialize(w.document.project);
    const auto path=Temp("caplab-bad-open.json"); std::string error;
    ASSERT_TRUE(TextFileIO::Save(path,"bad json",error));
    w.Start(Operation::Open,path); ASSERT_NE(w.activeJob,0);
    jobs->Shutdown(); w.OnUpdate(0);
    EXPECT_EQ(Serialize(w.document.project),before); EXPECT_FALSE(w.status.empty()); EXPECT_TRUE(w.document.path.Empty());
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST(CapLabWorkflow, StaleOpenIsRejectedOnMainThread) {
    const auto path=Temp("caplab-stale-open.json"); std::string error;
    ASSERT_TRUE(TextFileIO::Save(path,Serialize(FixtureProject()),error));
    auto jobs=CreateRef<JobSystem>(nullptr,1); ASSERT_TRUE(jobs->PauseAllJobs());
    CapLabWorkflow w(jobs); const auto before=w.document.project.engine->id();
    w.Start(Operation::Open,path); ASSERT_NE(w.activeJob,0); w.Changed();
    ASSERT_TRUE(jobs->ResumeAllJobs()); jobs->Shutdown(); w.OnUpdate(0);
    EXPECT_EQ(w.document.project.engine->id(),before); EXPECT_TRUE(w.document.dirty);
    EXPECT_NE(w.status.find("Stale"),std::string::npos);
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST(CapLabWorkflow, SaveUsesSnapshotAndDoesNotClearLaterEdit) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); ASSERT_TRUE(jobs->PauseAllJobs());
    CapLabWorkflow w(jobs); w.document.project=FixtureProject(); w.Changed();
    const auto path=Temp("caplab-save-snapshot.json");
    w.Start(Operation::Save,path); ASSERT_NE(w.activeJob,0); w.Changed();
    ASSERT_TRUE(jobs->ResumeAllJobs()); jobs->Shutdown(); w.OnUpdate(0);
    EXPECT_TRUE(w.document.dirty); EXPECT_EQ(w.document.path,path);
    std::string text,error; ASSERT_TRUE(TextFileIO::Load(path,text,error)); EXPECT_EQ(Parse(text).sweeps.size(),1);
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST(CapLabWorkflow, CancellationAfterReplacementIsCommitted) {
    bool cancel=false; JobContext ctx([&](float done,float) {cancel=done==1;}, {}, {}, {}, [&] {return cancel;});
    const auto path=Temp("caplab-committed.json");
    DocumentJob save(Operation::Save,Serialize(FixtureProject()),path); save.Execute(ctx);
    EXPECT_TRUE(cancel); EXPECT_TRUE(save.saveCommitted); EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST(CapLabCommands, ConfirmImportUsesCoreUndoAndRejectsStalePreview) {
    auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>(); auto registry=CreateRef<CommandRegistry>();
    CapLabWorkflow w(CreateRef<JobSystem>(nullptr,1),bus,undo); RegisterCommands(registry,bus,w);
    w.preview=FixturePreview(); w.temperature=100;
    ASSERT_TRUE(registry->Execute(CommandID{"caplab.import"}));
    EXPECT_EQ(w.document.project.sweeps.size(),1); EXPECT_TRUE(undo->CanUndo());
    ASSERT_TRUE(undo->Undo()); EXPECT_TRUE(w.document.project.sweeps.empty());
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(w.document.project.sweeps.size(),1);
    w.preview=FixturePreview(); w.previewRevision=0;
    EXPECT_FALSE(registry->Execute(CommandID{"caplab.import"})); EXPECT_EQ(w.document.project.sweeps.size(),1);
}
TEST(CapLabWorkflow, QueuedCancelDoesNotPublishOpen) {
    const auto path=Temp("caplab-queued-open.json"); std::string error;
    ASSERT_TRUE(TextFileIO::Save(path,Serialize(FixtureProject()),error));
    auto jobs=CreateRef<JobSystem>(nullptr,1); ASSERT_TRUE(jobs->PauseAllJobs());
    CapLabWorkflow w(jobs); const auto id=w.document.project.engine->id();
    w.Start(Operation::Open,path); ASSERT_NE(w.activeJob,0);
    ASSERT_TRUE(jobs->RequestCancel(w.activeJob));
    ASSERT_TRUE(jobs->ResumeAllJobs()); jobs->Shutdown(); w.OnUpdate(0);
    EXPECT_EQ(w.document.project.engine->id(),id); EXPECT_EQ(w.status,"Cancelled.");
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST(CapLabWorkflow, CompletionWaitsForUpdateAndReportsProgress) {
    const auto path=Temp("caplab-main-thread-open.json"); std::string error;
    ASSERT_TRUE(TextFileIO::Save(path,Serialize(FixtureProject()),error));
    auto jobs=CreateRef<JobSystem>(nullptr,1); CapLabWorkflow w(jobs);
    w.Start(Operation::Open,path); const auto job=w.activeJob; ASSERT_NE(job,0);
    jobs->Shutdown();
    EXPECT_TRUE(w.document.project.sweeps.empty());
    const auto progress=jobs->GetJob(job); ASSERT_TRUE(progress);
    EXPECT_EQ(progress->totalWork,1); EXPECT_EQ(progress->completedWork,1);
    w.OnUpdate(0); EXPECT_EQ(w.document.project.sweeps.size(),1); EXPECT_FALSE(w.document.dirty);
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}

}
