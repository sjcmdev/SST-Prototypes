#include <gtest/gtest.h>
#include "Workflow.hpp"
#include <scaps_core/batch.hpp>
#include <scaps_core/device_project_yaml.hpp>
#include <fstream>
#include <bit>
#include <thread>
#define NOMINMAX
#include <Windows.h>
#undef GetJob
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
using namespace SST;
using namespace SST::ScapsLab;
namespace {
using Clock = std::chrono::steady_clock;
double Seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
double Cpu() {FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return ((double(k.dwHighDateTime)+u.dwHighDateTime)*4294967296.+k.dwLowDateTime+u.dwLowDateTime)*1e-7;}
double PeakMB() {PROCESS_MEMORY_COUNTERS p{};GetProcessMemoryInfo(GetCurrentProcess(),&p,sizeof(p));return p.PeakWorkingSetSize/1048576.;}
}
TEST(PerfProbe, BatchBoundaryDirect) {
    if(!std::getenv("TYPED1_PERF")) GTEST_SKIP();
    const auto doc=ReadProject(SCAPSLAB_PROJECT);
    for(int curves=0;curves<=1;++curves) for(int rep=0;rep<3;++rep) {
        Json spec={{"voltages_V",scaps_core::make_voltage_list(Model(doc).voltage)},{"include_curves",bool(curves)},{"threads",1},{"columns",RecorderDefaults()}};
        const auto start=Clock::now();auto text=scaps_core::run_batch_json(doc.dump(),spec.dump());const double wall=Seconds(start);
        const auto parse=Clock::now();const auto result=Json::parse(text);
        std::printf("TYPED1 engine_json curves=%d rep=%d wall_s=%.9f parse_s=%.9f bytes=%zu elapsed_s=%.9f\n",curves,rep,wall,Seconds(parse),text.size(),result.at("elapsed_s").get<double>());
    }
}
TEST(PerfProbe, TypedBatchEngineDirect) {
    if(!std::getenv("TYPED1_PERF"))GTEST_SKIP();
    const auto project=Model(ReadProject(SCAPSLAB_PROJECT));
    scaps_core::BatchSpec spec;spec.voltages_V=scaps_core::make_voltage_list(project.voltage);
    scaps_core::RecorderColumn column;column.id="J_at_0V";column.quantity="J";column.voltage_V=0;spec.columns={column};
    scaps_core::BatchOptions options;options.include_curves=true;
    for(int rep=0;rep<3;++rep) {
        const auto start=Clock::now();const auto result=scaps_core::run_batch(project,spec,options);const double wall=Seconds(start);
        ASSERT_EQ(result.rows.size(),1u);ASSERT_EQ(result.rows[0]->iv.size(),spec.voltages_V.size());
        for(const auto& p:result.rows[0]->iv)EXPECT_FALSE(p.profiles);
        std::printf("TYPED1 engine_typed rep=%d wall_s=%.9f elapsed_s=%.9f\n",rep,wall,result.elapsed_s);
    }
}
TEST(PerfProbe, WorkflowSweep5) {
    const auto path=std::getenv("TYPED1_SWEEP5");if(!path) GTEST_SKIP();
    for(int rep=0;rep<3;++rep) {
        const auto workers=std::getenv("TYPED1_WORKERS");Workflow w(CreateRef<JobSystem>(nullptr,workers?std::atoi(workers):6));w.lightResponse=true;w.autoRecalculate=false;w.autoRecalculateBand=false;
        const auto wait=[&] {while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
        ASSERT_TRUE(w.Dispatch({{"operation","project_load"},{"path",path}}).at("ok").get<bool>());wait();
        ASSERT_TRUE(w.response.value("ok",false))<<w.response;
        for(const auto &d:w.project.devices) if(d->name=="light") w.project.active_device=d->id;
        const auto start=Clock::now();const double cpu=Cpu();double last=0;
        ASSERT_TRUE(w.Dispatch({{"operation","run_batch"}}).at("ok").get<bool>());const auto id=w.ActiveJob();
        while(w.Pending()) {
            const auto job=w.jobs->GetJob(id);
            if(job && job->totalWork>0 && job->completedWork>=job->totalWork && !last)last=Seconds(start);
            w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double wall=Seconds(start),cpuSeconds=Cpu()-cpu;
        std::printf("TYPED1 sweep5 rep=%d wall_s=%.9f post_s=%.9f peak_mb=%.3f cores=%.3f status=%s\n",rep,wall,last?wall-last:-1,PeakMB(),cpuSeconds/wall,w.status.c_str());std::fflush(stdout);
        EXPECT_TRUE(w.response.value("ok",false))<<w.response;
    }
}
TEST(TypedBatch, GoldenJsonAdapter) {
    auto p=scaps_core::make_default_device_project();p.layers.resize(1);p.layers[0].defects.clear();p.layers[0].Nd_m3=1e21;p.layers[0].Na_m3=0;p.layers[0].thickness_m=1e-5;
    p.left_contact.flat_band=p.right_contact.flat_band=true;p.mesh.points_per_layer=11;
    const auto strip=[](Json& table){table.erase("elapsed_s");for(auto& row:table["rows"])row.erase("elapsed_s");};
    Json actual=Json::array();
    for(int i=0;i<3;++i) {
        auto project=p;if(i==1){project.illumination.enabled=true;project.illumination.uniform_generation_m3s=1e24;}
        Json spec={{"include_curves",true},{"voltages_V",{0.,.01}},{"threads",1},{"columns",RecorderDefaults()}};
        if(i==2)spec["axes"]=Json::array({{{"path","temperature_K"},{"values",{290.,310.}}},{{"path","layers[0].doping.Nd_m3"},{"values",{1e20,1e21}}}});
        auto table=Json::parse(scaps_core::run_batch_json(scaps_core::serialize_device_project_yaml(project),spec.dump()));strip(table);actual.push_back(std::move(table));
    }
    const auto path=std::filesystem::path(SCAPSLAB_TUTORIALS).parent_path()/"tests/fixtures/typed-batch-golden.json";
    if(std::getenv("TYPED1_CAPTURE")){std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<actual.dump();return;}
    std::ifstream input(path);ASSERT_TRUE(input.good());Json expected;input>>expected;EXPECT_EQ(actual,expected);
}
TEST(TypedBatch, RecorderAcquiresOnlyRequestedProfile) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.Current().document["mesh"]["points_per_layer"]=21;w.Current().document["voltage"]={{"start_V",0.},{"stop_V",.01},{"step_V",.01}};
    const auto execute=[&](Json request){EXPECT_TRUE(w.Dispatch(request).value("ok",false));while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}EXPECT_TRUE(w.response.value("ok",false))<<w.response;};
    auto spec=BatchDefaults();spec["axes"][0]={{"parameter_id","temperature_K"},{"values",{300.}}};execute({{"operation","run_batch"},{"spec",spec}});
    for(const auto& p:w.Current().batchResult.typed()->rows[0]->iv)EXPECT_FALSE(p.profiles);
    execute({{"operation","record_batch"},{"columns",Json::array({{{"id","n"},{"quantity","profile"},{"field","n_m3"},{"voltage_V",0.},{"x_m",0.}}})}});
    const auto row=w.Current().batchResult.typed()->rows[0];ASSERT_TRUE(row->iv[0].profiles);EXPECT_EQ(row->iv[0].profiles->fields.size(),1u);EXPECT_FALSE(row->iv[1].profiles);
    scaps_core::BatchSpec expected;expected.voltages_V={0.,.01};scaps_core::BatchOptions options;options.include_curves=true;options.profile_biases={0.};options.profile_fields={"n_m3"};
    const auto direct=scaps_core::run_batch(Model(w.Current().document),expected,options);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(row->values.at("n").value),std::bit_cast<std::uint64_t>(direct.rows[0]->iv[0].profiles->fields.at("n_m3")[0]));
}
TEST(TypedBoundary, IntegerFileNumbersRemainLosslessAndAvailableToPlots) {
    const Json iv={{"mesh_x_m",{0,1}},{"points",Json::array({{{"voltage_V",0},{"J_A_m2",1},{"x_m",{0,1}},{"Ec_eV",{2,3}},{"valid",true}}})}};
    const auto result=IvFromJson(iv);EXPECT_EQ(result.Dump().dump(),iv.dump());ASSERT_EQ(result.points.size(),1u);EXPECT_EQ(result.points[0].J_A_m2,1.);EXPECT_EQ(result.points[0].Ec_eV.size(),2u);EXPECT_EQ(result.mesh_x_m.size(),2u);
    const Json ac={{"kind","qe"},{"mesh_x_m",{0,1}},{"points",Json::array({{{"wavelength_nm",500},{"EQE",1},{"valid",true}}})}};
    const auto response=AcFromJson(ac);EXPECT_EQ(response.Dump().dump(),ac.dump());ASSERT_EQ(response.points.size(),1u);EXPECT_EQ(response.points[0].EQE,1.);
}
TEST(TypedBatch, InitialWorkingPointPreservedAtJsonEdge) {
    auto project=scaps_core::make_default_device_project();project.layers.resize(1);project.layers[0].defects.clear();project.mesh.points_per_layer=11;
    Json spec={{"voltages_V",{0.,.01}},{"include_curves",true},{"initial_state",{{"temperature_K",320.},{"voltage_V",.02},{"generation_total_m2_s",0.}}},{"export_initial_state_wp2",true}};
    scaps_core::BatchSpec input;input.voltages_V={0.,.01};input.initial_state=BatchInitialStateModel(project,spec);scaps_core::BatchOptions options;options.include_curves=true;options.profile_biases=input.voltages_V;
    const auto actual=BatchRowJson(scaps_core::run_batch_member(project,input,options));
    const auto expected=Json::parse(scaps_core::run_batch_json(scaps_core::serialize_device_project_yaml(project),spec.dump())).at("rows")[0];
    for(const auto* key:{"initial_state","initial_state_status","initial_state_hash","initial_state_wp2","curves"})EXPECT_EQ(actual.at(key),expected.at(key))<<key;
}
TEST(TypedBatch, RecorderAndRetryPreserveOpaqueLegacyFields) {
    Workflow w(CreateRef<JobSystem>(nullptr,2));w.lightResponse=true;w.autoRecalculate=false;w.autoRecalculateBand=false;
    w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.Current().document["mesh"]["points_per_layer"]=11;w.Current().document["voltage"]={{"start_V",0.},{"stop_V",.01},{"step_V",.01}};
    const auto execute=[&](Json request){ASSERT_TRUE(w.Dispatch(request).value("ok",false));while(w.Pending()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_TRUE(w.response.value("ok",false))<<w.response;};
    auto spec=BatchDefaults();spec["axes"][0]={{"parameter_id","temperature_K"},{"values",{300.,301.}}};execute({{"operation","run_batch"},{"spec",spec}});
    auto saved=w.Current().batchResult.typed()->Dump();
    for(auto& row:saved["rows"]){row["future_field"]={{"integer",7},{"unit","future"}};row["curves"]["iv"][0]["future_sample"]=42;row["initial_state"]={{"opaque_extension","preserve"}};}
    auto original=CreateRef<BatchResult>(BatchFromJson(saved));ASSERT_FALSE(original->native);w.Current().batchResult.adopt(original);
    execute({{"operation","record_batch"},{"columns",Json::array({{{"id","again"},{"quantity","J"},{"voltage_V",0.}}})}});
    const auto recorded=w.Current().batchResult.typed()->Dump();
    for(std::size_t i=0;i<2;++i)for(const auto* key:{"future_field","initial_state","curves"})EXPECT_EQ(recorded["rows"][i][key].dump(),saved["rows"][i][key].dump())<<key;
    EXPECT_EQ(w.Current().batchResult.typed()->legacyBase,original);EXPECT_FALSE(original->HasView());
    execute({{"operation","retry_batch"},{"member_id","point_1"}});
    const auto retried=w.Current().batchResult.typed()->Dump();EXPECT_EQ(retried["rows"][0],recorded["rows"][0]);EXPECT_FALSE(retried["rows"][1].contains("future_field"));EXPECT_EQ(retried["rows"][1]["attempt"],2);
}

