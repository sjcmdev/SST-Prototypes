#include <gtest/gtest.h>
#include "TestProject.hpp"
#include "CapLabCommands.hpp"
#include "Domain/CapLabProjectTree.hpp"
#include "Presentation/CapLabPrototypePanel.hpp"
#include <imgui_internal.h>
#include <implot.h>
#include "Core/IO/TextFileIO.hpp"
using namespace SST;
using namespace SST::Prototypes::CapLab;
namespace {
struct TreeFixture : testing::Test {
    Ref<EventBus> bus=CreateRef<EventBus>();
    Ref<JobSystem> jobs=CreateRef<JobSystem>(nullptr,1);
    Ref<UndoStack> undo=CreateRef<UndoStack>();
    Ref<CommandRegistry> commands=CreateRef<CommandRegistry>();
    CapLabWorkflow state{jobs,bus,undo};
    TreeFixture() {state.Document().project=FixtureProject(); RegisterCommands(commands,bus,state);}
    auto Edit(Json edit) {CommandContext c; c.Set("edit",std::move(edit)); return commands->Execute(CommandID{"caplab.tree"},std::move(c));}
};
TEST_F(TreeFixture, MetadataDuplicateMoveDeleteUndoAndStableIds) {
    auto &p=state.Document().project; const auto source=p.samples[0].id, mid=p.sweeps[0].id;
    ASSERT_TRUE(Edit({{"action","rename_sample"},{"sample_id",source},{"name","T series"}}));
    ASSERT_TRUE(Edit({{"action","sample_metadata"},{"sample_id",source},{"area_m2",2e-6},{"material","CIGS"},{"notes","wafer A"}}));
    ASSERT_TRUE(Edit({{"action","duplicate_sample"},{"sample_id",source},{"name","copy"}}));
    ASSERT_EQ(p.samples.size(),2); ASSERT_EQ(p.sweeps.size(),2);
    const auto target=p.samples.back().id, copyMid=p.sweeps.back().id;
    EXPECT_NE(copyMid,mid); EXPECT_EQ(p.samples.back().metadata.at("notes"),"wafer A");
    EXPECT_EQ(MeasurementJson(AnalysisMeasurement(p,copyMid)).at("columns"),MeasurementJson(AnalysisMeasurement(p,mid)).at("columns"));
    EXPECT_NE(MeasurementJson(AnalysisMeasurement(p,copyMid)).at("row_ids"),MeasurementJson(AnalysisMeasurement(p,mid)).at("row_ids"));
    state.Select({{"kind","measurement"},{"id",mid}});
    ASSERT_TRUE(Edit({{"action","move_measurements"},{"measurement_ids",{mid}},{"target_sample_id",target}}));
    EXPECT_EQ(state.Document().selectedSample,target); EXPECT_EQ(p.sweeps[0].sample_id,target);
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(p.sweeps[0].sample_id,source);
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(p.sweeps[0].sample_id,target);
    ASSERT_TRUE(Edit({{"action","delete_sample"},{"sample_id",target}}));
    EXPECT_EQ(state.Document().selectedMeasurement,0); EXPECT_EQ(p.sweeps.size(),0);
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(p.sweeps.size(),2); EXPECT_EQ(state.Document().selectedMeasurement,0);
    ASSERT_TRUE(undo->Redo());
    ASSERT_TRUE(Edit({{"action","create_sample"},{"name","new"}}));
    EXPECT_GT(p.samples.back().id,copyMid); EXPECT_EQ(state.Document().selectedMeasurement,0);
    EXPECT_NO_THROW(Parse(Serialize(p)));
}
TEST_F(TreeFixture, FailedEditsAreAtomicAndIdsNeverReusedAfterUndo) {
    const auto original=Serialize(state.Document().project);
    const auto id=state.Document().project.samples[0].id;
    auto invalid=Json::parse(original); invalid["samples"][0]["metadata"]=Json::array();
    EXPECT_THROW(Parse(invalid.dump()),std::exception);
    EXPECT_FALSE(Edit({{"action","rename_sample"},{"sample_id",id},{"name"," "}}));
    EXPECT_FALSE(Edit({{"action","sample_metadata"},{"sample_id",id},{"area_m2",-1}}));
    EXPECT_FALSE(Edit({{"action","move_measurements"},{"measurement_ids",{999999}},{"target_sample_id",id}}));
    EXPECT_EQ(Serialize(state.Document().project),original); EXPECT_FALSE(undo->CanUndo());
    ASSERT_TRUE(Edit({{"action","duplicate_sample"},{"sample_id",id},{"name","one"}}));
    const auto discarded=state.Document().project.samples.back().id;
    ASSERT_TRUE(undo->Undo());
    ASSERT_TRUE(Edit({{"action","create_sample"},{"name","two"}}));
    EXPECT_GT(state.Document().project.samples.back().id,discarded);
}
TEST_F(TreeFixture, TargetImportIsOneUndoStepAndKeepsTemperatureSeries) {
    auto &p=state.Document().project; const auto id=p.samples[0].id;
    state.importTargetSample=id; state.preview=FixturePreview(); state.temperature=200; state.previewRevision=state.Document().revision;
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.import"}));
    ASSERT_EQ(p.samples.size(),1); ASSERT_EQ(p.sweeps.size(),2);
    EXPECT_EQ(p.sweeps.back().sample_id,id); EXPECT_EQ(p.sweeps.back().temperature_K,200);
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(p.sweeps.size(),1);
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(p.sweeps.size(),2);
    state.importTargetSample=999999; state.preview=FixturePreview(); state.previewRevision=state.Document().revision;
    const auto before=Serialize(p); EXPECT_FALSE(commands->Execute(CommandID{"caplab.import"})); EXPECT_EQ(Serialize(p),before);
}
TEST_F(TreeFixture, SaveAfterUndoDoesNotReuseDiscardedIdentityOnReopen) {
    ASSERT_TRUE(Edit({{"action","create_sample"},{"name","discarded"}}));
    const auto discarded=state.Document().project.samples.back().id;
    ASSERT_TRUE(undo->Undo());
    const Path path{FileSystem::TempDirectoryPath()/"caplab-clproj1-identities.json"};
    state.Start(Operation::Save,path); ASSERT_NE(state.activeJob,0);
    jobs->Shutdown(); state.OnUpdate(0);
    std::string text,error; ASSERT_TRUE(TextFileIO::Load(path,text,error));
    auto reopened=Parse(text);
    EditProjectTree(reopened,{{"action","create_sample"},{"name","after reopen"}});
    EXPECT_GT(reopened.samples.back().id,discarded);
    EXPECT_TRUE(FileSystem::Remove(path.Native()));
}
TEST_F(TreeFixture, MovingHistoricalMeasurementPreservesOriginalReferencesWithoutRebinding) {
    auto &p=state.Document().project; const auto source=p.samples[0].id, mid=p.sweeps[0].id;
    const auto row=MeasurementJson(AnalysisMeasurement(p,mid)).at("row_ids").at(0);
    p.historical.push_back({{"sample_id",source},{"status","historical"},{"data",{
        {"selections",{{"AS",mid}}},{"points",Json::array({Json{{"measurement_id",mid},{"row_id",row}}})}}}});
    ASSERT_TRUE(Edit({{"action","create_sample"},{"name","target"}}));
    const auto target=p.samples.back().id;
    ASSERT_TRUE(Edit({{"action","move_measurements"},{"measurement_ids",{mid}},{"target_sample_id",target}}));
    EXPECT_TRUE(p.historical[0].at("data").at("selections").at("AS").is_null());
    EXPECT_TRUE(p.historical[0].at("data").at("points")[0].at("measurement_id").is_null());
    EXPECT_EQ(p.historical[0].at("moved_references")[0].at("original_measurement_id"),mid);
    EXPECT_NO_THROW(Parse(Serialize(p)));
    ASSERT_TRUE(undo->Undo());
    EXPECT_EQ(p.historical[0].at("data").at("points")[0].at("row_id"),row);
}
TEST_F(TreeFixture, SelectionSurvivesProjectSwitchAndDoesNotRebindAfterDelete) {
    const auto first=state.workspace.active, mid=state.Document().project.sweeps[0].id;
    state.Select({{"kind","measurement"},{"id",mid}});
    auto second=state.workspace.Open(CapLabDocument{},false); ASSERT_TRUE(state.Activate(second));
    EXPECT_EQ(state.Document().selectedMeasurement,0);
    ASSERT_TRUE(state.Activate(first)); EXPECT_EQ(state.Document().selectedMeasurement,mid);
    CommandContext c; c.Set("edit",Json{{"measurement_id",mid}});
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.measurement"},std::move(c)));
    EXPECT_EQ(state.Document().selectedMeasurement,0);
    EXPECT_THROW(state.Select({{"kind","measurement"},{"id",mid}}),std::exception);
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(state.Document().selectedMeasurement,0);
}
TEST_F(TreeFixture, DeletingCircuitInvalidatesAcceptedResultAndUndoRestoresIt) {
    auto &p=state.Document().project; const auto sample=p.samples[0].id;
    EditCircuit(p,{{"action","create"},{"sample_id",sample},{"name","R"},{"topology","R0"}});
    const auto cid=p.circuits[0].at("id");
    JobContext context({}, {}, {}, {}, [] {return false;});
    AppendAnalysis(p,SimulateIS(nullptr,p.circuits[0],{{"frequency_Hz",{1.,10.,100.}},
        {"temperature_K",300.},{"bias_V",0.}},context));
    state.Select({{"kind","analysis"},{"id",p.analyses[0].at("id")}});
    EXPECT_EQ(state.Document().selectedCircuit,cid);
    ASSERT_TRUE(Edit({{"action","create_sample"},{"name","empty"}}));
    state.Select({{"kind","sample"},{"id",p.samples.back().id}});
    EXPECT_EQ(p.graph,0);
    state.Select({{"kind","circuit"},{"id",cid}});
    EXPECT_NE(p.graph,0);
    ASSERT_TRUE(Edit({{"action","delete_circuit"},{"circuit_id",cid}}));
    EXPECT_TRUE(p.circuits.empty()); EXPECT_EQ(state.Document().selectedCircuit,0);
    EXPECT_EQ(p.analyses[0].at("status"),"missing_input");
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(p.circuits.size(),1); EXPECT_EQ(p.circuits[0].at("id"),cid);
    EXPECT_EQ(p.analyses[0].at("status"),"valid");
}
TEST_F(TreeFixture, TreeAndModuleWindowsShareSelectionWithoutIdConflicts) {
    const auto oldGui=ImGui::GetCurrentContext(); const auto oldPlot=ImPlot::GetCurrentContext();
    auto *gui=ImGui::CreateContext(); auto *plot=ImPlot::CreateContext();
    auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1600,1600}; io.DeltaTime=1.f/60;
    io.ConfigDebugHighlightIdConflicts=true;
    unsigned char *pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    const auto id=state.Document().project.samples[0].id;
    ASSERT_TRUE(Edit({{"action","duplicate_sample"},{"sample_id",id},{"name",state.Document().project.samples[0].name}}));
    ASSERT_TRUE(Edit({{"action","move_measurements"},{"measurement_ids",{state.Document().project.sweeps[1].id}},{"target_sample_id",id}}));
    state.Document().project.sweeps[1].temperature_K=100.000001;
    state.Select({{"kind","measurement"},{"id",state.Document().project.sweeps[0].id}});
    std::vector<Unique<CapLabPrototypePanel>> panels;
    for (auto view:{CapLabPrototypePanel::View::Project,CapLabPrototypePanel::View::CV,CapLabPrototypePanel::View::AS,
        CapLabPrototypePanel::View::Walter,CapLabPrototypePanel::View::IS}) panels.push_back(CreateUnique<CapLabPrototypePanel>(&state,commands,view));
    for (int frame=0;frame<3;++frame) {
        ImGui::NewFrame();
        for (std::size_t i=0;i<panels.size();++i) {
            ImGui::SetNextWindowSize({900,1000}); ImGui::Begin(("Tree test "+std::to_string(i)).c_str());
            panels[i]->RenderContent(); ImGui::End();
        }
        ImGui::Render(); EXPECT_EQ(gui->DebugDrawIdConflictsCount,0);
    }
    EXPECT_EQ(state.Document().selectedSample,id);
    ImPlot::DestroyContext(plot); ImGui::DestroyContext(gui);
    ImGui::SetCurrentContext(oldGui); ImPlot::SetCurrentContext(oldPlot);
}
}

