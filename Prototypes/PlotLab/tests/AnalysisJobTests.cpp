#include "AnalysisJobs.hpp"
#include "AnalysisPanel.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <imgui_internal.h>
#include <implot.h>

using namespace SST;
using namespace SST::Prototypes::PlotLab;
class PlotGapAnalysisJobs : public testing::Test {
protected:
    Ref<CommandRegistry> commands=CreateRef<CommandRegistry>();
    Ref<UndoStack> undo=CreateRef<UndoStack>();
    Ref<EventBus> bus=CreateRef<EventBus>();
    Ref<JobSystem> jobs=CreateRef<JobSystem>(bus,2);
    Document doc{commands,undo,bus,jobs};
    AnalysisController analysis{doc,jobs};
    pl::AnalysisRecipe recipe;
    void SetUp() override {
        doc.Execute(pl::importTable(*doc.project,doc.project->root(),"Data",pl::CsvImporter{}.read("X,Y\ns,V\n0,2\n1,5\n2,8\n3,11\n")));
        auto book=doc.project->children(doc.project->root()).front();recipe.worksheet=doc.project->children(book).front();
        auto columns=doc.project->children(recipe.worksheet);recipe.xColumn=columns[0];recipe.yColumn=columns[1];recipe.id="owner-recipe";
        undo->Clear();undo->MarkClean();
    }
    void TearDown() override {analysis.Cancel();jobs->Shutdown(true);}
};
TEST_F(PlotGapAnalysisJobs, CoreCommandSharedPoolTypedParityAndOneAcceptUndo) {
    std::vector<std::string> commandsSeen;
    auto observer=commands->AddObserver([&](const CommandExecutionEvent& event){if(event.state==CommandExecutionState::Succeeded)commandsSeen.push_back(event.id.value);});
    const auto source=CaptureAnalysisSource(*doc.project,recipe);const auto direct=pl::analyze(source.View(),recipe.settings);
    const auto sourceX=source.View().x.data();EXPECT_EQ(sourceX,std::get<std::vector<double>>(std::get<pl::Column>(source.x->data).values).data());
    auto id=analysis.Submit(recipe);ASSERT_NE(id,0);ASSERT_EQ(analysis.Wait().stage,AnalysisStage::PreviewReady);
    auto result=analysis.Status().preview;ASSERT_TRUE(result);const auto& actual=*result->items.front().result;
    EXPECT_EQ(actual.parameters,direct.parameters);EXPECT_EQ(actual.values,direct.values);EXPECT_EQ(actual.rowIds,direct.rowIds);
    EXPECT_EQ(result->snapshotNumericCopiedBytes,0);EXPECT_EQ(actual.copiedInputBytes,64);EXPECT_EQ(undo->GetUndoDepth(),0);
    std::cout<<"ANALYSIS copied_source_numeric_bytes="<<actual.copiedInputBytes<<" snapshot_numeric_bytes="<<result->snapshotNumericCopiedBytes<<" mask_bytes="<<result->maskCopiedBytes<<'\n';
    EXPECT_NE(std::find(commandsSeen.begin(),commandsSeen.end(),"plotlab.analysis.run"),commandsSeen.end());
    for(int i=0;i<100;++i)EXPECT_EQ(analysis.Poll().preview.get(),result.get());
    analysis.Accept();EXPECT_EQ(undo->GetUndoDepth(),1);ASSERT_EQ(analysis.Status().outputWorksheets.size(),2);
    const auto output=analysis.Status().outputWorksheets.front();const auto rows=std::get<pl::Worksheet>(doc.project->object(output).data).rows;
    const auto values=doc.project->children(output);EXPECT_EQ(analysis.Status().publishedCopiedBytes,160);EXPECT_TRUE(actual.normalizedResiduals.empty());
    ASSERT_TRUE(undo->Undo());EXPECT_THROW(doc.project->object(output),std::exception);ASSERT_TRUE(undo->Redo());
    EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(output).data).rows,rows);EXPECT_EQ(doc.project->children(output),values);
    std::cout<<"ANALYSIS publication_copy_bytes="<<analysis.Status().publishedCopiedBytes<<'\n';
    commands->RemoveObserver(observer);
}
TEST_F(PlotGapAnalysisJobs, StaleFailureCancelAndProjectReplacementKeepLastResult) {
    analysis.Submit(recipe);ASSERT_EQ(analysis.Wait().stage,AnalysisStage::PreviewReady);auto previous=analysis.Status().preview;
    analysis.Submit(recipe);doc.SetValues(recipe.yColumn,std::vector<double>{4,7,10,13});
    EXPECT_EQ(analysis.Wait().stage,AnalysisStage::Stale);EXPECT_EQ(analysis.Status().preview,previous);EXPECT_EQ(undo->GetUndoDepth(),1);
    recipe.settings.xFrom=100;analysis.Submit(recipe);EXPECT_EQ(analysis.Wait().stage,AnalysisStage::Failed);EXPECT_EQ(analysis.Status().preview,previous);
    recipe.settings.xFrom.reset();analysis.Submit(recipe);analysis.Cancel();EXPECT_EQ(analysis.Poll().stage,AnalysisStage::Cancelled);EXPECT_EQ(analysis.Status().preview,previous);
    std::weak_ptr<pl::Project> oldProject=doc.project;
    analysis.Submit(recipe);doc.New();EXPECT_TRUE(oldProject.expired());EXPECT_EQ(analysis.Wait().stage,AnalysisStage::Stale);EXPECT_EQ(analysis.Status().preview,previous);
}
TEST_F(PlotGapAnalysisJobs, PreviewCannotBeAcceptedAndBatchKeepsMixedStatusIds) {
    analysis.Submit(recipe,{},true);ASSERT_EQ(analysis.Wait().stage,AnalysisStage::PreviewReady);EXPECT_THROW(analysis.Accept(),std::invalid_argument);EXPECT_EQ(undo->GetUndoDepth(),0);
    auto bad=recipe;bad.id="bad-recipe";bad.settings.xFrom=100;
    analysis.SubmitBatch({{"good",recipe},{"bad",bad}});ASSERT_EQ(analysis.Wait().stage,AnalysisStage::PreviewReady);
    const auto result=analysis.Status().preview;ASSERT_EQ(result->items.size(),2);EXPECT_EQ(result->items[0].id,"good");EXPECT_TRUE(result->items[0].result->ok());
    EXPECT_EQ(result->items[1].id,"bad");EXPECT_EQ(result->items[1].recipeId,"bad-recipe");EXPECT_EQ(result->items[1].result->error,pl::AnalysisError::InsufficientPoints);
    analysis.Accept();EXPECT_EQ(analysis.Status().outputWorksheets.size(),2);EXPECT_EQ(undo->GetUndoDepth(),1);
}
TEST_F(PlotGapAnalysisJobs, UntrustedProjectRejectsUserCallableBeforeExecution) {
    const auto file=std::filesystem::temp_directory_path()/("plotgap-untrusted-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".plotlab");
    doc.SaveScript("Owner model","raise RuntimeError('must not execute before trust')");
    doc.project->save(file);doc.Open(file);
    ASSERT_FALSE(doc.PythonTrusted());std::atomic_int calls=0;
    recipe.settings.operation=pl::ScientificOperation::UserFit;recipe.settings.userModelId="trusted.line";
    recipe.settings.parameters={{"a","V",2,-10,10},{"b","V/s",3,-10,10}};
    pl::UserAnalysisModel model{"trusted.line","s","V",1,{"a","b"},[&](auto x,auto p,auto out){++calls;for(std::size_t i=0;i<x.size();++i)out[i]=p[0]+p[1]*x[i];}};
    try {analysis.Submit(recipe,model);FAIL()<<"Untrusted model was accepted";} catch(const pl::AnalysisException& error) {EXPECT_EQ(error.code,pl::AnalysisError::Trust);}
    EXPECT_EQ(calls,0);
    std::filesystem::remove(file);
}
TEST_F(PlotGapAnalysisJobs, CancelRunningCallableReleasesSourceAndDoesNotPublish) {
    analysis.Submit(recipe);ASSERT_EQ(analysis.Wait().stage,AnalysisStage::PreviewReady);auto previous=analysis.Status().preview;
    recipe.settings.operation=pl::ScientificOperation::UserFit;recipe.settings.userModelId="slow";recipe.settings.parameters={{"a","V",2,-10,10,true}};
    std::promise<void> entered,release;auto gate=release.get_future().share();
    pl::UserAnalysisModel model{"slow","s","V",1,{"a"},[&](auto x,auto p,auto out){entered.set_value();gate.wait();for(std::size_t i=0;i<x.size();++i)out[i]=p[0];}};
    const auto job=analysis.Submit(recipe,model);ASSERT_EQ(entered.get_future().wait_for(std::chrono::seconds(5)),std::future_status::ready);
    analysis.Cancel();release.set_value();EXPECT_EQ(analysis.Poll().preview,previous);
    for(int i=0;i<5000;++i) {auto state=jobs->GetJob(job);if(state && state->status==JobStatus::Cancelled)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_EQ(jobs->GetJob(job)->status,JobStatus::Cancelled);EXPECT_EQ(undo->GetUndoDepth(),0);
}
TEST_F(PlotGapAnalysisJobs, AcceptedWorksheetSaveReopenKeepsIdsAndProvenance) {
    analysis.Submit(recipe);ASSERT_EQ(analysis.Wait().stage,AnalysisStage::PreviewReady);analysis.Accept();
    const auto sheets=analysis.Status().outputWorksheets;const auto sourceRows=analysis.Status().accepted->items.front().result->rowIds;
    const auto file=std::filesystem::temp_directory_path()/"plotgap-accepted.plotlab";doc.Save(file);doc.Open(file);
    for(auto sheet:sheets) {
        const auto& saved=std::get<pl::Worksheet>(doc.project->object(sheet).data);
        EXPECT_EQ(saved.source.at("analysis.recipe_id"),recipe.id);EXPECT_EQ(saved.source.at("analysis.model_id"),"linear");
    }
    EXPECT_EQ(std::get<pl::Worksheet>(doc.project->object(recipe.worksheet).data).rows,sourceRows);
    const auto& summary=std::get<pl::Worksheet>(doc.project->object(sheets[1]).data);EXPECT_EQ(summary.rows.size(),1);
    std::filesystem::remove(file);
}
TEST_F(PlotGapAnalysisJobs, HeadlessPanelKeepsPreviewAndHasNoIdConflicts) {
    ImGui::CreateContext();ImPlot::CreateContext();
    struct Cleanup {~Cleanup(){ImPlot::DestroyContext();ImGui::DestroyContext();}} cleanup;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1200};io.DeltaTime=1.f/60;
    io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;io.ConfigDebugHighlightIdConflicts=true;
    AnalysisPanel panel{analysis};panel.SetRecipe(recipe);
    analysis.Submit(recipe);ASSERT_EQ(analysis.Wait().stage,AnalysisStage::PreviewReady);const auto preview=analysis.Status().preview;
    for(int frame=0;frame<4;++frame) {
        ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,1100});panel.Draw();ImGui::Render();
        EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);EXPECT_EQ(analysis.Status().preview,preview);
    }
    EXPECT_EQ(undo->GetUndoDepth(),0);
}
