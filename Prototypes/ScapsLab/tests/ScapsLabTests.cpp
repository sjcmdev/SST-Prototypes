#include <gtest/gtest.h>
#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
#include <scaps_core/api.hpp>
#include <scaps_core/grading.hpp>
#include <sst/plotlab/Engine.hpp>
#include <scaps_core/device_project_yaml.hpp>
#include <thread>
#include <fstream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#undef GetJob
#endif
SST::ScapsLab::Json RunLightViaUi();
SST::ScapsLab::Json RunBatchViaUi(const SST::ScapsLab::Json &document,const SST::ScapsLab::Json &spec);
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
    d["layers"][0]["defects"]=Json::array({{{"scapslab_id","retained-defect"},{"label","retained"},{"energy_reference","relative_to_Ei"},{"energy_offset_eV",0.1},{"Nt_m3",1e15},{"sigma_n_m2",1e-19},{"sigma_p_m2",1e-19},{"charge_type","neutral"}}});
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
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    const auto r=Execute(w,{{"operation","run"}}); ASSERT_TRUE(r.at("ok").get<bool>())<<r.dump();
    EXPECT_EQ(w.Current().result.at("mesh_x_m").get<std::vector<double>>(),mesh.x_m);
    ASSERT_EQ(w.Current().result.at("points").size(),expected.size());
    for(std::size_t i=0;i<expected.size();++i) {
        const auto &point=w.Current().result.at("points")[i]; const auto &e=expected[i];
        EXPECT_EQ(point.at("status"),scaps_core::to_string(e.status));
        EXPECT_EQ(point.at("x_m").get<std::vector<double>>(),e.state.x_m.empty()?mesh.x_m:e.state.x_m);
        if(e.status==scaps_core::SolverStatus::Converged) {
            const double current=e.state.Jn_Am2.front()+e.state.Jp_Am2.front();
            EXPECT_NEAR(point.at("J_A_m2").get<double>(),current,std::abs(current)*1e-10);
        } else EXPECT_TRUE(point.at("J_A_m2").is_null());
    }
}
TEST(ScapsLab,EditsStableIdsValidationAndCancellation) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    const auto original=w.Current().document;
    ASSERT_TRUE(Execute(w,{{"operation","layer"},{"action","add"}}).at("ok").get<bool>());
    const auto id=w.Current().document["layers"][1]["scapslab_id"];
    ASSERT_TRUE(Execute(w,{{"operation","layer"},{"action","move"},{"layer_id",id},{"index",0}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().document["layers"][0]["scapslab_id"],id);
    ASSERT_TRUE(Execute(w,{{"operation","layer"},{"action","remove"},{"layer_id",id}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().document,original);
    EXPECT_FALSE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",-1}}}}).at("ok").get<bool>()); EXPECT_EQ(w.Current().document,original);
    ASSERT_TRUE(w.jobs->PauseAllJobs()); ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","cancel"}}).at("ok").get<bool>()); ASSERT_TRUE(w.jobs->ResumeAllJobs());
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.response.at("error").at("code"),"cancelled"); EXPECT_TRUE(w.Current().result.empty());
    auto bad=original;bad["temperature_K"]=-1;const auto path=Temp("scapslab-protected.yaml");
    std::string error,text;ASSERT_TRUE(TextFileIO::Save(Path{path},"original",error));EXPECT_THROW(WriteProject(bad,path.string()),std::exception);
    ASSERT_TRUE(TextFileIO::Load(Path{path},text,error));EXPECT_EQ(text,"original");std::filesystem::remove(path);
}
TEST(ScapsLab,EmbeddedPythonRunAndHeadlessCsv) {
#if DS_PYTHON_CAPI_AVAILABLE
    const auto uiDocument=ReadProject(SCAPSLAB_PROJECT);auto uiSpec=BatchDefaults();uiSpec["voltages_V"]=Json::array({0.,0.05});
    const auto uiBatch=RunBatchViaUi(uiDocument,uiSpec);
    const auto uiLight=RunLightViaUi();
    RegisterPython(); PythonInterpreter python; ASSERT_TRUE(python.Start());
    const auto gil=PythonInterpreter::AcquireRuntimeGil();
    const auto code=std::string("import json\nui_light=json.loads(")+Json(uiLight.dump()).dump()+")\nui_batch=json.loads("+Json(uiBatch.dump()).dump()+")\nui_document=json.loads("+Json(uiDocument.dump()).dump()+")\nui_spec=json.loads("+Json(uiSpec.dump()).dump()+")\nimport sys\nsys.path.insert(0, "+Json(SCAPSLAB_PYTHON).dump()+")\nproject_path="+Json(SCAPSLAB_PROJECT).dump()+"\nexamples_path="+Json(ExamplesDirectory().string()).dump()+"\nheadless_path="+Json(SCAPSLAB_HEADLESS).dump()+"\nexec(compile(open("+Json(SCAPSLAB_PYTHON_TEST).dump()+", encoding='utf-8-sig').read(), 'test_headless.py', 'exec'))\n";
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
    Workflow w(CreateRef<JobSystem>(nullptr,1)); w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    for(const auto *kind:{"cv","cf"}) {
        const Json request={{"operation",kind},{"start_V",-0.2},{"stop_V",0.2},{"step_V",0.2},{"frequency_Hz",1e4},
            {"voltage_V",0.1},{"start_Hz",10.0},{"stop_Hz",1e5},{"points",5},{"temperature_K",p.temperature_K}};
        const auto response=Execute(w,request); ASSERT_TRUE(response.at("ok").get<bool>())<<response.dump();
        const auto &result=w.Current().acResults.at(kind); const auto &points=result.at("points");
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
        EXPECT_EQ(line,"point_id,voltage_V,frequency_Hz,temperature_K,capacitance_F,conductance_S,G_over_omega_F,inverse_C2_F_minus2,status,valid,validity_status,conservation_relative,area_m2");
        std::size_t rows=0; while(std::getline(stream,line)) ++rows; EXPECT_EQ(rows,points.size()); stream.close(); std::filesystem::remove(csv);
    }
    const auto prior=w.Current().acResults;
    EXPECT_FALSE(Execute(w,{{"operation","cf"},{"temperature_K",300},{"voltage_V",0},{"start_Hz",0},{"stop_Hz",1e6},{"points",10}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","cv"},{"temperature_K",300},{"start_V",0},{"stop_V",1},{"step_V",-0.1},{"frequency_Hz",1e3}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","cv"},{"temperature_K",300},{"start_V",1e20},{"stop_V",1e20+1e6},{"step_V",1000.0},{"frequency_Hz",1e3}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","cf"},{"temperature_K",300},{"voltage_V",0},{"start_Hz",1},{"stop_Hz",1e6},{"points",4294967299LL}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().acResults,prior);
    ASSERT_TRUE(w.jobs->PauseAllJobs()); ASSERT_TRUE(w.Dispatch({{"operation","cf"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","cancel"}}).at("ok").get<bool>()); ASSERT_TRUE(w.jobs->ResumeAllJobs());
    while(w.Busy()) {w.OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.response.at("error").at("code"),"cancelled"); EXPECT_EQ(w.Current().acResults,prior);
}

TEST(ScapsLab,UndoMergesOnlyOneGesture) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);const auto original=w.Current().document;
    for(int temperature:{301,302,303}) ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",temperature}}},{"gesture_id","drag-1"}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().undo.GetUndoDepth(),1u);
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).at("ok").get<bool>());EXPECT_EQ(w.Current().document,original);
    ASSERT_TRUE(Execute(w,{{"operation","redo"}}).at("ok").get<bool>());EXPECT_EQ(w.Current().document.at("temperature_K"),303);
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",304}}},{"gesture_id","drag-2"}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().undo.GetUndoDepth(),2u);
    EXPECT_FALSE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",-1}}}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().undo.GetUndoDepth(),2u);
}
TEST(ScapsLab,AutoRecalculateCancelsAndDebounces) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(w.Dispatch({{"operation","auto_recalculate"},{"enabled",true}}).at("ok").get<bool>());
    w.OnUpdate(0);EXPECT_FALSE(w.Busy());
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    const auto obsolete=w.active;
    ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"patch",{{"temperature_K",301}}}}).at("ok").get<bool>());
    ASSERT_TRUE(w.jobs->ResumeAllJobs());
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.jobs->GetJob(obsolete)->status,JobStatus::Cancelled);EXPECT_EQ(w.Current().document.at("temperature_K"),301);
    w.interacting=true;w.Current().changedAt-=std::chrono::seconds(1);w.OnUpdate(0);EXPECT_FALSE(w.Busy());
    w.interacting=false;w.Current().changedAt=std::chrono::steady_clock::now();const auto start=w.Current().changedAt;
    while(!w.Busy() && std::chrono::steady_clock::now()-start<std::chrono::seconds(5)) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.Busy());
    EXPECT_GE((std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()),250.0);
    while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(w.response.at("ok").get<bool>())<<w.response.dump();
    EXPECT_GT(w.Current().result.at("compute_time_ms").get<double>(),0);EXPECT_EQ(w.Current().result.at("mesh_node_count"),w.Current().result.at("mesh_x_m").size());
    std::cout<<"Auto-recalculate compute_ms="<<w.Current().result.at("compute_time_ms")<<"; edit-release-to-result_ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"; debounce_ms=250\n";
    w.OnUpdate(0);EXPECT_FALSE(w.Busy());
}

TEST(ScapsLab,BandRecalculationUsesSelectedVoltageAndPreservesSweep) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(Execute(w,{{"operation","run"}}).at("ok").get<bool>());
    const auto sweep=w.Current().result;
    ASSERT_TRUE(Execute(w,{{"operation","band"},{"voltage_V",0.1}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().result,sweep);ASSERT_EQ(w.Current().bandResult.at("points").size(),1u);
    EXPECT_EQ(w.Current().bandResult.at("points")[0].at("voltage_V"),0.1);
    EXPECT_GT(w.Current().bandResult.at("mesh_node_count").get<int>(),0);
}

TEST(ScapsLab,QueuedEditsKeepSeparateGestures) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    for(int value:{301,302}) ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"patch",{{"temperature_K",value}}},{"gesture_id","first"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","edit"},{"patch",{{"temperature_K",303}}},{"gesture_id","second"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.jobs->ResumeAllJobs());while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().document.at("temperature_K"),303);EXPECT_EQ(w.Current().undo.GetUndoDepth(),2u);
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).at("ok").get<bool>());EXPECT_EQ(w.Current().document.at("temperature_K"),302);
}

TEST(ScapsLab,FailedBiasPointsRemainMaskedAndIdentifiable) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    w.Current().document["mesh"]["adaptivity_level"]="0";w.Current().document["solver"]["max_newton_iterations"]=1;
    w.Current().document["solver"]["residual_tolerance"]=1e-30;
    w.Current().document["voltage"]={{"start_V",0.0},{"stop_V",0.8},{"step_V",0.4}};
    const auto response=Execute(w,{{"operation","run"}});ASSERT_TRUE(response.at("ok").get<bool>())<<response.dump();
    ASSERT_EQ(w.Current().result.at("points").size(),3u);int failed=0;
    for(const auto &point:w.Current().result.at("points")) {
        EXPECT_TRUE(point.contains("bias_id"));
        if(point.at("status")!="Converged") {
            ++failed;EXPECT_TRUE(point.at("J_A_m2").is_null());
            const auto &gr=point.at("generation_recombination");
            EXPECT_FALSE(gr.at("valid").get<bool>());EXPECT_FALSE(gr.at("terminal_budget_valid").get<bool>());
            for(const auto &channel:gr.at("channels")) EXPECT_FALSE(channel.at("valid").get<bool>());
        }
    }
    EXPECT_GT(failed,0);
}

TEST(ScapsLab,StructuralChangesDoNotAcceptStaleEditorSnapshots) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);const auto count=w.Current().document.at("layers").size();
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","layer"},{"action","add"}}).at("ok").get<bool>());
    EXPECT_FALSE(w.Dispatch({{"operation","edit"},{"document",w.Current().document}}).at("ok").get<bool>());
    ASSERT_TRUE(w.jobs->ResumeAllJobs());while(w.Busy()) {w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.Current().document.at("layers").size(),count+1);
}

TEST(ScapsLab,DefectEditorsPreviewResistanceRoundtripAndStaleExport) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    auto draft=w.Current().document;draft["layers"][0]["defects"]=Json::array();
    for(const auto *shape:{"single","uniform","gaussian","cb_tail","vb_tail"}) {
        auto d=NewDefect();d["scapslab_id"]=std::string("test-")+shape;d["energy_distribution"]=shape;d["Nt_m3"]=1e16;
        if(std::string(shape)=="cb_tail") d["energy_reference"]="relative_to_Ec";
        if(std::string(shape)=="vb_tail") d["energy_reference"]="relative_to_Ev";
        draft["layers"][0]["defects"].push_back(d);
    }
    auto second=draft["layers"][0];second["scapslab_id"]="layer-2";second["defects"]=Json::array();second["material"]["chi_eV"]=4.1;draft["layers"].push_back(second);
    auto sheet=NewDefect(true);sheet["energy_distribution"]="gaussian";sheet["energy_offset_eV"]=.5;sheet["Nt_m2"]=1e10;draft["interface_defects"]=Json::array({sheet});
    draft["external"]={{"area_m2",1e-4},{"series_ohm_m2",1e-5},{"shunt_ohm_m2",1.0}};
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"document",draft}}).at("ok").get<bool>());
    const auto path=Temp("scapslab-editors.yaml");ASSERT_TRUE(Execute(w,{{"operation","save"},{"path",path.string()}}).at("ok").get<bool>());EXPECT_EQ(ReadProject(path.string()),draft);
    ASSERT_TRUE(Execute(w,{{"operation","band"},{"voltage_V",0.05}}).at("ok").get<bool>())<<w.response.dump();
    const auto &point=w.Current().bandResult.at("points")[0];EXPECT_EQ(point.at("defect_previews").size(),6u);
    for(const auto &curve:point.at("defect_previews")) {
        EXPECT_EQ(curve.at("band_edges_eV").size(),curve.at("sheet").get<bool>()?4u:2u);
        EXPECT_FALSE(curve.at("occupation").empty());
        for(const auto &f:curve.at("occupation")) if(f.is_number()) {EXPECT_GE(f.get<double>(),0);EXPECT_LE(f.get<double>(),1);}
        if(!curve.at("single").get<bool>()) {EXPECT_EQ(curve.at("energy_eV").size(),401u);for(const auto &n:curve.at(curve.at("sheet").get<bool>()?"density_m2_eV":"density_m3_eV")) EXPECT_GE(n.get<double>(),0);}
    }
    EXPECT_EQ(w.Current().bandResult.at("project_snapshot"),draft);EXPECT_TRUE(w.Current().bandResult.contains("validity_status"));
    EXPECT_FALSE(Execute(w,{{"operation","layer"},{"action","remove"},{"layer_id","layer-2"}}).at("ok").get<bool>());
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",301}}}}).at("ok").get<bool>());EXPECT_FALSE(w.Current().bandResult.empty());
    EXPECT_FALSE(Execute(w,{{"operation","export_ac"},{"kind","cv"},{"path",Temp("stale.csv").string()}}).at("ok").get<bool>());
    std::filesystem::remove(path);
}

TEST(ScapsLab,AmphotericPreviewAndDuplicateDefectIds) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    auto d=NewDefect();d["amphoteric"]=true;d["Nt_m3"]=1e16;
    auto draft=w.Current().document;draft["layers"][0]["defects"]=Json::array({d,d});EXPECT_THROW((void)Model(draft),std::invalid_argument);
    draft["layers"][0]["defects"]=Json::array({d});ASSERT_TRUE(Execute(w,{{"operation","edit"},{"document",draft}}).at("ok").get<bool>());
    ASSERT_TRUE(Execute(w,{{"operation","band"},{"voltage_V",0.05}}).at("ok").get<bool>())<<w.response.dump();
    const auto &c=w.Current().bandResult.at("points")[0].at("defect_previews")[0];EXPECT_TRUE(c.at("amphoteric").get<bool>());EXPECT_TRUE(c.contains("second_level_energy_eV"));
    ASSERT_EQ(c.at("occupation").size(),1u);EXPECT_GE(c.at("occupation")[0].get<double>(),0);EXPECT_LE(c.at("occupation")[0].get<double>(),1);
}

TEST(ScapsLab,ChangingSheetRepresentationPreservesDensity) {
    auto d=NewDefect(true);const double original=d.at("Nt_m2");
    ChangeSheetDistribution(d,"gaussian","scaps_peak");EXPECT_GT(d.at("peak_density_m2_eV").get<double>(),0);EXPECT_EQ(d.at("Nt_m2"),0.);
    ChangeSheetDistribution(d,"uniform","integrated_sheet");EXPECT_NEAR(d.at("Nt_m2").get<double>(),original,original*1e-15);EXPECT_EQ(d.at("peak_density_m2_eV"),0.);
    ChangeSheetDistribution(d,"gaussian","scaps_peak");ChangeSheetDistribution(d,"single","scaps_peak");EXPECT_NEAR(d.at("Nt_m2").get<double>(),original,original*1e-15);EXPECT_EQ(d.at("peak_density_m2_eV"),0.);
}

TEST(ScapsLab,DefectPreviewFollowsSelectedBiasInsteadOfStaleBand) {
    const Json sweep={{"points",Json::array({{{"voltage_V",0.0},{"bias_id","iv-0"}},{{"voltage_V",0.1},{"bias_id","iv-1"}}})}};
    Json band={{"points",Json::array({{{"voltage_V",0.0},{"bias_id","band"}}})}};
    EXPECT_EQ(PointAtBias(sweep,band,1).at("bias_id"),"iv-1");
    band["points"][0]["voltage_V"]=0.1;EXPECT_EQ(PointAtBias(sweep,band,1).at("bias_id"),"band");
    EXPECT_EQ(PointAtBias(Json::object(),band,0).at("bias_id"),"band");
    EXPECT_EQ(PointAtBias(sweep,Json::object(),999).at("bias_id"),"iv-1");
}

TEST(ScapsLab,MultiDeviceIsolationHistoryAndOwnership) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);const auto first=w.project.active_device;
    ASSERT_TRUE(Execute(w,{{"operation","run"}}).at("ok").get<bool>());const auto firstResult=w.Current().result;
    ASSERT_TRUE(Execute(w,{{"operation","cf"},{"temperature_K",300},{"voltage_V",0},{"start_Hz",10},{"stop_Hz",1e4},{"points",3}}).at("ok").get<bool>());const auto firstAc=w.Current().acResults;
    ASSERT_TRUE(Execute(w,{{"operation","device_duplicate"},{"name","Second"}}).at("ok").get<bool>())<<w.response.dump();const auto second=w.project.active_device;
    EXPECT_NE(first,second);EXPECT_TRUE(w.Current().result.empty());EXPECT_TRUE(w.Current().acResults.empty());
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",301}}}}).at("ok").get<bool>());
    EXPECT_EQ(w.Device(first).result,firstResult);EXPECT_EQ(w.Device(first).acResults,firstAc);EXPECT_EQ(w.Current().undo.GetUndoDepth(),1u);EXPECT_TRUE(w.Current().dirty);
    ASSERT_TRUE(Execute(w,{{"operation","device_select"},{"device_id",first}}).at("ok").get<bool>());
    EXPECT_EQ(w.Current().undo.GetUndoDepth(),0u);EXPECT_FALSE(w.Current().dirty);
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","device_select"},{"device_id",second}}).at("ok").get<bool>());
    ASSERT_TRUE(w.jobs->ResumeAllJobs());while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_TRUE(w.Current().result.empty());EXPECT_EQ(w.Device(first).result.at("points"),firstResult.at("points"));
    ASSERT_TRUE(Execute(w,{{"operation","device_remove"},{"device_id",second}}).at("ok").get<bool>());
    EXPECT_EQ(w.project.devices.size(),1u);ASSERT_TRUE(Execute(w,{{"operation","undo"}}).at("ok").get<bool>());
    EXPECT_EQ(w.project.devices.size(),2u);EXPECT_EQ(w.Device(second).undo.GetUndoDepth(),1u);
    ASSERT_TRUE(Execute(w,{{"operation","device_select"},{"device_id",second}}).at("ok").get<bool>());w.preferProjectUndo=false;
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).at("ok").get<bool>());EXPECT_EQ(w.Current().document,w.Device(first).document);
    EXPECT_EQ(w.Device(first).result.at("points"),firstResult.at("points"));
    ASSERT_TRUE(Execute(w,{{"operation","run_all"}}).at("ok").get<bool>())<<w.response.dump();EXPECT_EQ(w.response.at("runs").size(),2u);
    ASSERT_TRUE(w.jobs->PauseAllJobs());ASSERT_TRUE(w.Dispatch({{"operation","run_all"}}).at("ok").get<bool>());
    ASSERT_TRUE(w.Dispatch({{"operation","cancel"}}).at("ok").get<bool>());ASSERT_TRUE(w.jobs->ResumeAllJobs());
    while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_TRUE(w.runQueue.empty());EXPECT_EQ(w.response.at("error").at("code"),"cancelled");EXPECT_EQ(w.response.at("runs").size(),1u);
}
TEST(ScapsLab,EveryExampleSolvesAndRoundtripsBitwise) {
    std::size_t projects=0,devices=0;
    for(const auto &entry:std::filesystem::directory_iterator(SCAPSLAB_EXAMPLES)) if(entry.path().extension()==".scapslab") {
        ++projects;Workflow w(CreateRef<JobSystem>(nullptr,1));
        ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",entry.path().string()}}).at("ok").get<bool>())<<entry.path()<<w.response.dump();
        if(std::getenv("SCAPSLAB_WRITE_STORED_EXAMPLE")) {
            const auto deviceFile=Temp("scapslab-example-normalize.yaml");
            for(auto &d:w.project.devices) {WriteProject(d->document,deviceFile.string());d->document=ReadProject(deviceFile.string());}
            std::filesystem::remove(deviceFile);
            if(entry.path().filename()!="07-pn-compare-with-results.scapslab") ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",entry.path().string()}}).at("ok").get<bool>());
        }
        std::vector<std::string> originals;for(const auto &d:w.project.devices){originals.push_back(d->document.dump());++devices;}
        ASSERT_TRUE(Execute(w,{{"operation","run_all"}}).at("ok").get<bool>())<<entry.path()<<w.response.dump();
        if(entry.path().filename()=="05-capacitance-ready.scapslab") for(const auto *kind:{"cv","cf"}) {
            ASSERT_TRUE(Execute(w,{{"operation",kind},{"temperature_K",300},{"start_V",-0.1},{"stop_V",0.1},{"step_V",0.1},{"frequency_Hz",1e4},{"voltage_V",0},{"start_Hz",10},{"stop_Hz",1e4},{"points",3}}).at("ok").get<bool>())<<w.response.dump();
            for(const auto &point:w.Current().acResults.at(kind).at("points")) ASSERT_EQ(point.at("status"),"Converged");
        }
        const auto acBefore=w.Current().acResults;
        std::vector<Json> solved;for(const auto &d:w.project.devices) {solved.push_back(d->result.at("points"));for(const auto &p:d->result.at("points")) ASSERT_EQ(p.at("status"),"Converged")<<entry.path()<<p.at("voltage_V");}
        if(std::getenv("SCAPSLAB_WRITE_STORED_EXAMPLE") && entry.path().filename()=="07-pn-compare-with-results.scapslab") {
            ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",entry.path().string()}}).at("ok").get<bool>());
        }
        const auto target=Temp("scapslab-example-roundtrip.scapslab");
        ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",target.string()}}).at("ok").get<bool>());
        ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",target.string()}}).at("ok").get<bool>())<<w.status<<" "<<entry.path();
        EXPECT_EQ(w.Current().acResults,acBefore);
        for(std::size_t i=0;i<w.project.devices.size();++i) {EXPECT_EQ(w.project.devices[i]->document.dump(),originals[i]);EXPECT_EQ(w.project.devices[i]->result.at("points").dump(),solved[i].dump());}
        ASSERT_TRUE(Execute(w,{{"operation","run_all"}}).at("ok").get<bool>());
        for(std::size_t i=0;i<w.project.devices.size();++i) EXPECT_EQ(w.project.devices[i]->result.at("points").dump(),solved[i].dump())<<entry.path();
        std::filesystem::remove(target);
    }
    EXPECT_EQ(projects,7u);EXPECT_GE(devices,10u);
}
TEST(ScapsLab,LegacyMigrationAndInvalidWorkspaceAreTransactional) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",SCAPSLAB_PROJECT}}).at("ok").get<bool>());
    ASSERT_EQ(w.project.devices.size(),1u);EXPECT_EQ(w.Current().document,ReadProject(SCAPSLAB_PROJECT));EXPECT_EQ(std::filesystem::path(w.path).extension(),".scapslab");
    auto workspace=w.Workspace();const auto original=workspace;
    workspace["devices"].push_back(workspace["devices"][0]);EXPECT_THROW(w.SetWorkspace(workspace),std::invalid_argument);EXPECT_EQ(w.Workspace(),original);
    workspace=original;workspace["compare_set"]={"missing"};EXPECT_THROW(w.SetWorkspace(workspace),std::invalid_argument);EXPECT_EQ(w.Workspace(),original);
    workspace=original;workspace["devices"][0]["colour"]={1,0,-1,1};EXPECT_THROW(w.SetWorkspace(workspace),std::invalid_argument);EXPECT_EQ(w.Workspace(),original);
    EXPECT_FALSE(Execute(w,{{"operation","device_remove"}}).at("ok").get<bool>());EXPECT_EQ(w.Workspace(),original);
    workspace=original;workspace["devices"][0]["result"]={{"provenance",{{"device_hash",DocumentHash(w.Current().document)},{"solver_version","test"},{"time_unix_ms",0}}},{"points",Json::array()}};
    EXPECT_THROW(w.SetWorkspace(workspace),std::invalid_argument);EXPECT_EQ(w.Workspace(),original);
    const Json heterogeneous={{"points",Json::array({{{"a",1}},{{"b",2}}})},{"ui",{{"points",Json::array({1,2})}}}};
    EXPECT_EQ(UnpackResults(PackResults(heterogeneous)),heterogeneous);
    auto columns=PackResults(Json{{"points",Json::array({{{"a",1.0},{"b",2}},{{"a",2.0},{"b",3}}})}});
    EXPECT_EQ(UnpackResults(columns).at("points")[0].at("a").dump(),"1.0");columns["points"]["count"]=3;EXPECT_THROW((void)UnpackResults(columns),std::invalid_argument);
}
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#undef GetJob
#undef GetJob
TEST(ScapsLab,AtomicSaveChild) {
    const char *target=std::getenv("SCAPSLAB_ATOMIC_TARGET");if(!target) return;
    const std::string text(128*1024*1024,'x');std::string error;ASSERT_TRUE(TextFileIO::Save(Path{target},text,error))<<error;
}
TEST(ScapsLab,KilledDuringSavePreservesPreviousProject) {
    const auto dir=Temp("scapslab-atomic-kill");std::filesystem::create_directories(dir);const auto target=dir/"project.scapslab";
    const std::string previous="previous complete project";std::string error;ASSERT_TRUE(TextFileIO::Save(Path{target},previous,error));
    wchar_t executable[32768];ASSERT_GT(GetModuleFileNameW(nullptr,executable,32768),0u);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --gtest_filter=ScapsLab.AtomicSaveChild";
    _putenv_s("SCAPSLAB_ATOMIC_TARGET",target.string().c_str());STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    const bool started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);_putenv_s("SCAPSLAB_ATOMIC_TARGET","");ASSERT_TRUE(started);
    bool sawTemporary=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<deadline && WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT) {
        for(const auto &entry:std::filesystem::directory_iterator(dir)) if(entry.path().filename().string().starts_with("project.scapslab.tmp-")) {sawTemporary=true;TerminateProcess(process.hProcess,99);break;}
        if(sawTemporary) break;
    }
    if(!sawTemporary) TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    ASSERT_TRUE(sawTemporary)<<"Child must be interrupted during TextFileIO temporary write";
    std::string contents;ASSERT_TRUE(TextFileIO::Load(Path{target},contents,error));EXPECT_EQ(contents,previous);
    for(const auto &entry:std::filesystem::directory_iterator(dir)) std::filesystem::remove(entry.path());std::filesystem::remove(dir);
}
#endif

TEST(ScapsLab,BatchPlanStableIdsAndModes) {
    auto doc=ReadProject(SCAPSLAB_PROJECT);auto spec=BatchDefaults();spec["axes"][0]={{"parameter_id","temperature_K"},{"values",Json::array({270.,300.,330.})}};
    auto second=doc["layers"][0];second["scapslab_id"]="stable-second";doc["layers"].push_back(second);
    doc["interface_defects"]=Json::array({NewDefect(true)});
    auto parameters=BatchParameters(doc);
    for(const auto &parameter:parameters) {if(parameter.at("path")=="interface_defects[0].Nt_m2")EXPECT_EQ(parameter.at("unit"),"m^-2");if(parameter.at("path")=="illumination.uniform_generation_m3_s")EXPECT_EQ(parameter.at("unit"),"m^-3/s");}
    auto p=std::find_if(parameters.begin(),parameters.end(),[](const Json &p){return p.at("path")=="layers[1].doping.Nd_m3";});ASSERT_NE(p,parameters.end());
    spec["axes"].push_back({{"parameter_id",p->at("id")},{"start",1e20},{"stop",1e22},{"count",3},{"spacing","log"}});
    auto plan=BatchPreview(doc,spec);EXPECT_EQ(plan.at("member_count"),9);EXPECT_NEAR(plan.at("combinations")[1].at("layers[1].doping.Nd_m3").get<double>()/1e21,1.,1e-13);
    std::swap(doc["layers"][0],doc["layers"][1]);plan=BatchPreview(doc,spec);EXPECT_EQ(plan.at("spec").at("axes")[1].at("path"),"layers[0].doping.Nd_m3");
    spec["mode"]="together";EXPECT_EQ(BatchPreview(doc,spec).at("member_count"),3);
    spec["axes"][1]["count"]=2;EXPECT_THROW(BatchPreview(doc,spec),std::invalid_argument);
    spec["axes"][0]["parameter_id"]="typo";EXPECT_THROW(BatchPreview(doc,spec),std::invalid_argument);
}
TEST(ScapsLab,BatchFailedRowsRecorderRetryPersistenceAndPlotLab) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);auto spec=BatchDefaults();spec["axes"][0]={{"parameter_id","temperature_K"},{"values",Json::array({300.,-1.,330.})}};spec["voltages_V"]=Json::array({0.,0.05});
    auto result=Execute(w,{{"operation","run_batch"},{"spec",spec}});ASSERT_TRUE(result.at("ok").get<bool>())<<result.dump();auto table=w.Current().batchResult;ASSERT_EQ(table.at("rows").size(),3);EXPECT_EQ(table.at("rows")[1].at("status"),"Failed");EXPECT_EQ(table.at("rows")[1].at("error_type"),"invalid_parameter");
    EXPECT_TRUE(Execute(w,{{"operation","retry_batch"},{"member_id","point_1"}}).at("ok").get<bool>());EXPECT_EQ(w.Current().batchResult.at("rows")[1].at("attempt"),2);EXPECT_EQ(w.Current().batchResult.at("rows")[0],table.at("rows")[0]);
    const Json columns=Json::array({{{"id","Voc"},{"quantity","Voc"}}});EXPECT_TRUE(Execute(w,{{"operation","record_batch"},{"columns",columns}}).at("ok").get<bool>());EXPECT_EQ(w.Current().batchResult.at("rows")[0].at("statuses").at("Voc"),"not_illuminated");EXPECT_TRUE(w.Current().batchResult.at("rows")[1].at("values").at("Voc").is_null());
    auto invalidDraft=spec;invalidDraft["axes"][0]["parameter_id"]="deleted-parameter";
    EXPECT_TRUE(Execute(w,{{"operation","batch_settings"},{"spec",invalidDraft}}).value("ok",false));
    ASSERT_TRUE(Execute(w,{{"operation","retry_batch"},{"member_id","point_0"}}).value("ok",false));
    EXPECT_EQ(w.Current().batchResult.at("rows")[0].at("statuses").at("Voc"),"not_illuminated");
    const auto csv=Temp("sl4-batch.csv"),book=Temp("sl4-batch.plotlab"),project=Temp("sl4-batch.scapslab");
    EXPECT_TRUE(Execute(w,{{"operation","export_batch"},{"path",csv.string()}}).at("ok").get<bool>());
    EXPECT_TRUE(Execute(w,{{"operation","send_to_plotlab"},{"path",book.string()}}).at("ok").get<bool>());
    auto workbook=sst::plotlab::Project::load(book);int books=0,sheets=0;for(const auto &[id,o]:workbook.objects()){books+=std::holds_alternative<sst::plotlab::Workbook>(o->data);if(const auto *sheet=std::get_if<sst::plotlab::Worksheet>(&o->data)){++sheets;EXPECT_EQ(sheet->rows.size(),3);EXPECT_TRUE(sheet->source.contains("provenance"));}}EXPECT_EQ(books,1);EXPECT_EQ(sheets,1);
    auto invalid=w.Workspace();invalid["devices"][0]["batch_result"]["rows"][0]["values"]=Json::object();
    EXPECT_THROW(w.SetWorkspace(invalid),Json::out_of_range);EXPECT_EQ(w.Current().batchResult.at("rows").size(),3);
    EXPECT_TRUE(Execute(w,{{"operation","project_save"},{"path",project.string()}}).at("ok").get<bool>());auto stored=w.Current().batchResult;EXPECT_TRUE(Execute(w,{{"operation","project_load"},{"path",project.string()}}).at("ok").get<bool>());EXPECT_EQ(w.Current().batchResult,stored);
    EXPECT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",310.}}}}).at("ok").get<bool>());EXPECT_FALSE(w.Current().batchResult.empty());EXPECT_TRUE(Execute(w,{{"operation","export_batch"},{"path",csv.string()}}).at("ok").get<bool>());
    std::filesystem::remove(csv);std::filesystem::remove(book);std::filesystem::remove(project);
}
TEST(ScapsLab,BatchStopRetainsCompletedAndCancelledMembers) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);auto spec=BatchDefaults();spec["axes"][0]["count"]=100;spec["voltages_V"]=Json::array({0.,0.05});ASSERT_TRUE(w.Dispatch({{"operation","run_batch"},{"spec",spec}}).at("ok").get<bool>());
    bool progressed=false;for(int i=0;i<20000 && w.active;++i) {const auto job=w.jobs->GetJob(w.active);if(job && job->completedWork>=1){progressed=true;break;}w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_TRUE(progressed);ASSERT_TRUE(w.Dispatch({{"operation","cancel"}}).at("ok").get<bool>());while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_EQ(w.response.at("error").at("code"),"cancelled");ASSERT_EQ(w.Current().batchResult.at("rows").size(),100);EXPECT_EQ(w.Current().batchResult.at("rows")[0].at("status"),"Converged");EXPECT_EQ(w.Current().batchResult.at("rows").back().at("status"),"Cancelled");
}

TEST(ScapsLab,IlluminationQeGradingAndOpticalRoundtrip) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string());
    auto run=Execute(w,{{"operation","run"}});ASSERT_TRUE(run.value("ok",false))<<run.dump();ASSERT_TRUE(w.Current().result.contains("optics"));
    EXPECT_GT(w.Current().result["optics"]["generation_m3_s"][30].get<double>(),0.);
    const auto &alpha=w.Current().result["optics"]["absorption"][0];
    EXPECT_GT(alpha["alpha_m1"][150].get<double>(),alpha["without_tail_m1"][150].get<double>());
    EXPECT_GT(AM15Spectrum().size(),100u);
    EXPECT_EQ(w.Current().result["photovoltaic"]["status"],"converged")<<w.Current().result["photovoltaic"];
    const auto &pv=w.Current().result.at("photovoltaic");
    EXPECT_GT(pv.at("Vmpp_V").get<double>(),0.);EXPECT_LT(pv.at("Vmpp_V").get<double>(),pv.at("Voc_V").get<double>());
    EXPECT_LT(pv.at("Jmpp_A_m2").get<double>(),0.);
    EXPECT_NEAR(-pv.at("Vmpp_V").get<double>()*pv.at("Jmpp_A_m2").get<double>(),pv.at("Pmax_W_m2").get<double>(),1e-10);
    const auto q=Execute(w,{{"operation","qe"},{"start_nm",590.},{"stop_nm",610.},{"points",3}});ASSERT_TRUE(q.value("ok",false))<<q.dump();
    const auto qe=w.Current().acResults["qe"];EXPECT_TRUE(qe["valid"].get<bool>());ASSERT_TRUE(qe["integrated_Jsc_A_m2"].is_number());
    EXPECT_NEAR(qe["integrated_Jsc_A_m2"].get<double>(),qe["light_Jsc_A_m2"].get<double>(),std::abs(qe["light_Jsc_A_m2"].get<double>())*.02);
    const auto file=Temp("scapslab-light.scapslab");ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",file.string()}}).value("ok",false));
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false));EXPECT_EQ(w.Current().acResults["qe"],qe);
    EXPECT_FALSE(Execute(w,{{"operation","qe"},{"points",1}}).value("ok",true));
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",301.}}}}).value("ok",false));EXPECT_FALSE(w.Current().acResults.empty());std::filesystem::remove(file);
}

TEST(ScapsLab,Stage05DefImportSolvesLikeIndependentYaml) {
    const auto fixtures=ExamplesDirectory().parent_path()/"tests";
    Workflow imported(CreateRef<JobSystem>(nullptr,1)), reference(CreateRef<JobSystem>(nullptr,1));
    auto result=Execute(imported,{{"operation","import_def"},{"path",(fixtures/"stage05-gaussian.def").string()}});
    ASSERT_TRUE(result.value("ok",false))<<result.dump();
    EXPECT_TRUE(result.at("operation_result").contains("warnings"));
    reference.Current().document=ReadProject((fixtures/"stage05-gaussian.yaml").string());
    // Match numerical settings, including the def1 audit's 63-level Gauss quadrature.
    auto doc=imported.Current().document;
    for(const char *key:{"mesh","solver","voltage","temperature_K","illumination"}) doc[key]=reference.Current().document.at(key);
    for(auto &layer:doc["layers"]) for(auto &defect:layer["defects"]) {defect["energy_levels"]=63;defect["energy_quadrature"]="gauss_legendre";}
    ASSERT_TRUE(Execute(imported,{{"operation","edit"},{"document",doc}}).value("ok",false));
    ASSERT_TRUE(Execute(imported,{{"operation","run"}}).value("ok",false));
    ASSERT_TRUE(Execute(reference,{{"operation","run"}}).value("ok",false));
    const auto &actual=imported.Current().result.at("points"), &expected=reference.Current().result.at("points");
    ASSERT_EQ(actual.size(),expected.size());
    for(std::size_t i=0;i<actual.size();++i) {
        ASSERT_EQ(actual[i].at("status"),"Converged");
        EXPECT_EQ(actual[i].at("voltage_V"),expected[i].at("voltage_V"));
        EXPECT_EQ(actual[i].at("J_A_m2"),expected[i].at("J_A_m2"));
    }
    const auto file=Temp("scapslab-stage05-export.def");
    ASSERT_TRUE(Execute(imported,{{"operation","export_def"},{"path",file.string()}}).value("ok",false));
    EXPECT_GT(std::filesystem::file_size(file),0u);
    ASSERT_TRUE(Execute(imported,{{"operation","import_def"},{"path",file.string()}}).value("ok",false));
    EXPECT_EQ(imported.Current().document.at("layers").size(),3u);
    std::filesystem::remove(file);
}

TEST(ScapsLab,BatchDraftCanPassThroughUnequalTogetherCounts) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);auto spec=BatchDefaults();spec["mode"]="together";
    spec["axes"].push_back({{"parameter_id","external.area_m2"},{"values",Json::array({1e-4})}});
    auto draft=Execute(w,{{"operation","batch_settings"},{"spec",spec}});
    ASSERT_TRUE(draft.value("ok",false))<<draft.dump();
    EXPECT_TRUE(draft.at("operation_result").contains("configuration_error"));
    EXPECT_FALSE(Execute(w,{{"operation","run_batch"}}).value("ok",true));
    spec["axes"][1]["values"]=Json::array({1e-4,2e-4,3e-4});
    EXPECT_TRUE(Execute(w,{{"operation","batch_settings"},{"spec",spec}}).value("ok",false));
    EXPECT_EQ(BatchPreview(w.Current().document,w.Current().batchSpec).at("member_count"),3);
    spec["axes"][0]["count"]=2.5;EXPECT_THROW(BatchPreview(w.Current().document,spec),std::invalid_argument);
    const auto before=w.Current().batchSpec;
    EXPECT_FALSE(Execute(w,{{"operation","batch_settings"},{"spec",{{"axes",4}}}}).value("ok",true));
    EXPECT_EQ(w.Current().batchSpec,before);
    spec["axes"][0]["values"]="bad";
    EXPECT_FALSE(Execute(w,{{"operation","batch_settings"},{"spec",spec}}).value("ok",true));
    EXPECT_EQ(w.Current().batchSpec,before);
}

TEST(ScapsLab,RecorderAcquiresRequestedSamplesWithinDeclaredRange) {
    const auto doc=ReadProject(SCAPSLAB_PROJECT);auto spec=BatchDefaults();
    spec["voltages_V"]=Json::array({.05,0.});
    spec["columns"]=Json::array({{{"id","Cmid"},{"quantity","C"},{"voltage_V",.025},{"frequency_Hz",1e4}},{{"id","outside"},{"quantity","J"},{"voltage_V",1.}}});
    const auto plan=BatchPreview(doc,spec);
    EXPECT_EQ(plan.at("voltages_V"),Json::array({.05,.025,0.}));
    EXPECT_EQ(plan.at("frequencies_Hz"),Json::array({1e4}));
    EXPECT_EQ(plan.at("spec").at("voltages_V"),plan.at("voltages_V"));
    EXPECT_EQ(plan.at("sample_count"),18);
}

TEST(ScapsLab,GenerationRecombinationChannelsLayersAndExport) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string());
    auto &doc=w.Current().document;
    for(int i=0;i<2;++i) {auto d=NewDefect();d["scapslab_id"]="gr-defect-"+std::to_string(i);d["Nt_m3"]=1e17;doc["layers"][0]["defects"].push_back(d);}
    auto sheet=NewDefect(true);sheet["id"]="gr-interface";sheet["Nt_m2"]=1e10;doc["interface_defects"].push_back(sheet);
    for(auto &layer:doc["layers"]) layer["recombination"]={{"radiative_cm3_s",1e-14},{"auger_n_cm6_s",2.8e-31},{"auger_p_cm6_s",1e-31}};
    const auto result=Execute(w,{{"operation","band"},{"voltage_V",0.05}});ASSERT_TRUE(result.at("ok").get<bool>())<<result.dump();
    const auto &snapshot=w.Current().bandResult;const auto &point=snapshot.at("points")[0];const auto &gr=point.at("generation_recombination");
    ASSERT_TRUE(point.at("valid").get<bool>())<<point.dump();
    const auto p=Model(doc);const auto device=scaps_core::make_device_from_project(p);const auto pc=scaps_core::physical_constants(p.constants_mode);
    const auto mesh=scaps_core::prepare_mesh(device,p.mesh,{-0.05},{p.temperature_K},p.solver,pc);
    const auto reference=scaps_core::solve_iv(device,mesh,{-0.05},p.temperature_K,p.solver,pc).front();
    ASSERT_EQ(gr.at("channels").size(),reference.state.recombination.size());
    double sum=0;bool defect0=false,defect1=false,hasInterface=false;
    for(std::size_t i=0;i<reference.state.recombination.size();++i) {
        const auto &c=gr.at("channels")[i];const auto &expected=reference.state.recombination[i];
        EXPECT_EQ(c.at("R_m3_s").get<std::vector<double>>(),expected.R_m3_s);
        EXPECT_EQ(c.at("R_sheet_m2_s").get<std::vector<double>>(),expected.R_sheet_m2_s);
        EXPECT_DOUBLE_EQ(c.at("integrated_current_A_m2").get<double>(),expected.integrated_current_A_m2);
        EXPECT_LE(c.at("layer_sum_relative_error").get<double>(),1e-12);sum+=expected.integrated_current_A_m2;
        defect0|=c.at("id")=="gr-defect-0";defect1|=c.at("id")=="gr-defect-1";hasInterface|=c.at("mechanism")=="interface";
    }
    EXPECT_TRUE(defect0);EXPECT_TRUE(defect1);EXPECT_TRUE(hasInterface);
    EXPECT_DOUBLE_EQ(gr.at("recombination_current_A_m2").get<double>(),sum);
    double generated=0,layerRecombined=0;
    for(const auto &layer:gr.at("layers")) {generated+=layer.at("generation_current_A_m2").get<double>();layerRecombined+=layer.at("recombination_current_A_m2").get<double>();}
    EXPECT_NEAR(generated,gr.at("generation_current_A_m2").get<double>(),1e-12);
    EXPECT_NEAR(layerRecombined,sum,1e-12);
    const double net=generated-sum,terminal=point.at("J_A_m2");
    ASSERT_EQ(reference.state.contact_recombination.size(),2u);
    double contactLoss=0;
    for(std::size_t i=0;i<2;++i) {
        const auto &c=reference.state.contact_recombination[i];const auto &actual=gr.at("contact_recombination")[i];
        EXPECT_EQ(actual.at("id"),c.id);EXPECT_DOUBLE_EQ(actual.at("electron_current_A_m2").get<double>(),c.electron_current_A_m2);EXPECT_DOUBLE_EQ(actual.at("hole_current_A_m2").get<double>(),c.hole_current_A_m2);
        contactLoss+=c.id=="right"?c.electron_current_A_m2:c.hole_current_A_m2;
    }
    const double budgetError=std::abs(net-contactLoss-terminal)/std::max({1e-30,std::abs(net),std::abs(contactLoss),std::abs(terminal)});
    EXPECT_TRUE(gr.at("terminal_budget_valid").get<bool>());
    EXPECT_NEAR(gr.at("terminal_budget_relative_error").get<double>(),budgetError,1e-14);
    EXPECT_EQ(gr.at("terminal_budget_valid").get<bool>(),budgetError<=1e-6);
    RecordProperty("terminal_budget_relative_error",gr.at("terminal_budget_relative_error").dump());
    RecordProperty("generation_current_A_m2",gr.at("generation_current_A_m2").dump());
    RecordProperty("recombination_current_A_m2",gr.at("recombination_current_A_m2").dump());
    RecordProperty("terminal_current_A_m2",point.at("J_A_m2").dump());
    for(const auto *format:{"json","csv"}) {
        const auto path=Temp((std::string("scapslab-gr.")+format).c_str());
        auto exported=Execute(w,{{"operation","export_gr"},{"source","band"},{"path",path.string()},{"format",format}});ASSERT_TRUE(exported.at("ok").get<bool>())<<exported.dump();
        std::string text,error;ASSERT_TRUE(TextFileIO::Load(Path{path},text,error));
        if(std::string(format)=="json") {const auto saved=Json::parse(text);EXPECT_EQ(saved.at("point"),point);EXPECT_EQ(saved.at("provenance"),snapshot.at("provenance"));EXPECT_EQ(saved.at("project_snapshot"),doc);}
        else {EXPECT_NE(text.find("gr-defect-0"),std::string::npos);EXPECT_NE(text.find("illumination_json"),std::string::npos);EXPECT_NE(text.find("layer-1"),std::string::npos);}
        std::filesystem::remove(path);
    }
    EXPECT_FALSE(Execute(w,{{"operation","export_gr"},{"source","band"},{"point_index",1.5},{"path",Temp("fractional-gr.json").string()}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","export_gr"},{"source","band"},{"point_index",4294967296LL},{"path",Temp("overflow-gr.json").string()}}).at("ok").get<bool>());
    EXPECT_FALSE(Execute(w,{{"operation","export_gr"},{"source","band"},{"point_index",-1},{"path",Temp("bad-gr.json").string()}}).at("ok").get<bool>());
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",301.}}}}).at("ok").get<bool>());
    EXPECT_TRUE(Execute(w,{{"operation","export_gr"},{"source","band"},{"path",Temp("stale-gr.json").string()}}).at("ok").get<bool>());
}

TEST(ScapsLab,ContactExternalBalanceAndQeInternalVoltage) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));
    for(bool illuminated:{false,true}) {
        w.Current().document=ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string());
        auto &doc=w.Current().document;doc["illumination"]["enabled"]=illuminated;
        doc["external"]={{"area_m2",1e-4},{"series_ohm_m2",1e-5},{"shunt_ohm_m2",1.0}};
        ASSERT_TRUE(Execute(w,{{"operation","band"},{"voltage_V",.05}}).value("ok",false))<<w.response;
        const auto point=w.Current().bandResult.at("points")[0];const auto &gr=point.at("generation_recombination");
        ASSERT_TRUE(point.at("valid").get<bool>())<<point;
        EXPECT_TRUE(gr.at("terminal_budget_valid").get<bool>())<<gr;
        EXPECT_LE(gr.at("terminal_budget_relative_error").get<double>(),1e-6);
        EXPECT_NE(gr.at("shunt_current_A_m2").get<double>(),0.);
        EXPECT_EQ(gr.at("series_current_A_m2"),point.at("J_A_m2"));
    }
    const auto p=Model(w.Current().document);auto device=scaps_core::make_device_from_project(p);device.illumination.enabled=false;
    const auto pc=scaps_core::physical_constants(p.constants_mode);
    const auto mesh=scaps_core::prepare_mesh(device,p.mesh,{0.},{p.temperature_K},p.solver,pc);
    for(const auto *mode:{"constant_flux","constant_power"}) {
        ASSERT_TRUE(Execute(w,{{"operation","qe"},{"start_nm",590.},{"stop_nm",610.},{"points",3},{"probe_mode",mode}}).value("ok",false));
        const auto qe=w.Current().acResults.at("qe");
        const auto ref=scaps_core::solve_qe(device,mesh,{590.,600.,610.},mode,std::string(mode)=="constant_flux"?1e16:1e-3,0.,p.temperature_K,p.solver,pc);
        for(std::size_t i=0;i<ref.size();++i) {ASSERT_TRUE(qe.at("points")[i].at("internal_voltage_V").is_number());EXPECT_DOUBLE_EQ(qe.at("points")[i].at("internal_voltage_V").get<double>(),-ref[i].internal_voltage_V);EXPECT_EQ(qe.at("points")[i].at("terminal_voltage_V"),0.);}
        EXPECT_NE(qe.at("points")[1].at("internal_voltage_V").get<double>(),0.);
        const auto path=Temp("slgr3.scapslab");
        ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",path.string()},{"include_results",true}}).value("ok",false));
        ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",path.string()}}).value("ok",false));EXPECT_EQ(w.Current().acResults.at("qe"),qe);
        for(auto &point:w.Current().acResults["qe"]["points"]) {point.erase("internal_voltage_V");point.erase("terminal_voltage_V");}
        ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",path.string()},{"include_results",true}}).value("ok",false));
        ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",path.string()}}).value("ok",false));EXPECT_FALSE(w.Current().acResults.at("qe").at("points")[0].contains("internal_voltage_V"));
        std::filesystem::remove(path);
    }
}

TEST(ScapsLab,SlioProjectRoundtripMigrationRecoveryAndOptionalResults) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    w.Current().document["mesh"]["adaptivity_level"]="0";
    w.Current().document["voltage"]={{"start_V",0.},{"stop_V",.1},{"step_V",.05}};
    ASSERT_TRUE(Execute(w,{{"operation","run"}}).value("ok",false));
    const auto document=w.Current().document,points=w.Current().result.at("points");
    ASSERT_TRUE(Execute(w,{{"operation","action_settings"},{"actions",{{"iv",{{"operation","run"}}},{"qe",{{"operation","qe"},{"start_nm",400.},{"stop_nm",800.},{"points",5}}}}}}).value("ok",false));
    const auto actions=w.Current().actions;
    const auto file=Temp("slio-roundtrip.scapslab"),recovery=Temp("slio-recovery.scapslab");
    auto saved=Execute(w,{{"operation","project_save"},{"path",file.string()}});ASSERT_TRUE(saved.value("ok",false))<<saved.dump();
    EXPECT_EQ(saved.at("operation_result").at("size_bytes"),std::filesystem::file_size(file));
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false));
    EXPECT_EQ(w.Current().document.dump(),document.dump());EXPECT_EQ(w.Current().actions,actions);
    ASSERT_TRUE(Execute(w,{{"operation","run"}}).value("ok",false));EXPECT_EQ(w.Current().result.at("points").dump(),points.dump());
    w.recoveryPath=recovery.string();w.autosavedAt=std::chrono::steady_clock::now()-std::chrono::seconds(31);
    w.OnUpdate(0);while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    EXPECT_TRUE(std::filesystem::exists(recovery));EXPECT_TRUE(w.dirty);EXPECT_EQ(w.path,file.string());
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",recovery.string()}}).value("ok",false));EXPECT_TRUE(w.dirty);EXPECT_TRUE(w.path.empty());EXPECT_EQ(w.Current().document,document);
    ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",file.string()},{"include_results",false}}).value("ok",false));
    EXPECT_FALSE(w.Current().result.empty());EXPECT_FALSE(std::filesystem::exists(recovery));
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false));EXPECT_TRUE(w.Current().result.empty());EXPECT_EQ(w.Current().document,document);EXPECT_EQ(w.Current().actions,actions);
    for(int version:{0,1}) {auto old=w.Workspace();old["schema_version"]=version;old.erase("ui");for(auto &d:old["devices"])d.erase("actions");std::string error;ASSERT_TRUE(TextFileIO::Save(Path{file},old.dump(),error));ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false));EXPECT_EQ(w.project.schema_version,3);EXPECT_EQ(w.Current().document,document);}
    ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",recovery.string()}}).value("ok",false));EXPECT_TRUE(std::filesystem::exists(recovery));
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",recovery.string()}}).value("ok",false));EXPECT_EQ(w.Current().document,document);std::filesystem::remove(recovery);
    auto old=document;old["schema_version"]=0;std::string error;ASSERT_TRUE(TextFileIO::Save(Path{file},old.dump(),error));ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false));EXPECT_EQ(w.Current().document,document);
    std::filesystem::remove(file);
}
TEST(ScapsLab,SlioInlineResourcesSurviveRemoval) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject((ExamplesDirectory()/"08-pn-illumination.yaml").string());
    const auto spectrum=Temp("slio-spectrum.spe");
    // The SCAPS parser accepts wavelength/power pairs, with an optional header.
    {std::ofstream f(spectrum);f<<"400 10\n600 20\n800 30\n";}
    auto draft=w.Current().document;draft["illumination"]["spectrum_file"]=spectrum.string();draft["illumination"]["spectrum_nm_W_m2"]=Json::array();
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"document",draft}}).value("ok",false))<<w.response.dump();
    EXPECT_TRUE(w.Current().document.at("illumination").at("spectrum_file").get<std::string>().empty());
    std::filesystem::remove(spectrum);EXPECT_NO_THROW((void)Model(w.Current().document));
    const auto file=Temp("slio-inline.scapslab");ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",file.string()}}).value("ok",false));const auto document=w.Current().document;
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false));EXPECT_EQ(w.Current().document,document);std::filesystem::remove(file);
}
TEST(ScapsLab,SlioExportsDoNotMutateAndFailuresRetainState) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    ASSERT_TRUE(Execute(w,{{"operation","run"}}).value("ok",false));const auto before=w.Workspace();
    auto invalid=before;invalid["devices"][0]["batch_result"]={{"provenance",{{"device_hash",DocumentHash(w.Current().document)}}}};
    EXPECT_THROW(w.SetWorkspace(invalid),std::invalid_argument);EXPECT_EQ(w.Workspace(),before);
    invalid=before;invalid["devices"][0]["actions"]={{"cv",{{"temperature_K","invalid"}}}};
    EXPECT_THROW(w.SetWorkspace(invalid),std::invalid_argument);EXPECT_EQ(w.Workspace(),before);
    const auto runs=ResultRuns(before);ASSERT_EQ(runs.size(),1);const auto file=Temp("slio-results.json");
    ASSERT_TRUE(Execute(w,{{"operation","export_results"},{"path",file.string()},{"format","json"},{"run_ids",{runs[0].at("run_id")}}}).value("ok",false))<<w.response.dump();
    std::string text,error;ASSERT_TRUE(TextFileIO::Load(Path{file},text,error));auto exported=UnpackResults(Json::parse(text)).at("runs");for(auto &r:exported){r.erase("voltage_columns");r.erase("effective_conditions");}EXPECT_EQ(exported,runs);EXPECT_EQ(w.Workspace(),before);
    EXPECT_FALSE(Execute(w,{{"operation","export_results"},{"path",file.string()},{"all",true},{"format","bad"}}).value("ok",false));
    EXPECT_FALSE(Execute(w,{{"operation","export_results"},{"path",file.string()},{"run_ids",{"unknown"}}}).value("ok",false));
    EXPECT_FALSE(Execute(w,{{"operation","export_results"},{"path",std::filesystem::temp_directory_path().string()},{"all",true}}).value("ok",false));EXPECT_EQ(w.Workspace(),before);
    std::filesystem::remove(file);
}
#ifdef _WIN32
TEST(ScapsLab,DISABLED_SlioInterruptedSaveChild) {
    const char *file=std::getenv("SCAPSLAB_INTERRUPTED_SAVE");if(!file)GTEST_SKIP()<<"Child process helper";
    const std::string data(256*1024*1024,'x');std::string error;
    ASSERT_TRUE(TextFileIO::Save(Path{file},data,error))<<error;
}
TEST(ScapsLab,SlioKilledWriterLeavesPreviousProjectIntact) {
    const auto directory=std::filesystem::temp_directory_path()/("slio-interrupted-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);const auto target=directory/"device.scapslab";
    Workflow original(CreateRef<JobSystem>(nullptr,1));original.Current().document=ReadProject(SCAPSLAB_PROJECT);
    const auto previous=WorkspaceText(PackResults(original.Workspace()));std::string error;
    ASSERT_TRUE(TextFileIO::Save(Path{target},previous,error));
    wchar_t executable[32768];ASSERT_GT(GetModuleFileNameW(nullptr,executable,32768),0u);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --gtest_filter=ScapsLab.DISABLED_SlioInterruptedSaveChild --gtest_also_run_disabled_tests";
    SetEnvironmentVariableW(L"SCAPSLAB_INTERRUPTED_SAVE",target.c_str());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    const bool created=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0;
    SetEnvironmentVariableW(L"SCAPSLAB_INTERRUPTED_SAVE",nullptr);ASSERT_TRUE(created);
    bool midWrite=false;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(std::chrono::steady_clock::now()<deadline && WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT) {
        for(const auto &entry:std::filesystem::directory_iterator(directory)) if(entry.path().filename().string().starts_with("device.scapslab.tmp-")) {
            // Secure CRT keeps this sibling exclusively open during fwrite/flush, before replacement.
            if(entry.file_size()<256*1024*1024) {midWrite=true;TerminateProcess(process.hProcess,99);break;}
        }
        if(midWrite)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(!midWrite)TerminateProcess(process.hProcess,98);WaitForSingleObject(process.hProcess,10000);CloseHandle(process.hThread);CloseHandle(process.hProcess);
    ASSERT_TRUE(midWrite)<<"Writer must be killed with the temporary write open and before replacement";
    std::string actual;ASSERT_TRUE(TextFileIO::Load(Path{target},actual,error));EXPECT_EQ(actual,previous);
    Workflow recovered(CreateRef<JobSystem>(nullptr,1));ASSERT_TRUE(Execute(recovered,{{"operation","project_load"},{"path",target.string()}}).value("ok",false));EXPECT_EQ(recovered.Current().document,original.Current().document);
    for(const auto &entry:std::filesystem::directory_iterator(directory))std::filesystem::remove(entry.path());std::filesystem::remove(directory);
}
#endif

TEST(ScapsLab,SlioMeasures100000PointStorage) {
    Json points=Json::array();
    for(int i=0;i<100000;++i) points.push_back({{"bias_id","bias-"+std::to_string(i)},{"voltage_V",i*.001},{"terminal_voltage_V",i*.001},{"internal_voltage_V",i*.0009},{"J_A_m2",i*.0123456789012345},{"status","Converged"},{"valid",true},{"validity_status","valid"},{"iterations",7},{"residual",1e-12}});
    const Json rows={{"schema_version",2},{"points",points}};const auto packed=PackResults(rows);
    EXPECT_EQ(UnpackResults(packed),rows);
    const Json sizes={{"point_count",100000},{"row_bytes",WorkspaceText(rows).size()},{"column_bytes",WorkspaceText(packed).size()},{"format","JSON with inline compact result columns"},{"scope","scalar terminal result; spatial profile arrays add their full serialized size"}};
    std::string error;ASSERT_TRUE(TextFileIO::Save(Path{Temp("slio-storage-size.json")},sizes.dump(2),error));std::cout<<sizes.dump()<<std::endl;
}

TEST(ScapsLab,ImmutableHistoryRetentionClearUndoAndPersistence) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);w.Current().document["voltage"]={{"start_V",0.},{"stop_V",.1},{"step_V",.05}};
    auto ui=w.project.ui;ui["history_retention"]=20;
    ASSERT_TRUE(Execute(w,{{"operation","project_edit"},{"ui",ui}}).value("ok",false));
    const auto baseline=WorkspaceText(PackResults(w.Workspace())).size();
    for(int i=0;i<20;++i) {
        ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",300.+i}}}}).value("ok",false));
        ASSERT_TRUE(Execute(w,{{"operation","run"}}).value("ok",false))<<w.response.dump();
    }
    ASSERT_EQ(w.Current().history.size(),20);
    const auto first=w.Current().history.front();const auto id=first.at("run_id");
    ASSERT_TRUE(Execute(w,{{"operation","history_update"},{"run_id",id},{"pinned",true},{"name","Reference"},{"visible",false},{"colour",{.1,.2,.3,1.}}}).value("ok",false));
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"patch",{{"temperature_K",330.}}}}).value("ok",false));
    EXPECT_EQ(w.Current().status,"previous");EXPECT_EQ(w.Current().history.front().at("result"),first.at("result"));
    ASSERT_FALSE(w.Current().history.back().at("diff").empty());
    auto before=w.Workspace();auto damaged=before;damaged["devices"][0]["history"][0]["result"].erase("points");EXPECT_THROW(w.SetWorkspace(damaged),std::exception);EXPECT_EQ(w.Workspace(),before);
    const auto file=Temp("slhist1-20.scapslab");
    ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",file.string()}}).value("ok",false))<<w.response.dump();
    std::cout<<"SLHIST1_SIZE baseline_bytes="<<baseline<<" twenty_runs_bytes="<<std::filesystem::file_size(file)<<" growth_bytes="<<std::filesystem::file_size(file)-baseline<<std::endl;
    ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",Temp("slhist1-gallery.scapslab").string()}}).value("ok",false));
    ASSERT_TRUE(Execute(w,{{"operation","history_clear"}}).value("ok",false));ASSERT_EQ(w.Current().history.size(),1);EXPECT_TRUE(w.Current().history[0].at("pinned"));
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).value("ok",false));EXPECT_EQ(w.Workspace(),before);
    ASSERT_TRUE(Execute(w,{{"operation","redo"}}).value("ok",false));EXPECT_EQ(w.Current().history.size(),1);
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",file.string()}}).value("ok",false))<<w.response.dump();
    EXPECT_EQ(w.Current().history.size(),20);EXPECT_EQ(w.Current().history[0].at("name"),"Reference");
    const auto firstDevice=w.project.active_device;
    ASSERT_TRUE(Execute(w,{{"operation","device_duplicate"}}).value("ok",false));
    ASSERT_TRUE(Execute(w,{{"operation","band"},{"voltage_V",0.}}).value("ok",false));
    ASSERT_TRUE(Execute(w,{{"operation","device_select"},{"device_id",firstDevice}}).value("ok",false));
    const auto allBefore=w.Workspace();
    ASSERT_TRUE(Execute(w,{{"operation","history_clear"},{"scope","all"},{"include_pinned",true}}).value("ok",false));for(const auto &device:w.project.devices)EXPECT_TRUE(device->history.empty());
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).value("ok",false));EXPECT_EQ(w.Workspace(),allBefore);EXPECT_EQ(w.Current().history.size(),20);
    auto old=w.Workspace();old["schema_version"]=2;for(auto &d:old["devices"]){d.erase("history");d.erase("selected_run");}w.SetWorkspace(old);EXPECT_TRUE(w.Current().history.empty());
    std::filesystem::remove(file);
}
TEST(ScapsLab,QuickSweepUsesBatchAndHistoryValidation) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    auto uiState=w.project.ui;uiState["plots"]["C-V"]={0.,1.,1e-12,1e-9,1e-13,1e-10};
    ASSERT_TRUE(Execute(w,{{"operation","project_edit"},{"ui",uiState}}).value("ok",false));EXPECT_EQ(w.project.ui,uiState);
    auto noRetention=uiState;noRetention["history_retention"]=0;EXPECT_FALSE(Execute(w,{{"operation","project_edit"},{"ui",noRetention}}).value("ok",true));EXPECT_EQ(w.project.ui,uiState);
    auto invalidUi=uiState;invalidUi["plots"]["C-V"][5]=0.;EXPECT_FALSE(Execute(w,{{"operation","project_edit"},{"ui",invalidUi}}).value("ok",true));EXPECT_EQ(w.project.ui,uiState);
    Json request={{"operation","quick_sweep"},{"parameter_id","temperature_K"},{"delta_native_units",10.},{"points",3},{"spacing","linear"},{"voltages_V",{0.,.05,.1}}};
    const auto plan=QuickSweep(w.Current().document,request);EXPECT_EQ(plan.at("axes")[0].at("start"),290.);
    ASSERT_TRUE(Execute(w,request).value("ok",false))<<w.response.dump();ASSERT_EQ(w.Current().history.size(),3);
    EXPECT_EQ(w.Current().history.front().at("group_id"),w.Current().history.back().at("group_id"));
    EXPECT_NE(w.Current().history.front().at("model_hash"),w.Current().history.back().at("model_hash"));
    EXPECT_EQ(w.Current().history[1].at("model_hash"),DocumentHash(w.Current().document));
    ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",Temp("slhist1-sweep.scapslab").string()}}).value("ok",false));
    const auto before=w.Workspace();request["spacing"]="log";request["delta_native_units"]=400.;EXPECT_FALSE(Execute(w,request).value("ok",true));EXPECT_EQ(w.Workspace(),before);
    const auto id=w.Current().history.front().at("run_id");EXPECT_FALSE(Execute(w,{{"operation","history_update"},{"run_id",id},{"colour",{2.,0.,0.,1.}}}).value("ok",true));EXPECT_EQ(w.Workspace(),before);
    auto ui=w.project.ui;ui["history_retention"]=1;ASSERT_TRUE(Execute(w,{{"operation","project_edit"},{"ui",ui}}).value("ok",false));EXPECT_EQ(w.Current().history.size(),1);
}

TEST(ScapsLab,LegacyWorkspaceIlluminationDefaultsAndLoadDuringCalculation) {
    for(const auto *operation:{"load","project_load"}) {
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
        auto legacy=w.Workspace();legacy["devices"][0]["document"]["illumination"]={{"enabled",false}};
        EXPECT_THROW(legacy["devices"][0]["document"].at("illumination").at("mode"),Json::out_of_range);
        const auto file=Temp("scapslab-legacy-mode.scapslab");{std::ofstream out(file);out<<legacy.dump();}
        w.Current().document["voltage"]={{"start_V",0.},{"stop_V",0.1},{"step_V",0.01}};
        ASSERT_TRUE(w.Dispatch({{"operation","run"}}).at("ok"));ASSERT_NE(w.active,0u);
        ASSERT_TRUE(w.Dispatch({{"operation",operation},{"path",file.string()}}).at("ok"));
        EXPECT_NE(w.status.find("Cancelling"),std::string::npos);
        while(w.Busy()){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        ASSERT_TRUE(w.response.value("ok",false))<<w.response.dump();
        EXPECT_EQ(w.Current().document.at("illumination").at("mode"),"uniform");
        EXPECT_FALSE(w.Current().document.at("illumination").at("enabled").get<bool>());
        const auto kept=w.Current().document;
        EXPECT_FALSE(Execute(w,{{"operation",operation},{"path",file.string()+".missing"}}).value("ok",true));
        EXPECT_EQ(w.Current().document,kept);std::filesystem::remove(file);
    }
}
TEST(ScapsLab,ProfilesCommandJobsUndoPersistenceAndExport) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    const auto layer=w.Current().document.at("layers")[0].at("scapslab_id");
    const auto initial=w.Current().document;
    Json request={{"operation","load_profile"},{"layer_id",layer},{"parameter_id","Na_m3"},{"path",(ExamplesDirectory()/"profiles/doping.grd").string()}};
    ASSERT_TRUE(Execute(w,request).value("ok",false))<<w.response.dump();
    const auto loaded=w.Current().document;
    EXPECT_EQ(loaded["layers"][0]["grading"]["Na_m3"]["coordinate"].size(),3);
    ASSERT_TRUE(Execute(w,{{"operation","undo"}}).value("ok",false));EXPECT_EQ(w.Current().document,initial);
    ASSERT_TRUE(Execute(w,{{"operation","redo"}}).value("ok",false));EXPECT_EQ(w.Current().document,loaded);
    request["operation"]="preview_profile";ASSERT_TRUE(Execute(w,request).value("ok",false));
    const auto sample=w.response.at("operation_result");EXPECT_EQ(sample.at("x_m").size(),201);
    EXPECT_DOUBLE_EQ(sample["value_SI"].front(),1e20);EXPECT_DOUBLE_EQ(sample["value_SI"].back(),3e20);
    const auto output=Temp("scapslab-profile-export.grd");request["operation"]="export_profile";request["path"]=output.string();
    ASSERT_TRUE(Execute(w,request).value("ok",false));const auto exported=ProfileFromFile(output.string());
    EXPECT_EQ(exported.at("coordinate").size(),201);EXPECT_DOUBLE_EQ(exported.at("value").front(),1e20);std::filesystem::remove(output);
    const auto saved=Temp("scapslab-profiles.scapslab");ASSERT_TRUE(Execute(w,{{"operation","project_save"},{"path",saved.string()}}).value("ok",false));
    ASSERT_TRUE(Execute(w,{{"operation","project_load"},{"path",saved.string()}}).value("ok",false));EXPECT_EQ(w.Current().document,loaded);std::filesystem::remove(saved);
    request["operation"]="load_profile";request["path"]="does-not-exist.grd";EXPECT_FALSE(Execute(w,request).value("ok",true));EXPECT_EQ(w.Current().document,loaded);
    request["operation"]="load_profile";request["parameter_id"]="generation_m3_s";request["path"]=(ExamplesDirectory()/"profiles/generation.gen").string();
    ASSERT_TRUE(Execute(w,request).value("ok",false));EXPECT_EQ(w.Current().document.at("illumination").at("generation_m_m3s").size(),3);
    auto doc=w.Current().document;auto defect=NewDefect();defect["scapslab_id"]="profile-defect";doc["layers"][0]["defects"]=Json::array({defect});
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"document",doc}}).value("ok",false));
    request["parameter_id"]="Nt_m3";request["defect_id"]="profile-defect";request["path"]=(ExamplesDirectory()/"profiles/defects.grd").string();
    ASSERT_TRUE(Execute(w,request).value("ok",false))<<w.response.dump();
    EXPECT_TRUE(Model(w.Current().document).layers[0].defects[0].density_profile_m3.has_value());
    request["operation"]="set_profile";request["parameter_id"]="composition";request["profile"]={{"law","linear"},{"left",.2},{"right",.8}};
    ASSERT_TRUE(Execute(w,request).value("ok",false));
    request["parameter_id"]="Eg_eV";request["profile"]={{"law","linear"},{"left",1.1},{"right",1.5},{"composition_dependent",false}};
    ASSERT_TRUE(Execute(w,request).value("ok",false));request["operation"]="preview_profile";
    ASSERT_TRUE(Execute(w,request).value("ok",false));EXPECT_DOUBLE_EQ(w.response.at("operation_result").at("value_SI").back(),1.42);
}
TEST(ScapsLab,ProfilePreviewUsesEveryCoreLawAndPhysicalCoordinates) {
    for(const auto *law:{"uniform","linear","parabolic","parabolic2","logarithmic","exponential","beta","power","effective_medium","gaussian","gauss_diffusion","erfc_diffusion"}) {
        Json spec={{"law",law},{"left",1.},{"right",2.},{"middle",1.5},{"midpoint",.5},{"beta_a",2.},{"beta_b",3.},{"power",2.},{"gaussian_width",.2},{"diffusion_depth_m",-1e-6},{"dose_m2",1e14},{"surface_density_m3",1e20}};
        const auto p=ProfileFromJson(spec);const auto samples=ProfileSamples(spec,2e-6);
        for(int i:{0,40,100,200})EXPECT_DOUBLE_EQ(samples.at("value_SI")[i],scaps_core::evaluate_position_profile(p,2e-6*i/200.,2e-6))<<law;
    }
}

TEST(ScapsLab,SharedProfilePreviewWarningsAndMaterialUseCoreConventions) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().document=ReadProject(SCAPSLAB_PROJECT);
    auto &layers=w.Current().document["layers"];layers=Json::array({layers.front()});
    layers[0]["thickness_m"]=1e-6;auto second=layers[0];second["scapslab_id"]="shared-second";layers.push_back(second);
    Json shared={{"law","table"},{"coordinate",{0.,1.}},{"value",{1e20,3e20}},{"position_relative",true},{"shared_range",true},{"source_file","shared.grd"}};
    for(auto &layer:layers)layer["grading"]["Na_m3"]=shared;
    const auto response=Execute(w,{{"operation","preview_profile"},{"layer_id","shared-second"},{"parameter_id","Na_m3"}});
    ASSERT_TRUE(response.value("ok",false))<<response.dump();EXPECT_DOUBLE_EQ(response.at("operation_result").at("value_SI").front(),2e20);
    const auto file=Temp("scapslab-duplicate.grd");{std::ofstream out(file);out<<"position: relative\n1 3\n0 1\n0 9\n";}
    const auto profile=ProfileFromFile(file.string());EXPECT_FALSE(profile.at("scapslab_warnings").empty());EXPECT_EQ(profile.at("value").front(),1);std::filesystem::remove(file);
    // Round-trip an ordinary supported material through the product command/job path.
    auto ordinary=ReadProject(SCAPSLAB_PROJECT);ordinary["layers"]=Json::array({ordinary["layers"].front()});
    ASSERT_TRUE(Execute(w,{{"operation","edit"},{"document",ordinary}}).value("ok",false));
    const auto material=Temp("scapslab-roundtrip.material");const auto layerId=w.Current().document["layers"][0]["scapslab_id"];
    const auto saved=Execute(w,{{"operation","save_material"},{"layer_id",layerId},{"path",material.string()}});
    ASSERT_TRUE(saved.value("ok",false))<<saved.dump();
    const auto loaded=Execute(w,{{"operation","load_material"},{"layer_id",layerId},{"path",material.string()},{"selection","A"}});
    ASSERT_TRUE(loaded.value("ok",false))<<loaded.dump();EXPECT_EQ(w.Current().document["layers"][0]["scapslab_id"],layerId);std::filesystem::remove(material);
}

TEST(ScapsLab,OpticalFileTablesRemainUsableWithoutOriginalFiles) {
    auto doc=ReadProject(SCAPSLAB_PROJECT);const auto profiles=ExamplesDirectory()/"profiles";
    auto &light=doc["illumination"];light["mode"]="spectrum";light["spectrum_file"]=(profiles/"spectrum.spe").string();light["front_filter_file"]=(profiles/"filter.ftr").string();
    auto &layer=doc["layers"][0];layer["grading"]["composition"]={{"law","linear"},{"left",0.},{"right",1.}};
    auto &absorption=layer["absorption"];absorption["model"]="file";absorption["file"]=(profiles/"absorption.abs").string();
    absorption["composition"]={{"file_a",absorption["file"]},{"file_b",absorption["file"]},{"Eg_a_eV",1.1},{"Eg_b_eV",1.2},{"uv_a_nm",200.},{"uv_b_nm",200.}};
    auto defect=NewDefect();defect["scapslab_id"]="optical-profile-defect";defect["optical"]={{"enabled",true},{"electron_file",(profiles/"capture.opt").string()}};layer["defects"]=Json::array({defect});
    const auto embedded=SelfContained(doc);EXPECT_TRUE(embedded["illumination"]["spectrum_file"].get<std::string>().empty());
    EXPECT_EQ(embedded["layers"][0]["absorption"]["composition"]["table_b_nm_m1"].size(),3);
    EXPECT_EQ(embedded["layers"][0]["defects"][0]["optical"]["electron_table_nm_m2"].size(),3);
    EXPECT_NO_THROW((void)Model(embedded));
    const auto file=Temp("scapslab-all-optical-profiles.yaml");WriteProject(embedded,file.string());const auto restored=ReadProject(file.string());
    EXPECT_EQ(restored["layers"][0]["absorption"]["composition"]["table_b_nm_m1"],embedded["layers"][0]["absorption"]["composition"]["table_b_nm_m1"]);
    EXPECT_EQ(restored["layers"][0]["defects"][0]["optical"]["electron_table_nm_m2"],embedded["layers"][0]["defects"][0]["optical"]["electron_table_nm_m2"]);std::filesystem::remove(file);
}
