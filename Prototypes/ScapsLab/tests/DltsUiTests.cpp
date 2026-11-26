#include "Panel.hpp"
#include "Commands/Commands.hpp"
#include <gtest/gtest.h>
#include <imgui_internal.h>
#include <implot.h>
#include <scaps_core/boundary_probe.hpp>
#include <scaps_core/../../src/parallel.hpp>
#include <thread>
#include <fstream>
#include "Core/ScientificRuntime/Python/PythonInterpreter.hpp"
using namespace SST;
using namespace SST::ScapsLab;
namespace d=sst::physics::dlts;
namespace {
void WaitDlts(Workflow& w){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(90);while(w.Pending() && std::chrono::steady_clock::now()<end){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}ASSERT_FALSE(w.Pending())<<w.status;}
Ref<const ProjectDraft> DeviceDlts(bool light=false) {
    scaps_core::DeviceProject p;scaps_core::ProjectLayer l;l.thickness_m=2e-6;l.Nd_m3=1e22;
    scaps_core::ProjectDefect trap;trap.energy_reference=scaps_core::DefectEnergyReference::RelativeToEc;trap.energy_offset_eV=.35;trap.Nt_m3=1e20;trap.sigma_n_m2=trap.sigma_p_m2=1e-19;trap.charge_type=scaps_core::TrapChargeType::DonorLike;l.defects={trap};p.layers={l};
    p.left_contact.flat_band=p.right_contact.flat_band=true;p.left_contact.Sn_mps=p.left_contact.Sp_mps=p.right_contact.Sn_mps=p.right_contact.Sp_mps=1e5;p.mesh.points_per_layer=31;p.mesh.clustering_beta=0;p.mesh.adaptivity_level="0";
    p.illumination.enabled=light;p.illumination.mode="uniform";p.illumination.uniform_generation_m3s=1e23;return DraftFromProject(std::move(p));
}
Ref<RunPlan> PlanDlts(ActionKind kind) {
    auto plan=CreateRef<RunPlan>();for(auto& a:plan->actions)a.include=a.kind==kind;
    auto& a=plan->actions[int(kind)];a.range=SweepRange{SweepSampling::List,false,300,300,1,1,{300}};auto& c=a.dlts;
    c.reverse_V=0;c.pulse_V=1e-4;c.second_pulse_V=5e-5;c.pulse_width_s=2e-8;c.first_sample_s=1e-9;c.last_sample_s=2e-7;c.initial_step_s=1e-10;c.max_step_s=1e-9;c.gate_start_s=1e-8;c.windows=3;c.samples=33;c.max_steps=10000;c.rate_min_per_s=1e6;c.rate_max_per_s=1e9;c.grid_size=31;
    if(kind==ActionKind::Capture)c.series=DltsSeries::DoublePulse;return plan;
}
}
TEST(DltsUi,SettingsRoundTripValidationAndOneGestureUndo) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.Current().SetDraft(DeviceDlts());const auto original=w.Current().runPlan;
    auto plan=PlanDlts(ActionKind::Dlts);w.SetRunPlanTyped(plan,"pulse-drag");plan=CreateRef<RunPlan>(*plan);plan->actions[6].dlts.pulse_V=2e-4;w.SetRunPlanTyped(plan,"pulse-drag");EXPECT_EQ(w.Current().undo.GetUndoDepth(),1u);
    ASSERT_TRUE(w.Exec(Cmd::Undo::With()).at("ok"));EXPECT_EQ(w.Current().runPlan,original);ASSERT_TRUE(w.Exec(Cmd::Redo::With()).at("ok"));
    const auto json=RunPlanJson(*w.Current().runPlan);EXPECT_EQ(RunPlanJson(ReadRunPlan(json)),json);
    plan->actions[6].dlts.gate_start_s=1;EXPECT_THROW(ValidateRunPlan(*plan,*w.Current().draft),std::invalid_argument);
    auto bad=DltsConfigJson(DltsConfig{});bad["readout"]=99;EXPECT_THROW(ReadDltsConfig(bad),std::invalid_argument);
    bad=DltsConfigJson(DltsConfig{});bad["samples"]=129.5;EXPECT_THROW(ReadDltsConfig(bad),std::invalid_argument);
    bad["samples"]=4294967425ull;EXPECT_THROW(ReadDltsConfig(bad),std::invalid_argument);
    auto extension=json;extension["dlts"]["protocol"]["future_metadata"]="preserve";EXPECT_EQ(RunPlanJson(ReadRunPlan(extension),extension),extension);
}
TEST(DltsPhysics,RateWindowFamilyAndSingleLevelEaSigmaRecovery) {
    DltsConfig c;c.first_sample_s=1e-9;c.last_sample_s=1;c.gate_start_s=1e-5;c.window_ratio=3;c.windows=5;
    DltsData data;d::EmissionRequest emission;emission.dos_mass_m0=c.dos_mass_m0;emission.velocity_mass_m0=c.velocity_mass_m0;emission.degeneracy_factor=c.degeneracy_factor;emission.sigma_infinity_cm2=1e-16;emission.trap_activation_eV=.19;
    for(int i=0;i<=400;++i)emission.temperature_K.push_back(65+i*.4);const auto rates=d::emission(emission);
    for(std::size_t i=0;i<rates.rate_per_s.size();++i){auto trace=std::make_shared<DltsTrace>();trace->id="synthetic-"+std::to_string(i);trace->temperature_K=rates.temperature_K[i];trace->pulse_width_s=.001;trace->baseline_F=1e-9;std::vector<double> time,cap;for(int k=0;k<601;++k){const double t=1e-9*std::pow(1e9,double(k)/600);time.push_back(t);cap.push_back(1e-9+1e-11*std::exp(-rates.rate_per_s[i]*t));}trace->time_s=Column::Intern(std::move(time));trace->capacitance_F=Column::Intern(std::move(cap));data.traces.push_back(trace);}
    AnalyzeDlts(data,c,false);ASSERT_TRUE(data.arrhenius);EXPECT_NEAR(data.arrhenius->trap_activation_eV,.19,.003);EXPECT_NEAR(data.arrhenius->sigma_infinity_cm2/1e-16,1,.2);
    for(auto kind:{d::FilterKind::Boxcar,d::FilterKind::MultiGate,d::FilterKind::Square,d::FilterKind::Sine,d::FilterKind::Exponential}){c.filter=kind;for(const auto& filter:DltsFilters(c)){const auto window=d::rateWindow({filter});EXPECT_TRUE(std::isfinite(window.rate_per_s));EXPECT_GT(std::abs(window.gain),1e-6);}}
}
TEST(DltsPhysics,LaplaceSeparatesTwoLevelsAndCaptureRecoversCrossSection) {
    DltsConfig c;c.rate_min_per_s=10;c.rate_max_per_s=300;c.grid_size=101;c.alpha_method=d::AlphaMethod::Fixed;c.alpha=1e-5;
    auto trace=std::make_shared<DltsTrace>();trace->temperature_K=300;trace->pulse_width_s=.001;trace->baseline_F=1e-9;
    std::vector<double> time,cap;for(int i=0;i<401;++i){const double t=1e-6+double(i)*.15/400;time.push_back(t);cap.push_back(1e-9+1e-11*std::exp(-50*t)+.8e-11*std::exp(-100*t));}trace->time_s=Column::Intern(std::move(time));trace->capacitance_F=Column::Intern(std::move(cap));
    DltsData data;data.traces.push_back(trace);AnalyzeDlts(data,c,true);ASSERT_TRUE(data.traces[0]->laplace);const auto& l=*data.traces[0]->laplace;ASSERT_GE(l.peaks.size(),2u);for(double rate:{50.,100.})EXPECT_TRUE(std::any_of(l.peaks.begin(),l.peaks.end(),[&](const auto& p){return std::abs(p.rate_per_s/rate-1)<.12;}));
    std::vector<double> widths,amps;for(int i=0;i<31;++i){const double t=1e-8*std::pow(1e5,double(i)/30);widths.push_back(t);amps.push_back(2e-11*(-std::expm1(-1e5*t)));}
    const auto fit=FitCapture(widths,amps,1e21,1e5);EXPECT_NEAR(fit.sigma_m2/1e-21,1,1e-6);EXPECT_LT(fit.rms_F,1e-18);EXPECT_THROW(FitCapture({1,2,3},{1,1,1},1,1),std::invalid_argument);
}
TEST(DltsPhysics,DepthAndDdltsDepletionShellRecoverUniformTrapDensity) {
    const double nd=1e15,nt=1e12,baseline=1e-9;DltsConfig c;const auto filter=DltsFilters(c).front();const auto window=d::rateWindow({filter});
    for(double reverse:{.5e-6,.7e-6,1e-6}) {
        const double pulse=.2e-6,f=1-pulse*pulse/(reverse*reverse),amplitude=nt/(2*nd)*baseline*f;
        d::ConcentrationRequest request;request.donor_density_cm3=nd;request.baseline_F=baseline;request.gain=window.gain;request.signal_F=amplitude*window.gain;request.relative_permittivity=11.7;request.reverse_width_m=reverse;request.pulse_width_m=pulse;
        EXPECT_NEAR(d::concentration(request).trap_density_cm3/nt,1,1e-12);
        const double nearby=.21e-6,paired=nt/(2*nd)*baseline*(1-nearby*nearby/(reverse*reverse));
        const double difference=amplitude-paired,shell=(nearby*nearby-pulse*pulse)/(reverse*reverse);
        EXPECT_NEAR((2*nd*difference/baseline/shell)/nt,1,1e-12);
    }
}
TEST(DltsPhysics,UnresolvedTraceKeepsSamplesWithoutInferringALaplacePeak) {
    DltsData data;auto trace=std::make_shared<DltsTrace>();trace->temperature_K=300;trace->pulse_width_s=.001;trace->baseline_F=1e-9;trace->readout_resolution_F=1e-15;
    std::vector<double> time,cap;for(int i=0;i<129;++i){const double t=1e-6*std::pow(1e4,double(i)/128);time.push_back(t);cap.push_back(1e-9+1e-18*std::exp(-100*t));}trace->time_s=Column::Intern(std::move(time));trace->capacitance_F=Column::Intern(std::move(cap));data.traces.push_back(trace);
    AnalyzeDlts(data,DltsConfig{},true);EXPECT_FALSE(data.traces[0]->laplace);EXPECT_EQ(data.traces[0]->time_s.id(),trace->time_s.id());EXPECT_EQ(data.traces[0]->capacitance_F.id(),trace->capacitance_F.id());EXPECT_NE(data.traces[0]->message.find("resolution"),std::string::npos);
}
TEST(DltsPhysics,ProcessingUsesTheSameCancellationScopeAsAcquisition) {
    std::stop_source stop;stop.request_stop();scaps_core::ExecutionContext execution;execution.stop=stop.get_token();scaps_core::detail::ExecutionScope scope(execution,false);DltsData data;
    EXPECT_THROW(AnalyzeDlts(data,DltsConfig{},true),scaps_core::ExecutionCancelled);
}
TEST(DltsUi,AllVariantJobsPersistPerRunAndDoNotUseResultAdapters) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(DeviceDlts(true));
    for(auto kind:{ActionKind::Dlts,ActionKind::Laplace,ActionKind::Capture,ActionKind::Optical}) {
        auto plan=PlanDlts(kind);w.SingleShotTyped(plan);WaitDlts(w);const auto result=w.Current().Ac(ActionId(kind));ASSERT_TRUE(result)<<w.status;ASSERT_TRUE(result->dlts);ASSERT_EQ(result->dlts->traces.size(),1u);const auto& trace=*result->dlts->traces.front();ASSERT_EQ(trace.status,"converged")<<trace.message;EXPECT_EQ(trace.time_s.size(),33u);EXPECT_EQ(trace.capacitance_F.size(),33u);EXPECT_FALSE(result->HasView());
        const auto encoded=result->Dump();EXPECT_EQ(AcFromJson(encoded).Dump(),encoded);auto invalid=encoded;invalid["dlts"]["traces"][0]["time_s"][1]=0;EXPECT_THROW(AcFromJson(invalid),std::invalid_argument);
        if(kind==ActionKind::Capture){
            ASSERT_EQ(trace.reference_capacitance_F.size(),33u);const auto& p=w.Current().draft->project;auto device=scaps_core::make_device_from_project(p);device.illumination.enabled=false;
            const auto mesh=scaps_core::mesh_from_coordinates(device,result->mesh_x_m.get());const auto& c=plan->actions[8].dlts;
            scaps_core::TransientConfig time;time.retention=scaps_core::TransientRetention::ObservablesOnly;time.initial_step_s=c.initial_step_s;time.max_step_s=c.max_step_s;time.max_steps=c.max_steps;time.prebias_generation_multiplier=0;time.readout_frequency_Hz=plan->actions[8].frequency_Hz;
            for(double t:trace.time_s)time.sample_times_s.push_back(c.pulse_width_s+t);
            for(double pulse:{c.pulse_V,c.second_pulse_V}) {
                const auto direct=scaps_core::solve_transient(device,mesh,-c.reverse_V,300,{{0,-pulse,0},{c.pulse_width_s,-c.reverse_V,0}},c.pulse_width_s+c.last_sample_s,time,p.solver,scaps_core::physical_constants(p.constants_mode));
                ASSERT_EQ(direct.status,scaps_core::TransientStatus::Converged);ASSERT_EQ(direct.readouts.size(),33u);
                for(std::size_t i=0;i<33;++i)if(pulse==c.second_pulse_V)EXPECT_DOUBLE_EQ(trace.reference_capacitance_F[i],direct.readouts[i].C_F);else EXPECT_DOUBLE_EQ(trace.capacitance_F[i]+trace.reference_capacitance_F[i],direct.readouts[i].C_F);
            }
        }
    }
    EXPECT_EQ(w.Current().history.size(),4u);const auto first=w.Current().RunResult(w.Current().history.front().at("run_id"));w.SingleShotTyped(PlanDlts(ActionKind::Dlts));WaitDlts(w);EXPECT_EQ(w.Current().history.size(),5u);EXPECT_EQ(w.Current().RunResult(w.Current().history.front().at("run_id")),first);
    auto file=w.Workspace();ValidateWorkspace(file);Workflow loaded(CreateRef<JobSystem>(nullptr,1));loaded.SetWorkspace(file);EXPECT_EQ(loaded.Current().Ac("dlts")->Dump(),w.Current().Ac("dlts")->Dump());
    const auto directory=std::filesystem::temp_directory_path()/"scapslab-dlts-export";std::filesystem::create_directories(directory);const auto selected=w.ResultRuns().front();
    EXPECT_EQ(ExportResults({selected},directory/"transient.csv","csv").size(),1u);std::ifstream csv(directory/"transient.csv");std::string header;std::getline(csv,header);EXPECT_NE(header.find("time_s"),std::string::npos);EXPECT_NE(header.find("capacitance_F"),std::string::npos);csv.close();
#if DS_PYTHON_CAPI_AVAILABLE
    RegisterPython();PythonInterpreter python;ASSERT_TRUE(python.Start());
    EXPECT_EQ(ExportResults({selected},directory/"transient.svg","svg").size(),1u);EXPECT_GT(std::filesystem::file_size(directory/"transient.svg"),100u);
#endif
    std::filesystem::remove_all(directory);
    if(const char* path=std::getenv("SCAPSLAB_DLTS_EVIDENCE")){std::ofstream(std::filesystem::path(path)/"variants.scapslab")<<file.dump(2);}
}
TEST(DltsUi,ProgressCancelAndInactiveCaseKeepPriorResults) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(DeviceDlts());w.SingleShotTyped(PlanDlts(ActionKind::Dlts));WaitDlts(w);const auto previous=w.Current().Ac("dlts");ASSERT_TRUE(previous);const auto id=w.Current().id;
    auto plan=PlanDlts(ActionKind::Dlts);plan->actions[6].dlts.last_sample_s=.1;plan->actions[6].dlts.max_step_s=1e-9;plan->actions[6].dlts.max_steps=2000000;w.SingleShotTyped(plan);const auto job=w.ActiveJob();
    for(int i=0;i<100 && w.Pending();++i){w.OnUpdate(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    const auto start=std::chrono::steady_clock::now();ASSERT_TRUE(w.Exec(Cmd::Cancel::With()).at("ok"));WaitDlts(w);EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),3);EXPECT_EQ(w.Current().Ac("dlts"),previous);EXPECT_EQ(w.jobs->GetJob(job)->status,JobStatus::Cancelled);
    ASSERT_TRUE(w.Exec(Cmd::DuplicateDevice::With()).at("ok"));const auto other=w.Current().id;w.SelectDevice(id);w.SingleShotTyped(PlanDlts(ActionKind::Dlts));w.SelectDevice(other);WaitDlts(w);EXPECT_EQ(w.project.active_device,other);EXPECT_TRUE(w.Device(id).Ac("dlts"));
}
TEST(DltsUi,CancelRetainsCompletedTemperatureMembersAsANewPartialRun) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(DeviceDlts());auto plan=PlanDlts(ActionKind::Dlts);plan->actions[6].range=SweepRange{SweepSampling::Points,false,290,310,1,101};w.SingleShotTyped(plan);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);bool published=false;
    while(w.Pending() && std::chrono::steady_clock::now()<end){w.OnUpdate(0);if(!w.Current().liveDlts.empty()){published=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    ASSERT_TRUE(published);ASSERT_TRUE(w.Exec(Cmd::Cancel::With()).at("ok"));WaitDlts(w);const auto result=w.Current().Ac("dlts");ASSERT_TRUE(result);ASSERT_TRUE(result->dlts);EXPECT_TRUE(result->dlts->cancelled);EXPECT_GE(result->dlts->traces.size(),1u);EXPECT_LT(result->dlts->traces.size(),101u);EXPECT_EQ(w.Current().history.size(),1u);EXPECT_EQ(w.Current().RunResult(w.Current().selectedRun),result);ValidateWorkspace(w.Workspace());
}
TEST(DltsUi,FrozenAndDemodulatedReadoutsAgreeForSmallSignalSteadyDevice) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;auto draft=CreateRef<ProjectDraft>(*DeviceDlts());draft->project.layers[0].defects.clear();draft->layers[0].defects.clear();w.Current().SetDraft(draft);
    auto plan=PlanDlts(ActionKind::Dlts);auto& a=plan->actions[6];a.frequency_Hz=1e8;a.dlts.first_sample_s=2e-8;a.dlts.gate_start_s=2e-8;a.dlts.pulse_V=0;a.dlts.ac_amplitude_V=1e-5;a.dlts.max_step_s=1e-10;
    w.SingleShotTyped(plan);WaitDlts(w);const auto frozen=w.Current().Ac("dlts");ASSERT_TRUE(frozen);ASSERT_EQ(frozen->dlts->traces[0]->status,"converged")<<frozen->dlts->traces[0]->message;
    a.dlts.readout=DltsReadout::Demodulated;w.SingleShotTyped(plan);WaitDlts(w);const auto demod=w.Current().Ac("dlts");ASSERT_TRUE(demod);const auto& trace=*demod->dlts->traces[0];ASSERT_EQ(trace.status,"converged")<<trace.message;
    EXPECT_NEAR(trace.capacitance_F[32]/frozen->dlts->traces[0]->capacitance_F[32],1,.05);
}
TEST(DltsUi,PulseWidthAndDepthSeriesUseTheirRequestedAxes) {
    Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(DeviceDlts());w.project.ui["dlts_action"]=8;
    for(auto series:{DltsSeries::PulseWidth,DltsSeries::Depth}) {
        auto plan=PlanDlts(ActionKind::Capture);auto& a=plan->actions[8];a.dlts.series=series;
        const std::vector<double> values=series==DltsSeries::PulseWidth?std::vector<double>{1e-8,2e-8,4e-8,8e-8}:std::vector<double>{-1e-4,-5e-5,0};a.range->custom=values;
        w.SetRunPlanTyped(plan,"series");w.SingleShotTyped(plan);WaitDlts(w);const auto result=w.Current().Ac("capture_dlts");ASSERT_TRUE(result);ASSERT_EQ(result->dlts->traces.size(),values.size());
        for(std::size_t i=0;i<values.size();++i){const auto& trace=*result->dlts->traces[i];ASSERT_EQ(trace.status,"converged")<<trace.message;EXPECT_EQ(trace.temperature_K,300);if(series==DltsSeries::PulseWidth)EXPECT_EQ(trace.pulse_width_s,values[i]);else EXPECT_EQ(trace.reverse_V,values[i]);}
        if(const char* evidence=std::getenv("SCAPSLAB_DLTS_EVIDENCE"))std::ofstream(std::filesystem::path(evidence)/(series==DltsSeries::PulseWidth?"pulse-width.scapslab":"depth.scapslab"))<<w.Workspace().dump(2);
    }
}
TEST(DltsUi,FourPanelsKeepTypedPlotsWithoutIdConflictsOrAutoRuns) {
    ImGui::CreateContext();ImPlot::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,2200};io.Fonts->AddFontDefault();io.Fonts->Build();io.ConfigDebugHighlightIdConflicts=true;
    {
        Workflow w(CreateRef<JobSystem>(nullptr,1));w.lightResponse=true;w.Current().SetDraft(DeviceDlts());Panel panel(&w,25,true);
        for(int action=6;action<10;++action){w.project.ui["dlts_action"]=action;for(int frame=0;frame<3;++frame){io.DeltaTime=1.f/60;ImGui::NewFrame();ImGui::SetNextWindowSize({1300,2100});ImGui::Begin("DLTS test");panel.RenderContent();ImGui::End();ImGui::Render();EXPECT_EQ(GImGui->DebugDrawIdConflictsCount,0);EXPECT_FALSE(w.Pending());}}
    }
    ImPlot::DestroyContext();ImGui::DestroyContext();
}
