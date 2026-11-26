#include "ProjectFiles.hpp"
#include "PlotLabDocument.hpp"
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
