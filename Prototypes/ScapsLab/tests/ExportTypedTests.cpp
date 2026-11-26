#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "LegacyResultRuns.hpp"
#include <fstream>
#include <thread>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Json Exec(Workflow& w,const Json& request) {
    const auto started=w.Dispatch(request);if(!started.value("ok",false))return started;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(2);
    while(w.Pending() && std::chrono::steady_clock::now()<deadline){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_FALSE(w.Pending());return w.response;
}
struct CountingResult final : ResultBase {
    mutable int dumps=0;
    PJson meta;
    CountingResult() {meta.prim={{"provenance",{{"time_unix_ms",123},{"device_hash","test"}}},{"project_snapshot",{{"temperature_K",300.}}}};}
    const PJson& MetaOf() const override {return meta;}
    Json Dump() const override {++dumps;auto result=meta.prim;result["points"]=Json::array({{{"voltage_V",0.},{"J_A_m2",1.},{"valid",true}}});return result;}
};
}
TEST(ExportTyped, ReferenceRunOrderAliasesAndLiveOnlyMatchLegacy) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;
    ASSERT_TRUE(Exec(w,{{"operation","project_load"},{"path",(ExamplesDirectory()/"07-pn-compare-with-results.scapslab").string()}}).value("ok",false));
    for(bool liveOnly:{false,true}) {
        if(liveOnly)for(const auto& device:w.project.devices)if(device->result.typed()) {
            auto result=CreateRef<IvResult>(*device->result.typed());result->liveOnly=true;device->result.adopt(std::move(result));
        }
        const auto expected=ResultRuns(w.Workspace());
        Json actual=Json::array();
        for(const auto& run:w.ResultRuns())actual.push_back({{"run_id",run.runId},{"device_id",run.deviceId},{"device_name",run.deviceName},{"kind",run.kind},{"result",run.result?run.result->Dump():Json::object()}});
        EXPECT_EQ(actual,expected);
        for(auto& run:actual)run.erase("result");EXPECT_EQ(w.Runs(),actual);
    }
}
TEST(ExportTyped, OnlySelectedResultsAreSerializedAndReferencesStayShared) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;
    auto selected=CreateRef<CountingResult>(),other=CreateRef<CountingResult>();
    auto& d=w.Current();d.history=Json::array({{{"run_id","selected"},{"kind","iv"},{"provenance",selected->Provenance()}},{{"run_id","other"},{"kind","iv"},{"provenance",other->Provenance()}}});
    d.runs=CreateRef<const RunMap>(RunMap{{"selected",selected},{"other",other}});
    const auto runs=w.ResultRuns();ASSERT_EQ(runs.size(),2u);EXPECT_EQ(runs[0].result,selected);EXPECT_EQ(runs[1].result,other);
    EXPECT_EQ(w.Runs().size(),2u);EXPECT_EQ(selected->dumps,0);EXPECT_EQ(other->dumps,0);
    const auto path=std::filesystem::temp_directory_path()/"json2-selected.csv";
    ASSERT_TRUE(Exec(w,{{"operation","export_results"},{"path",path.string()},{"run_ids",{"selected","selected"}}}).value("ok",false))<<w.response;
    EXPECT_EQ(selected->dumps,1);EXPECT_EQ(other->dumps,0);EXPECT_FALSE(selected->HasView());EXPECT_FALSE(other->HasView());
    EXPECT_TRUE(std::filesystem::exists(path));
    for(const Json& ids:{Json("selected"),Json::array({123}),Json::array({"missing"}),Json::array()}) {
        const auto response=Exec(w,{{"operation","export_results"},{"path",path.string()},{"run_ids",ids}});
        EXPECT_FALSE(response.value("ok",true));EXPECT_EQ(response.at("error").at("code"),"operation_failed");
    }
    EXPECT_EQ(selected->dumps,1);EXPECT_EQ(other->dumps,0);std::filesystem::remove(path);
}
TEST(ExportTyped, NativeBatchHistoryUsesEffectiveTimestampAndStandaloneId) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;auto& d=w.Current();d.SetDocument(NewDeviceDocument());
    auto batch=CreateRef<BatchResult>();batch->native=true;batch->runPlan=d.runPlan;batch->projectSnapshot=d.draft;batch->batchDraft=d.batch;batch->recorderDraft=d.recorder;batch->time_unix_ms=123;
    d.batchResult.adopt(batch);d.runs=CreateRef<const RunMap>(RunMap{{"history-id",batch}});
    d.history=Json::array({{{"run_id","history-id"},{"kind","batch"},{"provenance",{{"time_unix_ms",0}}}}});
    const auto expected=ResultRuns(w.Workspace());const auto runs=w.ResultRuns();
    ASSERT_EQ(runs.size(),1u);EXPECT_EQ(runs[0].result,batch);EXPECT_EQ(runs[0].runId,expected[0].at("run_id").get<std::string>());
    EXPECT_EQ(runs[0].runId,d.id+"/batch/123");EXPECT_EQ(runs[0].result->Dump(),expected[0].at("result"));
    d.batchResult.clear();
    const auto history=w.ResultRuns();ASSERT_EQ(history.size(),1u);EXPECT_EQ(history[0].runId,"history-id");EXPECT_EQ(history[0].result,batch);
}
