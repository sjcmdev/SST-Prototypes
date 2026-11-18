#include <gtest/gtest.h>
#include "TestProject.hpp"
namespace SST::Prototypes::CapLab {
TEST(CapLabProject, PFSI2AndRoundtripPreserveAllDataAndIDs) {
    auto p = FixtureProject();
    const auto &s = p.sweeps.at(0);
    auto values = [&](pl::Id id) -> const auto & { return std::get<std::vector<double>>(std::get<pl::Column>(p.engine->object(id).data).values); };
    EXPECT_EQ(values(s.frequency).size(),40);
    EXPECT_DOUBLE_EQ(values(s.frequency)[0],100);
    EXPECT_DOUBLE_EQ(values(s.capacitance)[0],8.8601e-10);
    EXPECT_DOUBLE_EQ(values(s.resistance)[0],134160);
    EXPECT_EQ(s.temperature_K,100);
    EXPECT_EQ(Json::parse(Serialize(Parse(Serialize(p)))),Json::parse(Serialize(p)));
}
TEST(CapLabProject, RejectsBadSchemaReferencesAndMetadata) {
    auto json = Json::parse(Serialize(FixtureProject()));
    auto bad=json; bad["schema_version"]=2; EXPECT_THROW(Parse(bad.dump()),std::exception);
    bad=json; bad["sweeps"][0]["sample_id"]=999; EXPECT_THROW(Parse(bad.dump()),std::exception);
    bad=json; bad["samples"][0]["area_m2"]=0; EXPECT_THROW(Parse(bad.dump()),std::exception);
    bad=json; bad["sweeps"][0]["variant"]="C_G"; EXPECT_THROW(Parse(bad.dump()),std::exception);
    bad=json; bad["samples"].push_back(bad["samples"][0]); EXPECT_THROW(Parse(bad.dump()),std::exception);
}
TEST(CapLabImport, ValidatesPreviewAtCppBoundary) {
    auto v=FixturePreview(); v["columns"][0][0]=0; EXPECT_THROW(PreviewTable(v,100),std::exception);
    v=FixturePreview(); v["columns"][1].erase(0); EXPECT_THROW(PreviewTable(v,100),std::exception);
    v=FixturePreview(); v["columns"][2][0]=-1; EXPECT_THROW(PreviewTable(v,100),std::exception);
    EXPECT_THROW(PreviewTable(FixturePreview(),INFINITY),std::exception);
}
TEST(CapLabImport, UndoRedoAndRepeatedNamesKeepStableReferences) {
    Project p;
    ImportChange first(p,FixturePreview(),100,CFTemplate{});
    const auto sample=p.samples[0].id, sweep=p.sweeps[0].id, graph=p.graph;
    ImportChange second(p,FixturePreview(),200,CFTemplate{});
    EXPECT_EQ(p.samples.size(),2); EXPECT_NE(p.samples[1].id,sample); EXPECT_EQ(p.graph,graph);
    Validate(p);
    second.Undo(); first.Undo(); EXPECT_TRUE(p.samples.empty()); EXPECT_EQ(p.graph,0);
    first.Redo(); second.Redo(); Validate(p);
    EXPECT_EQ(p.samples[0].id,sample); EXPECT_EQ(p.sweeps[0].id,sweep); EXPECT_EQ(p.graph,graph);
}
}
