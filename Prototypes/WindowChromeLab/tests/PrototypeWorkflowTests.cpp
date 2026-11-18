#include <gtest/gtest.h>
#include "IO/PrototypeWorkflow.hpp"
#include "Core/IO/TextFileIO.hpp"

namespace SST::Prototypes::WindowChromeLab
{
    TEST(PrototypeDocument, OlderSaveDoesNotClearNewerEdit)
    {
        PrototypeDocument doc;
        doc.Edit({2, 300});
        const auto saved = doc.revision;
        doc.Edit({3, 300});
        doc.Saved(Path{"example.json"}, saved);
        EXPECT_TRUE(doc.dirty);
        EXPECT_EQ(doc.path, Path{"example.json"});
        doc.Saved(doc.path, doc.revision);
        EXPECT_FALSE(doc.dirty);
    }

    TEST(PrototypeDocument, OpenDoesNotOverwriteAnEditMadeDuringLoad)
    {
        PrototypeDocument doc;
        doc.Edit({3, 300});
        EXPECT_FALSE(doc.Opened({2, 300}, Path{"example.json"}, 0));
        EXPECT_DOUBLE_EQ(doc.input.voltage, 3);
        EXPECT_TRUE(doc.dirty);
    }

    TEST(PrototypeDocument, ValidatesSchemaAndPhysicalInputs)
    {
        EXPECT_DOUBLE_EQ(ParseInput(SerializeInput({2, 350})).temperatureKelvin, 350);
        EXPECT_THROW(ParseInput("{}"), std::exception);
        EXPECT_THROW(ParseInput("{\"schema_version\":2}"), std::exception);
        EXPECT_THROW(SerializeInput({2, 0}), std::exception);
        EXPECT_THROW(SerializeInput({INFINITY, 300}), std::exception);
    }

    TEST(PrototypeWorkflow, JobsRoundtripAndFailures)
    {
        const auto path = Path{FileSystem::TempDirectoryPath() / ("WindowChromeLab-roundtrip.json")};
        JobContext context({}, {}, {}, {}, [] { return false; });
        DocumentJob save(Operation::Save, {2, 350}, path);
        save.Execute(context);
        EXPECT_TRUE(save.saveCommitted);
        DocumentJob open(Operation::Open, {}, path);
        open.Execute(context);
        EXPECT_DOUBLE_EQ(open.input.voltage, 2);
        EXPECT_DOUBLE_EQ(open.input.temperatureKelvin, 350);
        EXPECT_TRUE(FileSystem::Remove(path.Native()));
        EXPECT_THROW(open.Execute(context), std::runtime_error);
    }

    TEST(PrototypeWorkflow, CancellationPreservesExistingFile)
    {
        const Path path{FileSystem::TempDirectoryPath() / "WindowChromeLab-cancel.json"};
        std::string error, text;
        ASSERT_TRUE(TextFileIO::Save(path, "original", error));
        JobContext context({}, {}, {}, {}, [] { return true; });
        DocumentJob save(Operation::Save, {2, 300}, path);
        EXPECT_THROW(save.Execute(context), JobCancelledException);
        ASSERT_TRUE(TextFileIO::Load(path, text, error));
        EXPECT_EQ(text, "original");
        EXPECT_TRUE(FileSystem::Remove(path.Native()));
    }

    TEST(PrototypeWorkflow, SaveFailureDoesNotCommit)
    {
        JobContext context({}, {}, {}, {}, [] { return false; });
        DocumentJob save(Operation::Save, {2, 300}, Path{FileSystem::TempDirectoryPath()});
        EXPECT_THROW(save.Execute(context), std::runtime_error);
        EXPECT_FALSE(save.saveCommitted);
    }

    TEST(PrototypeWorkflow, EditWhileSavingKeepsDirtyAndSavesSnapshot)
    {
        auto jobs = CreateRef<JobSystem>(nullptr, 1);
        ASSERT_TRUE(jobs->PauseAllJobs());
        PrototypeWorkflow workflow(jobs);
        workflow.document.path = Path{FileSystem::TempDirectoryPath() / "WindowChromeLab-snapshot.json"};
        workflow.document.Edit({2, 300});
        workflow.Start(Operation::Save);
        ASSERT_NE(workflow.activeJob, 0);
        workflow.document.Edit({3, 300});
        ASSERT_TRUE(jobs->ResumeAllJobs());
        jobs->Shutdown();
        workflow.OnUpdate(0);
        EXPECT_TRUE(workflow.document.dirty);
        std::string text, error;
        ASSERT_TRUE(TextFileIO::Load(workflow.document.path, text, error));
        EXPECT_DOUBLE_EQ(ParseInput(text).voltage, 2);
        EXPECT_TRUE(FileSystem::Remove(workflow.document.path.Native()));
    }

    TEST(PrototypeWorkflow, CancellationAfterReplacementStillRecordsSave)
    {
        bool cancel = false;
        JobContext context([&](float completed, float) { cancel = completed == 2; }, {}, {}, {}, [&] { return cancel; });
        const Path path{FileSystem::TempDirectoryPath() / "WindowChromeLab-committed.json"};
        DocumentJob save(Operation::Save, {2, 300}, path);
        save.Execute(context);
        EXPECT_TRUE(cancel);
        EXPECT_TRUE(save.saveCommitted);
        EXPECT_TRUE(FileSystem::Remove(path.Native()));
    }

    TEST(PrototypeWorkflow, JobSystemAppliesCompletionOnUpdate)
    {
        auto jobs = CreateRef<JobSystem>(nullptr, 1);
        PrototypeWorkflow workflow(jobs);
        workflow.document.Edit({2, 300});
        workflow.Start(Operation::Evaluate);
        ASSERT_NE(workflow.activeJob, 0);
        jobs->Shutdown();
        workflow.OnUpdate(0);
        EXPECT_EQ(workflow.activeJob, 0);
        EXPECT_NEAR(workflow.document.result.conductivity, 4.4860945752, 1e-12);
    }
    TEST(PrototypeWorkflow, WindowLayoutTravelsWithDocumentJob)
    {
        const Path path{FileSystem::TempDirectoryPath() / "window-layout-roundtrip.json"};
        JobContext context({}, {}, {}, {}, [] { return false; });
        DocumentJob save(Operation::Save, {2, 350}, path);
        save.windowLayout = R"({"version":1,"windows":[]})";
        save.Execute(context);
        DocumentJob open(Operation::Open, {}, path); open.Execute(context);
        EXPECT_EQ(open.windowLayout, save.windowLayout);
        EXPECT_TRUE(FileSystem::Remove(path.Native()));
    }

}
