#include "ProjectFiles.hpp"
#include "PlotLabDocument.hpp"
#include "PlotWorkflow.hpp"
#include "WorksheetPython.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#endif

using namespace SST;
using namespace SST::Prototypes::PlotLab;
PythonInterpreter& TestPython();
namespace {
[[noreturn]] void KillSave() {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(),77);
#endif
    std::_Exit(77);
}
bool WriteGeneration(const std::filesystem::path& file,const std::string& name) {
    pl::Project(name).save(file);
    WorkflowState state;state.recipe.name=name;SaveWorkflowState(file,state);
    pl::ImportRecipe recipe;recipe.name=name;
    auto imports=file;imports+=".imports.json";
    ImportUiState ui;ui.recipe=recipe;SaveImportPresets(imports,{{name,recipe}},&ui);
    return true;
}
class ProjectFilesDeathTest:public testing::Test {
protected:
    std::filesystem::path directory=std::filesystem::temp_directory_path()/"plotint2-save-set";
    std::filesystem::path file=directory/"project.plotlab";
    void SetUp() override {
        ASSERT_TRUE(TestPython().Start());
        std::filesystem::remove_all(directory);std::filesystem::create_directory(directory);
        SaveProjectFiles(file,[](const auto& staged){return WriteGeneration(staged,"old");});
    }
    void TearDown() override {std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
    void Check(const std::string& name) {
        auto bus=CreateRef<EventBus>();Document doc(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus);
        doc.Open(file);
        EXPECT_EQ(doc.project->object(doc.project->root()).name,name);
        EXPECT_EQ(doc.workflowState.recipe.name,name);
        ASSERT_EQ(doc.importPresets.size(),1);EXPECT_TRUE(doc.importPresets.contains(name));
        EXPECT_FALSE(RecoverProjectFiles(file));
    }
};
}
class ProjectFilesCommitDeathTest:public ProjectFilesDeathTest,public testing::WithParamInterface<unsigned> {};
TEST_P(ProjectFilesCommitDeathTest,KilledSaveRecoversOneGenerationAtEveryCommitBoundary) {
        const auto point=GetParam();
        SCOPED_TRACE(point);
        SaveProjectFiles(file,[](const auto& staged){return WriteGeneration(staged,"old");});
        EXPECT_EXIT(SaveProjectFiles(file,[](const auto& staged){return WriteGeneration(staged,"new");},[&](unsigned committed) {
            if(committed==point)KillSave();
        }),testing::ExitedWithCode(77),"");
        auto pending=file;pending+=".pending-save";ASSERT_TRUE(std::filesystem::exists(pending/"ready"));
        Check("new");EXPECT_FALSE(std::filesystem::exists(pending));
}
INSTANTIATE_TEST_SUITE_P(CommitBoundaries,ProjectFilesCommitDeathTest,testing::Values(0u,1u,2u,3u));
TEST_F(ProjectFilesDeathTest,FailedPreparationKeepsPreviousSetAndNextSaveDiscardsOnlyUnpreparedFiles) {
    EXPECT_THROW(SaveProjectFiles(file,[](const auto& staged)->bool {
        pl::Project("incomplete").save(staged);throw std::runtime_error("sidecar write failed");
    }),std::runtime_error);
    Check("old");
    SaveProjectFiles(file,[](const auto& staged){return WriteGeneration(staged,"next");});Check("next");
}
TEST_F(ProjectFilesDeathTest,InterruptedPreparationNeverPublishesAndMalformedOrIncompleteManifestIsRetained) {
    EXPECT_EXIT(SaveProjectFiles(file,[](const auto& staged)->bool {
        pl::Project("incomplete").save(staged);
        KillSave();
    }),testing::ExitedWithCode(77),"");
    Check("old");
    auto pending=file;pending+=".pending-save";
    {std::ofstream out(pending/"ready");out<<"unknown generation";}
    EXPECT_THROW(RecoverProjectFiles(file),std::runtime_error);
    {std::ofstream out(pending/"ready");out<<"sst.plotlab.save-set.v1\nimports=1\n";}
    EXPECT_THROW(RecoverProjectFiles(file),std::runtime_error);
    EXPECT_TRUE(std::filesystem::exists(pending/"ready"));
    const auto original=pl::Project::load(file);EXPECT_EQ(original.object(original.root()).name,"old");
}
TEST_F(ProjectFilesDeathTest,ConcurrentOpenAndSaveCannotMixGenerations) {
    ProjectFilesLock lock(file);
    std::thread other([&] {
        EXPECT_THROW(SaveProjectFiles(file,[](const auto& staged){return WriteGeneration(staged,"concurrent");}),std::runtime_error);
        EXPECT_THROW(RecoverProjectFiles(file),std::runtime_error);
    });
    other.join();Check("old");
}
TEST_F(ProjectFilesDeathTest,RecoveryCapturesFrozenWorkflowImportsViewsAndPythonWithoutChangingDocument) {
    auto bus=CreateRef<EventBus>();Document doc(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus);
    auto sidecars=LoadProjectSidecars(file);pl::ImportRecipe imported;imported.name="history recipe";
    const auto fingerprint=pl::importRecipeFingerprint(imported);sidecars.recipes[fingerprint]=std::make_shared<const pl::ImportRecipe>(imported);
    doc.Open(CreateRef<pl::Project>(pl::Project::load(file)),file,std::move(sidecars));
    doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Data",pl::CsvImporter{}.read("X,Y\n0,1\n1,2\n2,3\n")));
    const auto book=doc.project->children(doc.project->root()).front();
    const auto sheet=doc.project->children(book).front();const auto columns=doc.project->children(sheet);
    const auto graph=doc.AddGraph(doc.project->root(),sheet,columns[0],columns[1]);
    const auto font=directory/"custom.ttf";{std::ofstream out(font);out<<"recovery font bytes";}
    auto graphObject=doc.project->object(graph);std::get<pl::Graph>(graphObject.data).fontFile=Path::FromResolved(font).Utf8();doc.Execute(doc.project->change("Custom font",{std::move(graphObject)}));
    const auto script=doc.SaveScript("Draft","original");
    doc.scriptDrafts[script]="recovered";doc.scriptParameterDrafts[script]={{"gain",2}};doc.pythonHistory={"history"};
    auto& workflow=doc.Workflow();auto recipe=workflow.panel.Recipe();recipe.name="unsaved analysis";recipe.worksheet=sheet;
    workflow.panel.SetRecipe(recipe);workflow.panel.open=true;workflow.panel.previewView.viewport=plot::Bounds{0,4,0,5};
    doc.workflowState.acceptedRecipes[book]=std::make_shared<const pl::AnalysisRecipe>(recipe);
    doc.graphViews[graph].viewport=plot::Bounds{.2,1.5,1.2,2.5};
    auto preset=doc.importPresets.at("old");preset.name="unsaved import";doc.importPresets.clear();doc.importPresets.emplace(preset.name,preset);doc.importUi.open=true;doc.importUi.paths={directory/"input.csv"};
    const auto revision=doc.project->revision();const auto depth=doc.undo->GetUndoDepth();
    auto write=doc.RecoverySnapshot();
    EXPECT_EQ(doc.path,file);EXPECT_EQ(doc.project->revision(),revision);EXPECT_EQ(doc.undo->GetUndoDepth(),depth);
    EXPECT_EQ(std::get<pl::Script>(doc.project->object(script).data).source,"original");EXPECT_FALSE(doc.scriptDrafts.empty());
    workflow.panel.SetRecipe({});doc.importPresets.clear();doc.importUi={};doc.graphViews.clear();doc.scriptDrafts.clear();doc.pythonHistory.clear();
    const auto recovery=directory/"recovery.plotlab";write(Path::FromResolved(recovery));std::filesystem::remove(font);
    Document restored(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus);restored.Open(recovery);
    restored.path.clear();restored.recovered=true;
    EXPECT_TRUE(restored.Dirty());EXPECT_FALSE(restored.PythonTrusted());
    EXPECT_EQ(restored.workflowState.recipe,recipe);EXPECT_TRUE(restored.Workflow().panel.open);
    ASSERT_TRUE(restored.Workflow().panel.previewView.viewport);EXPECT_EQ(restored.Workflow().panel.previewView.viewport->maxX,4);
    ASSERT_EQ(restored.workflowState.acceptedRecipes.size(),1);EXPECT_EQ(*restored.workflowState.acceptedRecipes.at(book),recipe);
    ASSERT_EQ(restored.importPresets.size(),1);EXPECT_EQ(restored.importPresets.at("unsaved import").name,"unsaved import");
    ASSERT_EQ(restored.ImportRecipeHistory().size(),1);EXPECT_EQ(*restored.ImportRecipeHistory().at(fingerprint),imported);
    EXPECT_TRUE(restored.importUi.open);EXPECT_EQ(restored.importUi.paths,(std::vector<std::filesystem::path>{directory/"input.csv"}));
    EXPECT_EQ(restored.SnapshotGraph(graph)->ranges.minX,.2);
    const auto& recoveredScript=std::get<pl::Script>(restored.project->object(script).data);
    EXPECT_EQ(recoveredScript.source,"recovered");EXPECT_EQ(recoveredScript.parameters.at("gain"),2);EXPECT_EQ(restored.pythonHistory,(std::vector<std::string>{"history"}));
    const auto saved=directory/"saved.plotlab";restored.Save(saved);EXPECT_FALSE(restored.Dirty());EXPECT_FALSE(restored.recovered);
    auto fonts=recovery;fonts+=".fonts";std::filesystem::remove_all(fonts);
    Document reopened(CreateRef<CommandRegistry>(),CreateRef<UndoStack>(),bus);reopened.Open(saved);
    EXPECT_EQ(reopened.workflowState.recipe,recipe);EXPECT_EQ(reopened.importPresets.at("unsaved import").name,"unsaved import");EXPECT_EQ(reopened.SnapshotGraph(graph)->ranges.minX,.2);
    EXPECT_EQ(*reopened.ImportRecipeHistory().at(fingerprint),imported);
    EXPECT_TRUE(std::filesystem::is_regular_file(std::filesystem::u8path(std::get<pl::Graph>(reopened.project->object(graph).data).fontFile)));
    restored.recovered=true;restored.New();EXPECT_FALSE(restored.recovered);EXPECT_FALSE(restored.Dirty());
    Check("old");
}
