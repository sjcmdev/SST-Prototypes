#include <gtest/gtest.h>
#include "TestProject.hpp"
#include "IO/PrototypeWorkflow.hpp"

namespace SST::Prototypes::CapLab {
TEST(CapLabTypedBoundary, FreshnessMatchesPackedSnapshotWithoutPackingCurrentColumns) {
    auto p=FixtureProject();
    const auto id=p.sweeps[0].id;
    const auto input=AnalysisMeasurement(p,id);
    auto check=[&] {
        bool sameSample=false;
        EXPECT_EQ(SameAnalysisMeasurement(p,input,sameSample),MeasurementJson(AnalysisMeasurement(p,id))==MeasurementJson(input));
        EXPECT_TRUE(sameSample);
    };
    check();
    p.samples[0].area_m2=2e-6; check(); p.samples[0].area_m2.reset();
    auto object=p.engine->object(p.sweeps[0].capacitance);
    std::get<std::vector<double>>(std::get<pl::Column>(object.data).values)[0]*=2;
    p.engine->change("Edit analysis input",{std::move(object)}).apply(*p.engine);
    check();
    auto missingRow=input;auto rows=std::make_shared<std::vector<pl::Id>>(*input.rowIds);rows->erase(rows->begin());missingRow.rowIds=rows;
    bool sameSample=false;
    EXPECT_FALSE(SameAnalysisMeasurement(p,missingRow,sameSample));
}
TEST(CapLabTypedBoundary, SnapshotMatchesFileRoundtripAndSharesImmutableColumns) {
    auto p=FixtureProject();
    p.engine->reserveId(); // Unused reservations must survive a snapshot.
    auto snapshot=CloneProject(p);
    const auto persisted=Serialize(p);
    EXPECT_EQ(Serialize(snapshot),Serialize(Parse(persisted)));
    EXPECT_NE(snapshot.engine,p.engine);
    const auto column=p.sweeps[0].capacitance;
    EXPECT_EQ(snapshot.engine->objects().at(column),p.engine->objects().at(column));
    auto object=snapshot.engine->object(column);
    std::get<std::vector<double>>(std::get<pl::Column>(object.data).values)[0]=42.;
    snapshot.engine->change("Detached edit",{std::move(object)}).apply(*snapshot.engine);
    EXPECT_EQ(Serialize(p),persisted);
    EXPECT_NE(Serialize(snapshot),persisted);
    EXPECT_EQ(NextEntityId(*p.engine),NextEntityId(*Parse(persisted).engine));
}
TEST(CapLabTypedBoundary, JobTakesProjectWithoutSerializedInput) {
    auto p=FixtureProject();
    DocumentJob job(Operation::Plot,CloneProject(p));
    job.analysisOptions={{"plot_type","C-f"},{"measurement_ids",{p.sweeps[0].id}}};
    JobContext context({},{},{},{},[] {return false;});
    ASSERT_NO_THROW(job.Execute(context));
    ASSERT_TRUE(job.opened);
    EXPECT_NE(job.opened->graph,p.graph);
    EXPECT_EQ(job.opened->sweeps[0].temperature_K,p.sweeps[0].temperature_K);
}
TEST(CapLabTypedBoundary, LegacySourceTextIsLosslessButRuntimeUsesFields) {
    auto p=FixtureProject();
    auto j=ProjectJson(p);
    for (auto &object:j["engine"]["objects"]) if (object["id"]==p.sweeps[0].id) {
        object["source"]["temperature_K"]="1e2";
        object["source"]["source_rows"]="[ 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40 ]";
    }
    auto loaded=ProjectFromJson(j);
    EXPECT_EQ(Serialize(loaded),j.dump());
    EXPECT_EQ(loaded.sources.at(p.sweeps[0].id).rows.size(),40);
    const auto &source=std::get<pl::Worksheet>(loaded.engine->object(p.sweeps[0].id).data).source;
    EXPECT_FALSE(source.contains("source_rows"));
    EXPECT_FALSE(source.contains("temperature_K"));
    EXPECT_EQ(Serialize(CloneProject(loaded)),j.dump());
}
TEST(CapLabTypedBoundary, ViewComparisonUsesStoredFields) {
    sst::ui::plot::PlotViewState a,b;
    a.viewport=b.viewport=sst::ui::plot::Bounds{1,2,3,4};
    EXPECT_TRUE(SameStoredView(a,b));
    a.fitted=true; a.axisState.emplace();
    EXPECT_TRUE(SameStoredView(a,b));
    a.zTicks.push_back({1,"one",false}); EXPECT_FALSE(SameStoredView(a,b));
    b.zTicks=a.zTicks; EXPECT_TRUE(SameStoredView(a,b));
    b.visibility["plot"]=false; EXPECT_FALSE(SameStoredView(a,b));
}
TEST(CapLabTypedBoundary, ExternalHighWaterDoesNotReplayUntrustedReservations) {
    CapLabWorkflow workflow(CreateRef<JobSystem>(nullptr,1));
    auto workspace=workflow.WorkspaceJson();
    const pl::Id high=pl::Id{1}<<50;
    workspace["projects"][0]["next_entity_id"]=high;
    workflow.RestoreWorkspace(workspace);
    EXPECT_EQ(NextEntityId(*workflow.EditSnapshot().engine),high);
    auto exhausted=FixtureProject();
    auto j=ProjectJson(exhausted); j["engine"]["nextId"]=std::numeric_limits<pl::Id>::max();
    exhausted=ProjectFromJson(j);
    EXPECT_EQ(NextEntityId(*exhausted.engine),std::numeric_limits<pl::Id>::max());
}
TEST(CapLabTypedBoundary, PreviewReturnsDataWithoutSerializedTableMetadata) {
    struct Importer final : LabASImporter {
        bool invalid = false;
        Json preview(std::string_view,const Path &,char,const Json &options,JobContext &) const override {
            EXPECT_TRUE(options.is_object());
            auto result=FixturePreview();
            if (invalid) result["columns"][0][0]=0;
            return result;
        }
    };
    DocumentJob job(Operation::Preview,Project{},Path{std::filesystem::path(SST_PROTOTYPE_PYTHON_ROOT).parent_path()/"tests/fixtures/cf_100K_7.dat"});
    auto importer=std::make_shared<Importer>(); job.importer=importer;
    JobContext context({},{},{},{},[] {return false;});
    ASSERT_NO_THROW(job.Execute(context));
    ASSERT_EQ(job.sourcePreviews.size(),1);
    ASSERT_TRUE(job.sourcePreviews[0].value);
    EXPECT_EQ(job.preview,FixturePreview());
    EXPECT_FALSE(job.preview.contains("preview_json"));
    importer->invalid=true;
    DocumentJob invalid(Operation::Preview,Project{},job.path); invalid.importer=importer;
    ASSERT_NO_THROW(invalid.Execute(context));
    ASSERT_EQ(invalid.sourcePreviews.size(),1);
    EXPECT_FALSE(invalid.sourcePreviews[0].value);
    EXPECT_TRUE(invalid.sourcePreviews[0].error);
}
TEST(CapLabTypedBoundary, MeasurementMatchesFrozenValuesAndSharesColumnOwnership) {
    auto p=FixtureProject();const auto input=AnalysisMeasurement(p,p.sweeps[0].id);
    ASSERT_EQ(input.columns.size(),3u);ASSERT_TRUE(input.rowIds);EXPECT_EQ(input.rowIds->size(),40u);
    EXPECT_DOUBLE_EQ(input.columns[0]->front(),100.);EXPECT_DOUBLE_EQ(input.columns[1]->front(),8.8601e-10);EXPECT_DOUBLE_EQ(input.columns[2]->front(),134160.);
    const auto &original=std::get<std::vector<double>>(std::get<pl::Column>(p.engine->object(p.sweeps[0].capacitance).data).values);
    EXPECT_EQ(input.columns[1]->data(),original.data());EXPECT_FALSE(input.metadata.contains("columns"));EXPECT_FALSE(input.metadata.contains("row_ids"));
    auto snapshot=CloneProject(p);auto object=p.engine->object(p.sweeps[0].capacitance);
    std::get<std::vector<double>>(std::get<pl::Column>(object.data).values)[0]=42.;p.engine->change("Edit",{std::move(object)}).apply(*p.engine);
    EXPECT_DOUBLE_EQ(input.columns[1]->front(),8.8601e-10);EXPECT_DOUBLE_EQ(AnalysisMeasurement(snapshot,snapshot.sweeps[0].id).columns[1]->front(),8.8601e-10);
}
TEST(CapLabTypedBoundary, AnalysisInputsStayTypedAcrossSaveLoadCloneAndFreshness) {
    auto p=FixtureProject();const auto m=AnalysisMeasurement(p,p.sweeps[0].id);JobContext context({},{},{},{},[]{return false;});
    Json circuit{{"id",p.engine->reserveId()},{"sample_id",p.samples[0].id},{"name","R"},{"schematic",CircuitJson(ccui::importString("R0"))}};
    p.circuits.push_back(circuit);auto result=SimulateIS(m,circuit,{{"temperature_K",100.},{"bias_V",0.}},context);
    ASSERT_EQ(result.inputs.size(),1u);EXPECT_EQ(result.inputs[0].columns[0],m.columns[0]);ASSERT_FALSE(result.plots.empty());
    AppendAnalysis(p,std::move(result));const auto id=p.analyses[0].at("id").get<pl::Id>();
    EXPECT_FALSE(p.analyses[0]["inputs"][0].contains("columns"));EXPECT_EQ(p.analysisInputs.at(id)[0].columns[0],m.columns[0]);
    p.analyses[0]["settings"]["extension"]={{"measurements",Json::array({{{"id",p.sweeps[0].id},{"columns",{{42.}}},{"row_ids",{7u}}}})}};
    const auto text=Serialize(p);auto loaded=Parse(text);EXPECT_EQ(Serialize(loaded),text);
    EXPECT_EQ(loaded.analyses[0]["settings"]["extension"],p.analyses[0]["settings"]["extension"]);EXPECT_EQ(Serialize(CloneProject(p)),text);
    bool sameSample=false;EXPECT_TRUE(SameAnalysisMeasurement(loaded,loaded.analysisInputs.at(id)[0],sameSample));EXPECT_TRUE(sameSample);
    EXPECT_EQ(Json::parse(text)["analyses"][0]["inputs"][0],MeasurementJson(m));
    auto conflict=Json::parse(text);auto source=conflict["analyses"][0];source["inputs"][0]["columns"][0][0]=42.;
    conflict["analyses"][0]["source_analyses"]=Json::array({source});
    EXPECT_THROW(ProjectFromJson(conflict),std::runtime_error);
    loaded.samples[0].area_m2=2e-6;RefreshAnalyses(loaded);EXPECT_EQ(loaded.analyses[0]["status"],"stale");
}
}
