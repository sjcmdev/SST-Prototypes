#include <gtest/gtest.h>
#include "Core/IO/ImportPreview.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/Presentation/Panels/ImportSourceList.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "CapLabCommands.hpp"
#include "TestProject.hpp"
#include <chrono>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace SST::Prototypes::CapLab
{
    class ImportSourcesTest : public testing::Test
    {
    protected:
        Path root{FileSystem::TempDirectoryPath() / ("clshare2-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))};
        void SetUp() override { ASSERT_TRUE(FileSystem::CreateDirectories(root.Join("nested").Native())); }
        void TearDown() override { FileSystem::RemoveAll(root.Native()); }
        Path Write(const std::filesystem::path &name)
        {
            Path path{root.Native()/name}; std::string error;
            EXPECT_TRUE(TextFileIO::Save(path,"100 1e-9 1e6\n1000 2e-9 1e6",error)) << error;
            return path;
        }
        static bool Data(const Path &p) { return p.extension().Utf8()==".dat"; }
    };

    TEST_F(ImportSourcesTest, StableUnicodeFilesFoldersRecursiveAndDuplicates)
    {
        const auto z=Write("z.dat"), a=Write(std::filesystem::path(u8"ą-測定.dat")), nested=Write("nested/a.dat");
        Write("ignored.txt");
        auto flat=CollectSources(std::vector<Path>{root},false,Data);
        ASSERT_EQ(flat.size(),2); EXPECT_EQ(flat[0].source.path,z); EXPECT_EQ(flat[1].source.path,a);
        EXPECT_EQ(flat[0].source.root,root);
        auto tree=CollectSources(std::vector<Path>{root,z,nested},true,Data);
        ASSERT_EQ(tree.size(),3); EXPECT_EQ(tree[0].source.path,nested);
        EXPECT_EQ(ImportSourceLabel(tree[0].source),Path{std::filesystem::path("nested")/"a.dat"}.Utf8());
        for (const auto &entry : tree) EXPECT_FALSE(entry.error);
        auto files=CollectSources(std::vector<Path>{a,z,a},false,Data);
        ASSERT_EQ(files.size(),2); EXPECT_TRUE(files[0].source.root.Empty());
    }

    TEST_F(ImportSourcesTest, MissingSourceAndParserErrorsDoNotHideOtherFiles)
    {
        const auto good=Write("a.dat"), bad=Write("b.dat"), missing=root.Join("missing.dat");
        const auto sources=CollectSources(std::vector<Path>{missing,bad,good},false,Data);
        ASSERT_EQ(sources.size(),3); ASSERT_TRUE(sources.back().error);
        EXPECT_EQ(sources.back().error->code,"import.source_unreadable");
        int calls=0; float progress=0;
        JobContext context([&](float done,float total) { progress=done/total; },{},{},{},[] {return false;});
        const auto items=PreviewSources<int>(context,sources,[&](const ImportSource &source) -> Result<int> {
            ++calls;
            if (source.path==bad) throw std::runtime_error("invalid file");
            return 7;
        });
        EXPECT_EQ(calls,2); ASSERT_EQ(items.size(),3); EXPECT_EQ(items[0].value,7);
        EXPECT_EQ(items[1].error->code,"import.preview_failed"); EXPECT_TRUE(items[2].error);
        EXPECT_FLOAT_EQ(progress,1);
    }

    TEST_F(ImportSourcesTest, CancellationAfterLastParserDoesNotPublish)
    {
        const auto sources=CollectSources(std::vector<Path>{Write("a.dat")},false,Data);
        bool cancelled=false;
        JobContext context({},{},{},{},[&] {return cancelled;});
        EXPECT_THROW(PreviewSources<int>(context,sources,[&](const ImportSource &) -> Result<int> {
            cancelled=true; return 1;
        }),JobCancelledException);
        PreviewGeneration generation; const auto old=generation.Next();
        EXPECT_TRUE(generation.IsCurrent(old)); generation.Next(); EXPECT_FALSE(generation.IsCurrent(old));
    }

    TEST_F(ImportSourcesTest, BatchCommitIsAtomicAndOneUndoStep)
    {
        auto jobs=CreateRef<JobSystem>(nullptr,1);
        auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
        CapLabWorkflow state(jobs,bus,undo); RegisterCommands(commands,bus,state);
        const auto a=Write("a.dat"), b=Write("b.dat");
        state.sourcePreviews={{{a,root},FixturePreview(),{}},{{b,root},FixturePreview(),{}}};
        state.selectedSources={0,1};
        const auto before=Serialize(state.document.project);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"}));
        EXPECT_EQ(state.document.project.sweeps.size(),2); const auto after=Serialize(state.document.project);
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(state.document.project),before); EXPECT_FALSE(undo->CanUndo());
        ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(state.document.project),after);
        state.sourcePreviews={{{a,root},FixturePreview(),{}},{{b,root},Json::object(),{}}};
        state.selectedSources={0,1}; state.previewRevision=state.document.revision;
        EXPECT_FALSE(commands->Execute(CommandID{"caplab.import"})); EXPECT_EQ(Serialize(state.document.project),after);
    }

    class FixtureImporter final : public pl::Importer
    {
    public:
        std::string typeId() const override {return "fixture";}
        unsigned version() const override {return 1;}
        bool recognizes(std::string_view, std::string_view) const override {return true;}
        pl::Table preview(std::string_view,const pl::ImportOptions &,const pl::WorkContext &) const override
        { pl::Table table; table.source["preview_json"]=FixturePreview().dump(); return table; }
        pl::Table read(std::string_view text,const pl::ImportOptions &options,const pl::WorkContext &work) const override
        {return preview(text,options,work);}
    };

#ifdef _WIN32
    TEST_F(ImportSourcesTest, InaccessibleFileIsReportedAlongsideReadableFile)
    {
        const auto good=Write("a.dat"), locked=Write("b.dat");
        const HANDLE file=CreateFileW(locked.Native().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
        ASSERT_NE(file,INVALID_HANDLE_VALUE);
        struct Close { HANDLE file; ~Close() {CloseHandle(file);} } close{file};
        DocumentJob job(Operation::Preview,""); job.sources={good,locked};
        job.importer=std::make_shared<FixtureImporter>();
        JobContext context({},{},{},{},[] {return false;});
        ASSERT_NO_THROW(job.Execute(context)); ASSERT_EQ(job.sourcePreviews.size(),2);
        EXPECT_TRUE(job.sourcePreviews[0].value); EXPECT_TRUE(job.sourcePreviews[1].error);
    }
#endif

    TEST_F(ImportSourcesTest, DiscardedGenerationAndChangedRevisionRejectPreview)
    {
        for (bool revision : {false,true}) {
            auto jobs=CreateRef<JobSystem>(nullptr,1); ASSERT_TRUE(jobs->PauseAllJobs());
            CapLabWorkflow state(jobs); state.labImporter=std::make_shared<FixtureImporter>();
            state.Start(Operation::Preview,Write("a.dat")); ASSERT_NE(state.activeJob,0);
            if (revision) state.Changed(); else state.ClearPreview();
            ASSERT_TRUE(jobs->ResumeAllJobs()); jobs->Shutdown(); state.OnUpdate(0);
            EXPECT_FALSE(state.preview); EXPECT_TRUE(state.sourcePreviews.empty());
            EXPECT_NE(state.status.find("Stale"),std::string::npos);
        }
    }
}
