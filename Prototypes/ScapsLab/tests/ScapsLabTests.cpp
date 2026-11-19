#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <scaps_core/api.hpp>
#include <scaps_core/device_project_yaml.hpp>
#include <thread>
#include <fstream>
using namespace SST;
using namespace SST::ScapsLab;
namespace {
Json Execute(Workflow &w,const Json &r) {
    const auto started=w.Dispatch(r); if(!started.at("ok").get<bool>()) return started;
    while(w.Busy()) {w.OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(1));} return w.response;
}
std::filesystem::path Temp(const char *name) {return std::filesystem::temp_directory_path()/name;}
}
TEST(ScapsLab,YamlFullMappingRoundtrip) {
    auto d=ReadProject(SCAPSLAB_PROJECT);
    d["future_extension"]={{"numeric_string","001"},{"nullable",nullptr},{"list",{1,true,".inf"}}};
    d["layers"][0]["defects"]=Json::array({{{"label","retained"},{"energy_reference","relative_to_Ei"},{"energy_offset_eV",0.1},{"Nt_m3",1e15},{"sigma_n_m2",1e-19},{"sigma_p_m2",1e-19},{"charge_type","neutral"}}});
    const auto path=Temp("scapslab-roundtrip.yaml");
    WriteProject(d,path.string()); EXPECT_EQ(ReadProject(path.string()),d);
    const auto original=scaps_core::load_device_project_yaml(SCAPSLAB_PROJECT);
    auto plain=ReadProject(SCAPSLAB_PROJECT); WriteProject(plain,path.string());
    const auto round=scaps_core::load_device_project_yaml(path);
    EXPECT_EQ(round.layers[0].material.chi_eV,original.layers[0].material.chi_eV);
    EXPECT_EQ(round.left_contact.Sn_mps,original.left_contact.Sn_mps);
    std::filesystem::remove(path);
}
TEST(ScapsLab,SolverModesAndContactMapping) {
    auto d=ReadProject(SCAPSLAB_PROJECT);
    for(const auto *level:{"0","1","2","3","A"}) {
        d["mesh"]["adaptivity_level"]=level; EXPECT_EQ(Model(d).mesh.adaptivity_level,level);
    }
    for(const auto *mode:{"constant","sqrt_T"}) {
        d["mobility_temperature_model"]=mode; EXPECT_EQ(scaps_core::mobility_model_name(Model(d).mobility_temperature_model),std::string(mode));
    }
    d["mobility_temperature_model"]="caughey_thomas_T"; EXPECT_THROW((void)Model(d),std::exception);
    d["layers"][0]["material"]["caughey_thomas"]={{"source","synthetic unit-test parameters"},{"T_ref_K",300},{"alpha",1.0},{"beta_min",0.0},{"beta_N",0.0},{"beta_alpha",0.0},
        {"electron",{{"mu_min_m2_V_s",0.001},{"mu_delta_m2_V_s",0.01},{"N_ref_m3",1e23},{"beta_delta",0.0}}},
        {"hole",{{"mu_min_m2_V_s",0.001},{"mu_delta_m2_V_s",0.01},{"N_ref_m3",1e23},{"beta_delta",0.0}}}};
    EXPECT_EQ(Model(d).mobility_temperature_model,scaps_core::MobilityTemperatureModel::CaugheyThomasT);
    d["contacts"]["left"]["Sn_m_s"]=".inf";d["contacts"]["left"].erase("flat_band");
    const auto path=Temp("scapslab-all-modes.yaml");WriteProject(d,path.string());EXPECT_EQ(ReadProject(path.string()),d);std::filesystem::remove(path);
    d["constants_mode"]="typo";EXPECT_THROW((void)Model(d),std::exception);
}
TEST(ScapsLab,SameSolveAsHeadlessKernelAndMesh) {
    const auto p=scaps_core::load_device_project_yaml(SCAPSLAB_PROJECT);
    const auto device=scaps_core::make_device_from_project(p); auto voltages=scaps_core::make_voltage_list(p.voltage);for(auto &v:voltages)v=-v;
    const auto pc=scaps_core::physical_constants(p.constants_mode);
    const auto mesh=scaps_core::prepare_mesh(device,p.mesh,voltages,{p.temperature_K},p.solver,pc);
    const auto expected=scaps_core::solve_iv(device,mesh,voltages,p.temperature_K,p.solver,pc);
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);
    const auto r=Execute(w,{{"operation","run"}}); ASSERT_TRUE(r.at("ok").get<bool>())<<r.dump();
    EXPECT_EQ(w.result.at("mesh_x_m").get<std::vector<double>>(),mesh.x_m);
    ASSERT_EQ(w.result.at("points").size(),expected.size());
    for(std::size_t i=0;i<expected.size();++i) {
        const auto &point=w.result.at("points")[i]; const auto &e=expected[i];
        EXPECT_EQ(point.at("status"),scaps_core::to_string(e.status));
        EXPECT_EQ(point.at("x_m").get<std::vector<double>>(),e.state.x_m.empty()?mesh.x_m:e.state.x_m);
        if(e.status==scaps_core::SolverStatus::Converged) {
            const double current=e.state.Jn_Am2.front()+e.state.Jp_Am2.front();
            EXPECT_NEAR(point.at("J_A_m2").get<double>(),current,std::abs(current)*1e-10);
        } else EXPECT_TRUE(point.at("J_A_m2").is_null());
    }
}
TEST(ScapsLab,EditsStableIdsValidationAndCancellation) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);
    const auto original=w.document;
    ASSERT_TRUE(Execute(w,{{"operation","layer"},{"action","add"}}).at("ok").get<bool>());
    const auto id=w.document["layers"][1]["scapslab_id"];
    ASSERT_TRUE(Execute(w,{{"operation","layer"},{"action","move"},{"layer_id",id},{"index",0}}).at("ok").get<bool>());
    EXPECT_EQ(w.document["layers"][0]["scapslab_id"],id);
    ASSERT_TRUE(Execute(w,{{"operation","layer"},{"action","remove"},{"layer_id",id}}).at("ok").get<bool>());
    EXPECT_EQ(w.document,original);
    EXPECT_FALSE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",-1}}}}).at("ok").get<bool>()); EXPECT_EQ(w.document,original);
    ASSERT_TRUE(w.jobs->PauseAllJobs()); ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","cancel"}}).at("ok").get<bool>()); ASSERT_TRUE(w.jobs->ResumeAllJobs());
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.response.at("error").at("code"),"cancelled"); EXPECT_TRUE(w.result.empty());
    auto bad=original;bad["temperature_K"]=-1;const auto path=Temp("scapslab-protected.yaml");
    std::string error,text;ASSERT_TRUE(TextFileIO::Save(Path{path},"original",error));EXPECT_THROW(WriteProject(bad,path.string()),std::exception);
    ASSERT_TRUE(TextFileIO::Load(Path{path},text,error));EXPECT_EQ(text,"original");std::filesystem::remove(path);
}
TEST(ScapsLab,EmbeddedPythonRunAndHeadlessCsv) {
#if DS_PYTHON_CAPI_AVAILABLE
    RegisterPython(); PythonInterpreter python; ASSERT_TRUE(python.Start());
    const auto gil=PythonInterpreter::AcquireRuntimeGil();
    const auto code=std::string("import sys\nsys.path.insert(0, ")+Json(SCAPSLAB_PYTHON).dump()+")\nproject_path="+Json(SCAPSLAB_PROJECT).dump()+"\nheadless_path="+Json(SCAPSLAB_HEADLESS).dump()+"\nexec(compile(open("+Json(SCAPSLAB_PYTHON_TEST).dump()+", encoding='utf-8-sig').read(), 'test_headless.py', 'exec'))\n";
    const int result=PyRun_SimpleString(code.c_str());
    PythonInterpreter::ReleaseRuntimeGil(gil); EXPECT_EQ(result,0);
#else
    FAIL()<<"Embedded Python is required";
#endif
}



TEST(ScapsLab,ACMatchesCoreAndExports) {
    using namespace scaps_core;
    const auto p=load_device_project_yaml(SCAPSLAB_PROJECT);
    const auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
    Workflow w(CreateRef<JobSystem>(nullptr,1)); w.document=ReadProject(SCAPSLAB_PROJECT);
    for(const auto *kind:{"cv","cf"}) {
        const Json request={{"operation",kind},{"start_V",-0.2},{"stop_V",0.2},{"step_V",0.2},{"frequency_Hz",1e4},
            {"voltage_V",0.1},{"start_Hz",10.0},{"stop_Hz",1e5},{"points",5},{"temperature_K",p.temperature_K}};
        const auto response=Execute(w,request); ASSERT_TRUE(response.at("ok").get<bool>())<<response.dump();
        const auto &result=w.acResults.at(kind); const auto &points=result.at("points");
        std::vector<double> volts,frequencies;
        for(const auto &point:points) {
            if(std::string(kind)=="cv") volts.push_back(-point.at("voltage_V").get<double>());
            else frequencies.push_back(point.at("frequency_Hz"));
        }
        if(std::string(kind)=="cf") volts={-0.1};
        const auto mesh=prepare_mesh(device,p.mesh,volts,{p.temperature_K},p.solver,pc);
        const auto expected=std::string(kind)=="cv"?solve_cv(device,mesh,volts,p.temperature_K,1e4,p.solver,pc):solve_cf(device,mesh,-0.1,p.temperature_K,frequencies,p.solver,pc);
        EXPECT_EQ(result.at("mesh_x_m").get<std::vector<double>>(),mesh.x_m);
        ASSERT_EQ(points.size(),expected.size());
        for(std::size_t i=0;i<expected.size();++i) {
            const auto &e=expected[i]; const auto &actual=points[i];
            ASSERT_EQ(e.status,SolverStatus::Converged); EXPECT_EQ(actual.at("status"),to_string(e.status));
            EXPECT_EQ(actual.at("voltage_V"),-e.voltage_V); EXPECT_EQ(actual.at("frequency_Hz"),e.frequency_Hz);
            EXPECT_NEAR(actual.at("capacitance_F").get<double>(),e.C_F,std::abs(e.C_F)*1e-12);
            EXPECT_NEAR(actual.at("conductance_S").get<double>(),e.G_S,std::abs(e.G_S)*1e-12);
        }
        const auto csv=Temp("scapslab-ac.csv");
        ASSERT_TRUE(Execute(w,{{"operation","export_ac"},{"kind",kind},{"path",csv.string()}}).at("ok").get<bool>());
        std::ifstream stream(csv); std::string line; ASSERT_TRUE(bool(std::getline(stream,line)));
        EXPECT_EQ(line,"point_id,voltage_V,frequency_Hz,temperature_K,capacitance_F,conductance_S,G_over_omega_F,inverse_C2_F_minus2,status");
        std::size_t rows=0; while(std::getline(stream,line)) ++rows; EXPECT_EQ(rows,points.size()); stream.close(); std::filesystem::remove(csv);
    }
    const auto prior=w.acResults;
    EXPECT_FALSE(Execute(w,{{"operation","cf"},{"temperature_K",300},{"voltage_V",0},{"start_Hz",0},{"stop_Hz",1e6},{"points",10}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","cv"},{"temperature_K",300},{"start_V",0},{"stop_V",1},{"step_V",-0.1},{"frequency_Hz",1e3}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","cv"},{"temperature_K",300},{"start_V",1e20},{"stop_V",1e20+1e6},{"step_V",1000.0},{"frequency_Hz",1e3}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","cf"},{"temperature_K",300},{"voltage_V",0},{"start_Hz",1},{"stop_Hz",1e6},{"points",4294967299LL}}).at("ok").get<bool>());
    EXPECT_EQ(w.acResults,prior);
    ASSERT_TRUE(w.jobs->PauseAllJobs()); ASSERT_TRUE(w.Dispatch({{"operation","cf"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","cancel"}}).at("ok").get<bool>()); ASSERT_TRUE(w.jobs->ResumeAllJobs());
    while(w.Busy()) {w.OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.response.at("error").at("code"),"cancelled"); EXPECT_EQ(w.acResults,prior);
}

TEST(ScapsLab,UndoMergesOnlyOneGesture) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);const auto original=w.document;
    for(int temperature:{301,302,303}) ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",temperature}}},{"gesture_id","drag-1"}}).at("ok").get<bool>());
    EXPECT_EQ(w.undo.GetUndoDepth(),1u);
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).at("ok").get<bool>());EXPECT_EQ(w.document,original);
    ASSERT_TRUE(Execute(w,{{"operation","redo"}}).at("ok").get<bool>());EXPECT_EQ(w.document.at("temperature_K"),303);
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",304}}},{"gesture_id","drag-2"}}).at("ok").get<bool>());
    EXPECT_EQ(w.undo.GetUndoDepth(),2u);
    EXPECT_FALSE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",-1}}}}).at("ok").get<bool>());
    EXPECT_EQ(w.undo.GetUndoDepth(),2u);
}
TEST(ScapsLab,AutoRecalculateCancelsAndDebounces) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(w.Dispatch({{"operation","auto_recalculate"},{"enabled",true}}).at("ok").get<bool>());
    w.OnUpdate(0);EXPECT_FALSE(w.Busy());
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    const auto obsolete=w.active;
    ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"patch",{{"temperature_K",301}}}}).at("ok").get<bool>());
    ASSERT_TRUE(w.jobs->ResumeAllJobs());
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.jobs->GetJob(obsolete)->status,JobStatus::Cancelled);EXPECT_EQ(w.document.at("temperature_K"),301);
    w.interacting=true;w.changedAt-=std::chrono::seconds(1);w.OnUpdate(0);EXPECT_FALSE(w.Busy());
    w.interacting=false;w.changedAt=std::chrono::steady_clock::now();const auto start=w.changedAt;
    while(!w.Busy() && std::chrono::steady_clock::now()-start<std::chrono::seconds(5)) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.Busy());
    EXPECT_GE((std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()),250.0);
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.at("ok").get<bool>())<<w.response.dump();
    EXPECT_GT(w.result.at("compute_time_ms").get<double>(),0);EXPECT_EQ(w.result.at("mesh_node_count"),w.result.at("mesh_x_m").size());
    std::cout<<"Auto-recalculate compute_ms="<<w.result.at("compute_time_ms")<<"; edit-release-to-result_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"; debounce_ms=250\n";
    w.OnUpdate(0);EXPECT_FALSE(w.Busy());
}

TEST(ScapsLab,BandRecalculationUsesSelectedVoltageAndPreservesSweep) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(Execute(w,{{"operation","run"}}).at("ok").get<bool>());
    const auto sweep=w.result;
    ASSERT_TRUE(Execute(w,{{"operation","band"},{"voltage_V",0.1}}).at("ok").get<bool>());
    EXPECT_EQ(w.result,sweep);ASSERT_EQ(w.bandResult.at("points").size(),1u);
    EXPECT_EQ(w.bandResult.at("points")[0].at("voltage_V"),0.1);
    EXPECT_GT(w.bandResult.at("mesh_node_count").get<int>(),0);
}

TEST(ScapsLab,QueuedEditsKeepSeparateGestures) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    for(int value:{301,302}) ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"patch",{{"temperature_K",value}}},{"gesture_id","first"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"patch",{{"temperature_K",303}}},{"gesture_id","second"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.jobs->ResumeAllJobs());while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.document.at("temperature_K"),303);EXPECT_EQ(w.undo.GetUndoDepth(),2u);
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).at("ok").get<bool>());EXPECT_EQ(w.document.at("temperature_K"),302);
}

TEST(ScapsLab,FailedBiasPointsRemainMaskedAndIdentifiable) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);
    w.document["mesh"]["adaptivity_level"]="0";w.document["solver"]["max_newton_iterations"]=1;
    w.document["solver"]["residual_tolerance"]=1e-30;
    w.document["voltage"]={{"start_V",0.0},{"stop_V",0.8},{"step_V",0.4}};
    const auto response=Execute(w,{{"operation","run"}});ASSERT_TRUE(response.at("ok").get<bool>())<<response.dump();
    ASSERT_EQ(w.result.at("points").size(),3u);int failed=0;
    for(const auto &point:w.result.at("points")) {
        EXPECT_TRUE(point.contains("bias_id"));
        if(point.at("status")!="Converged") {++failed;EXPECT_TRUE(point.at("J_A_m2").is_null());}
    }
    EXPECT_GT(failed,0);
}

TEST(ScapsLab,StructuralChangesDoNotAcceptStaleEditorSnapshots) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.document=ReadProject(SCAPSLAB_PROJECT);const auto count=w.document.at("layers").size();
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","layer"},{"action","add"}}).at("ok").get<bool>());
    EXPECT_FALSE(w.Dispatch({{"operation","edit"},{"document",w.document}}).at("ok").get<bool>());
    ASSERT_TRUE(w.jobs->ResumeAllJobs());while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.document.at("layers").size(),count+1);
}
