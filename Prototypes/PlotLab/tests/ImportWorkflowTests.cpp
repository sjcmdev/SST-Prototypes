#include "PlotLabDocument.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <chrono>
#include <thread>

using namespace SST;
using namespace SST::Prototypes::PlotLab;
namespace {
class ImportWorkflowTest : public testing::Test {
protected:
    Ref<CommandRegistry> commands=CreateRef<CommandRegistry>();
    Ref<UndoStack> undo=CreateRef<UndoStack>();
    Document doc{commands,undo,CreateRef<EventBus>()};
    std::filesystem::path folder;
    pl::ImportRecipe recipe;
    void SetUp() override {
        folder=std::filesystem::temp_directory_path()/("plotimpl-c-app-"+std::to_string(doc.project->id()));std::filesystem::create_directories(folder);
        recipe.mapping={{"X","X","V",pl::Role::X},{"Y","Y","A",pl::Role::Y}};
    }
    void TearDown() override {doc.CancelImport();doc.WaitImport();std::filesystem::remove_all(folder);}
    std::filesystem::path file(const char* name,const char* data="X,Y\n[V],[A]\n1,2\n3,4\n") {auto p=folder/name;std::ofstream out(p);out<<data;return p;}
    std::shared_ptr<const ImportResult> preview(std::vector<std::filesystem::path> paths) {ImportRequest r;r.paths=std::move(paths);r.recipe=recipe;doc.ImportCommand(std::move(r));return doc.WaitImport();}
};
}
TEST_F(ImportWorkflowTest, PreviewPublishesNothingAndCommitUsesOneCoreUndo) {
    auto p=preview({file("sample.csv")});ASSERT_EQ(p->items.size(),1u);ASSERT_TRUE(p->items[0].plan->ready());EXPECT_EQ(doc.project->objects().size(),1u);EXPECT_EQ(undo->GetUndoDepth(),0u);
    doc.CommitImport(p);auto result=doc.WaitImport();ASSERT_NE(result->items[0].worksheet,0u);EXPECT_EQ(undo->GetUndoDepth(),1u);EXPECT_EQ(doc.activeSheet,0u);
    auto sheet=result->items[0].worksheet;const auto& values=std::get<pl::Column>(doc.project->object(doc.project->children(sheet).front()).data).values;
    EXPECT_EQ(values,p->items[0].plan->table->columns.front().values);ASSERT_TRUE(undo->Undo());EXPECT_EQ(doc.project->objects().size(),1u);ASSERT_TRUE(undo->Redo());EXPECT_TRUE(doc.project->objects().contains(sheet));
}
TEST_F(ImportWorkflowTest, MixedBatchKeepsValidItemsAndRetryReusesIds) {
    auto paths=std::vector{file("T290.csv"),file("power100.csv"),file("invalid.csv","Frequency,C\n[Hz],[F]\n1,2\n")};
    auto p=preview(paths);ASSERT_TRUE(p->items[0].plan->ready());ASSERT_FALSE(p->items[2].plan->ready());doc.CommitImport(p);auto result=doc.WaitImport();
    ASSERT_EQ(result->items.size(),3u);EXPECT_NE(result->items[0].worksheet,0u);EXPECT_NE(result->items[1].worksheet,0u);EXPECT_EQ(result->items[2].worksheet,0u);EXPECT_EQ(undo->GetUndoDepth(),2u);
    const auto id=result->items[0].worksheet;p=preview(paths);doc.CommitImport(p);result=doc.WaitImport();EXPECT_TRUE(result->items[0].reused);EXPECT_EQ(result->items[0].worksheet,id);EXPECT_EQ(undo->GetUndoDepth(),2u);
    const auto y=doc.project->children(id)[1];doc.Execute(doc.project->setCells(y,0,std::vector<double>{999}));
    p=preview(paths);ASSERT_FALSE(p->items[0].diagnostics.empty());EXPECT_EQ(p->items[0].diagnostics[0].code,pl::ImportError::Collision);
    doc.CommitImport(p);result=doc.WaitImport();EXPECT_EQ(result->items[0].worksheet,0u);EXPECT_DOUBLE_EQ(std::get<std::vector<double>>(std::get<pl::Column>(doc.project->object(y).data).values)[0],999);
}
TEST_F(ImportWorkflowTest, SourceSettingsAndProjectRevisionInvalidatePreview) {
    auto source=file("sample.csv");auto p=preview({source});{std::ofstream out(source,std::ios::app);out<<"5,6\n";}
    doc.CommitImport(p);auto r=doc.WaitImport();ASSERT_FALSE(r->items[0].diagnostics.empty());EXPECT_EQ(r->items[0].diagnostics[0].code,pl::ImportError::Conflict);EXPECT_EQ(doc.project->objects().size(),1u);
    p=preview({source});doc.Create(doc.project->root(),0,"Folder");EXPECT_THROW(doc.CommitImport(p),std::invalid_argument);
    p=preview({source});ImportRequest request;request.paths={source};request.recipe=recipe;request.recipe.version="2";request.publish=true;request.accepted={p->items[0].plan};request.previewProject=p->project;request.previewRevision=p->revision;EXPECT_THROW(doc.ImportCommand(std::move(request)),std::invalid_argument);
}
TEST_F(ImportWorkflowTest, NamingCollisionIsInPreviewAndDoesNotOverwritePreviousResult) {
    doc.Create(doc.project->root(),0,"sample");auto p=preview({file("sample.csv")});ASSERT_EQ(p->items[0].diagnostics.size(),1u);EXPECT_EQ(p->items[0].diagnostics[0].code,pl::ImportError::Collision);
    const auto count=doc.project->objects().size();doc.CommitImport(p);auto r=doc.WaitImport();EXPECT_EQ(r->items[0].worksheet,0u);EXPECT_EQ(doc.project->objects().size(),count);
}
TEST_F(ImportWorkflowTest, CancellationRetainsCompletedBatchMembersAndDoesNotChangeSelection) {
    auto p=preview({file("first.csv"),file("second.csv")});doc.CommitImport(p);
    while(true){auto r=doc.PollImport();if(!r->running || r->items.size()==1)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    doc.CancelImport();auto result=doc.WaitImport();EXPECT_NE(result->items[0].worksheet,0u);EXPECT_EQ(doc.activeSheet,0u);EXPECT_GE(undo->GetUndoDepth(),1u);
}
