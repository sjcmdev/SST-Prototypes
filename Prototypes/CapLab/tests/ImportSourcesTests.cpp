#include <gtest/gtest.h>
#include "Core/IO/ImportPreview.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/Presentation/Panels/ImportSourceList.hpp"
#include "IO/PrototypeWorkflow.hpp"
#include "CapLabCommands.hpp"
#include "TestProject.hpp"
#include <chrono>
#include <imgui_internal.h>
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
        int calls=0;
        EXPECT_THROW(PreviewSources<int>(context,sources,[&](const ImportSource &) -> Result<int> {
            ++calls; return 1;
        }),JobCancelledException);
        EXPECT_EQ(calls,0);
    }

    TEST(ImportSourceList, DuplicateNamesHaveDistinctIdsAndErrorsRemainVisible)
    {
        auto *gui=ImGui::CreateContext();
        auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1200,800}; io.DeltaTime=1.f/60;
        io.ConfigDebugHighlightIdConflicts=true;
        unsigned char *pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        const std::vector<ImportSourceRow> rows={{"a.dat","first/a.dat",{}},{"a.dat","second/a.dat","unreadable"}};
        std::vector<int> selected={0}; int focused=1,details=0;
        for (int frame=0;frame<2;++frame) {
            ImGui::NewFrame(); ImGui::SetNextWindowSize({1100,700}); ImGui::Begin("Sources");
            RenderImportSources(rows,selected,focused,[&](int index) { EXPECT_EQ(index,1); ++details; });
            ImGui::End(); ImGui::Render();
            EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
        }
        EXPECT_EQ(details,2); EXPECT_EQ(selected,std::vector<int>{0});
        ImGui::DestroyContext(gui);
    }

    TEST_F(ImportSourcesTest, BatchCommitIsAtomicAndOneUndoStep)
    {
        auto jobs=CreateRef<JobSystem>(nullptr,1);
        auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
        CapLabWorkflow state(jobs,bus,undo); RegisterCommands(commands,bus,state);
        const auto a=Write("a.dat"), b=Write("b.dat");
        state.sourcePreviews={{{a,root},FixturePreview(),{}},{{b,root},FixturePreview(),{}}};
        state.selectedSources={0,1};
        const auto before=Serialize(state.Document().project);
        ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"}));
        EXPECT_EQ(state.Document().project.sweeps.size(),2); const auto after=Serialize(state.Document().project);
        ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(state.Document().project),before); EXPECT_FALSE(undo->CanUndo());
        ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(state.Document().project),after);
        state.sourcePreviews={{{a,root},FixturePreview(),{}},{{b,root},Json::object(),{}}};
        state.selectedSources={0,1}; state.previewRevision=state.Document().revision;
        EXPECT_FALSE(commands->Execute(CommandID{"caplab.import"})); EXPECT_EQ(Serialize(state.Document().project),after);
    }

    TEST_F(ImportSourcesTest, SwitchingProjectsClearsBatchPreviewAndExportSelection)
    {
        auto jobs=CreateRef<JobSystem>(nullptr,1);
        CapLabWorkflow state(jobs);
        const auto first=state.workspace.active;
        state.Document().project=FixtureProject();
        auto layer=state.Document().project.engine->object(state.Document().project.engine->children(state.Document().project.graph).at(0));
        auto &view=std::get<pl::Layer>(layer.data).view;
        view.viewport=sst::ui::plot::Bounds{1,2,3,4};
        state.Document().project.engine->change("View",{std::move(layer)}).apply(*state.Document().project.engine);
        const auto generation=state.previewGeneration.Next();
        const auto source=Write("a.dat");
        state.sourcePreviews={{{source,root},FixturePreview(),{}}}; state.selectedSources={0};
        state.exportGraphs={state.Document().project.graph}; state.graphViews[state.Document().project.graph]={};
        state.Start(Operation::New);
        EXPECT_TRUE(state.sourcePreviews.empty()); EXPECT_TRUE(state.selectedSources.empty());
        EXPECT_FALSE(state.previewGeneration.IsCurrent(generation));
        EXPECT_TRUE(state.exportGraphs.empty()); EXPECT_TRUE(state.graphViews.empty());
        ASSERT_TRUE(state.Activate(first)); ASSERT_TRUE(state.view.viewport);
        EXPECT_DOUBLE_EQ(state.view.viewport->minX,1); EXPECT_DOUBLE_EQ(state.view.viewport->maxY,4);
    }

    class FixtureImporter final : public LabASImporter
    {
    public:
        bool recognizes(std::string_view, std::string_view) const override {return true;}
        Json preview(std::string_view,const Path &,char,const Json &,JobContext &) const override
        { return FixturePreview(); }
    };

#ifdef _WIN32
    TEST_F(ImportSourcesTest, InaccessibleFileIsReportedAlongsideReadableFile)
    {
        const auto good=Write("a.dat"), locked=Write("b.dat");
        const HANDLE file=CreateFileW(locked.Native().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
        ASSERT_NE(file,INVALID_HANDLE_VALUE);
        struct Close { HANDLE file; ~Close() {CloseHandle(file);} } close{file};
        DocumentJob job(Operation::Preview,Project{}); job.sources={good,locked};
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

    TEST_F(ImportSourcesTest, OpenOrImportStartsANewPreviewGenerationAfterDiscard)
    {
        auto jobs=CreateRef<JobSystem>(nullptr,1);
        CapLabWorkflow state(jobs); state.labImporter=std::make_shared<FixtureImporter>();
        state.ClearPreview();
        state.Start(Operation::OpenOrImport,Write("a.dat")); ASSERT_NE(state.activeJob,0);
        jobs->Shutdown(); state.OnUpdate(0);
        ASSERT_TRUE(state.preview) << state.status;
        ASSERT_EQ(state.sourcePreviews.size(),1);
    }

    TEST_F(ImportSourcesTest, EachPreviewRequiresFreshMetadataConfirmation)
    {
        for (auto operation : {Operation::Preview,Operation::OpenOrImport}) {
            auto jobs=CreateRef<JobSystem>(nullptr,1);
            CapLabWorkflow state(jobs); state.labImporter=std::make_shared<FixtureImporter>();
            state.metadataConfirmed=true;
            state.Start(operation,Write("a.dat")); ASSERT_NE(state.activeJob,0);
            EXPECT_FALSE(state.metadataConfirmed);
            jobs->Shutdown(); state.OnUpdate(0);
            ASSERT_TRUE(state.preview) << state.status;
            EXPECT_FALSE(state.metadataConfirmed);
        }
    }

    TEST_F(ImportSourcesTest, OpeningProjectClearsBatchPreviewAndRestoresGraphView)
    {
        auto project=FixtureProject();
        const sst::ui::plot::Bounds bounds{100,1000,1e-9,2e-9};
        auto layer=project.engine->object(project.engine->children(project.graph).at(0));
        std::get<pl::Layer>(layer.data).view.viewport=bounds;
        project.engine->change("Saved graph view",{std::move(layer)}).apply(*project.engine);
        const auto path=root.Join("project.caplab.json"); std::string error;
        ASSERT_TRUE(TextFileIO::Save(path,Serialize(project),error)) << error;
        auto jobs=CreateRef<JobSystem>(nullptr,1);
        CapLabWorkflow state(jobs);
        state.sourcePreviews={{{path,root},FixturePreview(),{}}};
        state.selectedSources={0}; state.sourceTemperatures={300}; state.SelectPreview(0);
        state.graphViews[project.graph]={}; state.exportGraphs={project.graph};
        const auto generation=state.previewGeneration.Next();
        state.Start(Operation::Open,path); ASSERT_NE(state.activeJob,0);
        jobs->Shutdown(); state.OnUpdate(0);
        EXPECT_FALSE(state.preview); EXPECT_TRUE(state.sourcePreviews.empty());
        EXPECT_TRUE(state.selectedSources.empty()); EXPECT_TRUE(state.sourceTemperatures.empty());
        EXPECT_EQ(state.previewSourceIndex,-1); EXPECT_FALSE(state.previewGeneration.IsCurrent(generation));
        EXPECT_TRUE(state.graphViews.empty()); EXPECT_TRUE(state.exportGraphs.empty());
        ASSERT_TRUE(state.view.viewport);
        EXPECT_DOUBLE_EQ(state.view.viewport->minX,bounds.minX);
        EXPECT_DOUBLE_EQ(state.view.viewport->maxX,bounds.maxX);
        EXPECT_DOUBLE_EQ(state.view.viewport->minY,bounds.minY);
        EXPECT_DOUBLE_EQ(state.view.viewport->maxY,bounds.maxY);
    }
}
