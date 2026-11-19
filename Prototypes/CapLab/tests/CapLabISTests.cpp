#include <gtest/gtest.h>
#include "Domain/CapLabIS.hpp"
#include "Domain/CapLabAnalysis.hpp"
#include "CapLabCommands.hpp"
#include "AnalysisRunner.h"
#include "TestProject.hpp"
#include <sst/physics/impedance.hpp>
#include <iostream>

namespace SST::Prototypes::CapLab {
namespace {
Json Measurement(const std::vector<double> &f) {
    return {{"id",2u},{"sample_id",1u},{"kind","FrequencySweep"},{"variant","ReZ_ImZ"},
        {"names",{"f","ReZ","ImZ"}},{"units",{"Hz","Ohm","Ohm"}},{"temperature_K",270.},
        {"bias_V",-.15},{"row_ids",std::vector<unsigned>(f.size(),1)},
        {"columns",{f,std::vector<double>(f.size(),100.),std::vector<double>(f.size(),0.)}}};
}
Json Circuit(const std::string &topology) {
    return {{"id",3u},{"sample_id",1u},{"name","test"},{"schematic",CircuitJson(ccui::importString(topology))}};
}
std::complex<double> Z(const Json &r,std::size_t i) {
    return {r.at("model_ReZ_Ohm").at(i).get<double>(),r.at("model_ImZ_Ohm").at(i).get<double>()};
}
}
TEST(CapLabIS, AnalyticR_C_SeriesRC_RandlesAtExactMeasuredFrequencies) {
    const std::vector<double> f{1234.5,.001,7.25,94001,9e7};
    auto m=Measurement(f);
    JobContext context({}, {}, {}, {}, [] {return false;});
    const double pi=std::acos(-1.);
    for (const std::string topology : {"R0","C0","R0-C0","R0-p(R1,C1)"}) {
        auto c=Circuit(topology); auto d=ReadCircuit(c.at("schematic"));
        for (auto &e : d.elements) e.value=e.kind==ccui::Kind::C ? 20e-6 : e.name=="R1" ? 500 : 20;
        c["schematic"]=CircuitJson(d);
        auto result=SimulateIS(m,c,Json::object(),context);
        ASSERT_EQ(result.at("status"),"valid") << result.at("diagnostics");
        EXPECT_EQ(result.at("frequency_Hz"),f); EXPECT_EQ(result.at("temperature_K"),270.); EXPECT_EQ(result.at("bias_V"),-.15);
        double maximum=0;
        for (std::size_t i=0;i<f.size();++i) {
            // SST-CapLab M5 / PFS pp.94?96; independent ideal impedances.
            const std::complex<double> yc{0,2*pi*f[i]*20e-6};
            const auto expected=topology=="R0" ? std::complex<double>{20,0} : topology=="C0" ? 1./yc :
                topology=="R0-C0" ? 20.+1./yc : 20.+1./(1./500.+yc);
            maximum=std::max(maximum,std::abs(Z(result,i)-expected)/std::abs(expected));
            EXPECT_LT(std::abs(Z(result,i)-expected)/std::abs(expected),1e-8);
        }
        std::cout << "M5 acceptance " << topology << " max relative error=" << maximum << '\n';
        EXPECT_TRUE(result.at("plots").at(0).at("equal_aspect"));
        EXPECT_EQ(result.at("plots").size(),7);
    }
}
TEST(CapLabIS, MeasuredVariantsAndInvalidAdmittanceRemainExplicit) {
    JobContext context({}, {}, {}, {}, [] {return false;});
    auto m=Measurement({123.});
    auto c=Circuit("R0");
    m["variant"]="C_G"; m["names"]={"f","C","G"}; m["units"]={"Hz","F","S"}; m["columns"]={{123.},{2e-6},{.01}};
    auto r=SimulateIS(m,c,Json::object(),context);
    const std::complex<double> expected=1./std::complex<double>(.01,2*std::acos(-1.)*123*2e-6);
    EXPECT_NEAR(r["measured_ReZ_Ohm"][0].get<double>(),expected.real(),1e-12);
    EXPECT_NEAR(r["measured_ImZ_Ohm"][0].get<double>(),expected.imag(),1e-12);
    m["variant"]="Cp_Rp"; m["names"][2]="Rp"; m["units"][2]="Ohm"; m["columns"][2]={100.};
    auto rp=SimulateIS(m,c,Json::object(),context);
    EXPECT_EQ(rp.at("measured_ReZ_Ohm"),r.at("measured_ReZ_Ohm"));
    m["columns"][2]={0.};
    r=SimulateIS(m,c,Json::object(),context);
    EXPECT_EQ(r.at("status"),"invalid"); EXPECT_TRUE(r["measured_ReZ_Ohm"][0].is_null());
    EXPECT_EQ(r["diagnostics"][0]["code"],"invalid_measurement");
    m["variant"]="C_G"; m["names"][2]="G"; m["units"][2]="S"; m["columns"][1]={0.};
    r=SimulateIS(m,c,Json::object(),context); EXPECT_TRUE(r["measured_ReZ_Ohm"][0].is_null());
}
TEST(CapLabIS, InvalidPortSingularCircuitAndCancellation) {
    auto m=Measurement({100,200,1000}); auto c=Circuit("R0");
    JobContext context({}, {}, {}, {}, [] {return false;});
    auto bad=c; bad["schematic"]["port"]={0,0};
    EXPECT_THROW(SimulateIS(m,bad,Json::object(),context),std::exception);
    // A persisted ideal V source at the port conflicts with analysis excitation.
    auto d=ReadCircuit(c.at("schematic"));
    const auto i=d.add(ccui::Kind::V,{}); d.wires.push_back({d.elements[i].nodes[0],d.port[0]}); d.wires.push_back({d.elements[i].nodes[1],d.port[1]});
    c["schematic"]=CircuitJson(d);
    auto r=SimulateIS(m,c,Json::object(),context);
    EXPECT_EQ(r.at("status"),"invalid"); EXPECT_TRUE(r.at("model_ReZ_Ohm").at(0).is_null()); EXPECT_FALSE(r.at("diagnostics").empty());
    c=Circuit("R0");
    d=ReadCircuit(c.at("schematic"));
    const int cap=d.add(ccui::Kind::C,{}), ind=d.add(ccui::Kind::L,{});
    d.elements[cap].value=1; d.elements[ind].value=1;
    d.wires.push_back({d.elements[cap].nodes[0],d.elements[ind].nodes[0]});
    d.wires.push_back({d.elements[cap].nodes[1],d.ground});
    d.wires.push_back({d.elements[ind].nodes[1],d.ground});
    c["schematic"]=CircuitJson(d);
    auto resonant=Measurement({1/(2*std::acos(-1.))});
    r=SimulateIS(resonant,c,Json::object(),context);
    EXPECT_EQ(r.at("status"),"invalid");
    bool singular=false;
    for (const auto &diagnostic : r.at("diagnostics")) singular |= diagnostic.at("code")=="singular_matrix";
    EXPECT_TRUE(singular) << r.at("diagnostics");
    c=Circuit("R0");
    bool cancel=false; int completed=0;
    JobContext cancelled([&](float done,float) {if (done>0) {cancel=true; ++completed;}}, {}, {}, {}, [&] {return cancel;});
    EXPECT_THROW(SimulateIS(m,c,Json::object(),cancelled),JobCancelledException); EXPECT_EQ(completed,1);
    auto missing=m; missing["temperature_K"]=nullptr;
    EXPECT_THROW(SimulateIS(missing,c,Json::object(),context),std::exception);
    missing["bias_V"]=nullptr;
    EXPECT_NO_THROW(SimulateIS(missing,c,{{"temperature_K",300},{"bias_V",0}},context));
    m["columns"][0]={100,100,200}; EXPECT_THROW(SimulateIS(m,c,Json::object(),context),std::exception);
}
TEST(CapLabIS, ExistingCircuitCoreUISelfChecks) {EXPECT_EQ(ccui::selfCheck(),0);}
TEST(CapLabIS, DiodeBiasTemperatureAndSpiceFCContinuation) {
    JobContext context({}, {}, {}, {}, [] {return false;});
    for (const auto kind : {ccui::Kind::PN,ccui::Kind::Schottky}) {
        ccui::CircuitDocument d; d.add(kind,{}); d.ground=d.elements[0].nodes[1]; d.port=d.elements[0].nodes;
        auto &e=d.elements[0]; e.value=1e-12; e.secondary=1; e.cj0=2e-9; e.vbi=.8; e.m=.5; e.fc=.4; e.tau=1e-8;
        Json c{{"id",3u},{"sample_id",1u},{"name","diode"},{"schematic",CircuitJson(d)}};
        for (double t : {250.,350.}) for (double bias : {-2.4,0.,.32,.48}) {
            auto m=Measurement({1234.5}); m["temperature_K"]=t; m["bias_V"]=bias;
            auto r=SimulateIS(m,c,Json::object(),context);
            ASSERT_EQ(r.at("status"),"valid") << r.at("diagnostics");
            const double conductance=1e-12/(1.380649e-23*t/1.602176634e-19)*std::exp(bias/(1.380649e-23*t/1.602176634e-19));
            // Literal independent SPICE values, rounded to 16 significant digits.
            const double cj=bias==-2.4 ? 1e-9 : bias==0 ? 2e-9 : bias==.32 ? 2.581988897471611e-9 : 3.012320380383546e-9;
            const auto y=1./Z(r,0);
            EXPECT_NEAR(y.imag()/(2*std::acos(-1.)*1234.5),cj+1e-8*conductance,1e-15);
            EXPECT_NEAR(y.real(),conductance,std::max(1e-14,conductance*1e-8));
        }
    }
    struct Custom final : circuitcore::IJunctionCapacitance {
        circuitcore::JunctionCharge evaluate(double v,const circuitcore::JunctionCapacitanceParameters &,double,double) const override {return {3e-9*v,3e-9};}
    };
    auto custom=std::make_shared<Custom>();
    auto registry=circuitcore::DeviceRegistry::createWithBuiltinDevices();
    registry.registerModel(std::make_shared<circuitcore::PnJunctionDiode>(custom));
    circuitcore::CircuitBuilder builder;
    auto node=builder.addNode("port"); builder.addPnDiode(node,builder.ground(),"custom");
    circuitcore::DiagnosticSink diagnostics;
    const auto compiled=circuitcore::Simulator::compile(builder.build(),registry,diagnostics);
    const auto parameters=compiled.defaultParameters();
    circuitcore::ImpedanceSweepOptions options; options.port={node,builder.ground()}; options.frequenciesHz={1000};
    const auto result=circuitcore::Simulator::impedanceSweep(compiled,parameters,{},compiled.createInitialState(parameters),options);
    ASSERT_EQ(result.points.size(),1);
    EXPECT_NEAR(result.points[0].admittance.imag()/(2*std::acos(-1.)*1000),3e-9,1e-18);
}
TEST(CapLabIS, CircuitCommandsPersistenceUndoAndLateJobRejection) {
    auto bus=CreateRef<EventBus>(); auto jobs=CreateRef<JobSystem>(nullptr,1);
    auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
    CapLabWorkflow w(jobs,bus,undo); RegisterCommands(commands,bus,w); w.document.project=FixtureProject();
    const auto sample=w.document.project.samples[0].id, mid=w.document.project.sweeps[0].id;
    auto edit=[&](Json value) {CommandContext c; c.Set("edit",value); return commands->Execute(CommandID{"caplab.circuit"},std::move(c));};
    ASSERT_TRUE(edit({{"action","create"},{"sample_id",sample},{"name","Randles"},{"topology","R0-p(R1,C1)"}}));
    const auto cid=w.document.project.circuits[0].at("id");
    const auto created=Serialize(w.document.project);
    EXPECT_EQ(Serialize(Parse(created)),created);
    ASSERT_TRUE(undo->Undo()); EXPECT_TRUE(w.document.project.circuits.empty());
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(w.document.project),created);
    auto schematic=w.document.project.circuits[0].at("schematic"); schematic["elements"][0]["x"]=321.;
    ASSERT_TRUE(edit({{"action","replace"},{"circuit_id",cid},{"schematic",schematic}}));
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(w.document.project),created);
    auto invalid=schematic; invalid["elements"][0]["value_unit"]="F";
    EXPECT_FALSE(edit({{"action","replace"},{"circuit_id",cid},{"schematic",invalid}})); EXPECT_EQ(Serialize(w.document.project),created);
    invalid=schematic; invalid["port"]={999,0};
    EXPECT_FALSE(edit({{"action","replace"},{"circuit_id",cid},{"schematic",invalid}})); EXPECT_EQ(Serialize(w.document.project),created);
    auto sub=bus->Subscribe<DocumentRequested>([&](const auto &e) {w.Start(e.operation,e.target);});
    w.analysisOptions={{"type","Circuit"},{"measurement_id",mid},{"circuit_id",cid},{"bias_V",0}};
    ASSERT_TRUE(jobs->PauseAllJobs());
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue(); ASSERT_NE(w.activeJob,0);
    ASSERT_TRUE(edit({{"action","replace"},{"circuit_id",cid},{"schematic",schematic}}));
    ASSERT_TRUE(jobs->ResumeAllJobs()); jobs->Shutdown(); w.OnUpdate(0);
    EXPECT_TRUE(w.document.project.analyses.empty()); EXPECT_NE(w.status.find("Stale"),std::string::npos);
    w.jobs=CreateRef<JobSystem>(nullptr,1);
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.analyze"})); bus->ProcessQueue();
    w.jobs->Shutdown(); w.OnUpdate(0);
    ASSERT_EQ(w.document.project.analyses.size(),1) << w.status;
    EXPECT_EQ(w.document.project.analyses[0]["status"],"valid");
    EXPECT_EQ(w.document.project.analyses[0]["circuit"],w.document.project.circuits[0]);
    const auto analyzed=Serialize(w.document.project);
    EXPECT_EQ(Serialize(Parse(analyzed)),analyzed);
    ASSERT_TRUE(undo->Undo()); EXPECT_TRUE(w.document.project.analyses.empty());
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(w.document.project),analyzed);
    schematic["elements"][0]["value"]=200.;
    ASSERT_TRUE(edit({{"action","replace"},{"circuit_id",cid},{"schematic",schematic}}));
    EXPECT_EQ(w.document.project.analyses[0]["status"],"stale");
    ASSERT_TRUE(edit({{"action","delete"},{"circuit_id",cid}})); EXPECT_EQ(w.document.project.analyses[0]["status"],"missing_input");
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(w.document.project.circuits.size(),1);
}
}
#include <random>
namespace SST::Prototypes::CapLab {
namespace {
Json RandlesMeasurement(bool noisy=false) {
    std::vector<double> f,re,im;
    std::mt19937 rng(6006); std::normal_distribution<double> noise(0.,.005);
    for (int i=0;i<31;++i) {
        const double frequency=std::pow(10.,-1.+i*.2);
        // Independent ideal Randles equation, SST-CapLab 4.3 / PFS K pp.94-96.
        const auto z=20.+1./std::complex<double>(1./500.,2*std::acos(-1.)*frequency*20e-6);
        f.push_back(frequency); re.push_back(z.real()+(noisy ? std::abs(z)*noise(rng) : 0.));
        im.push_back(z.imag()+(noisy ? std::abs(z)*noise(rng) : 0.));
    }
    auto m=Measurement(f); m["columns"]={f,re,im}; return m;
}
Json RandlesStart() {
    auto c=Circuit("R0-p(R1,C1)");
    for (auto &e : c["schematic"]["elements"]) e["value"]=e["name"]=="R0" ? 35. : e["name"]=="R1" ? 300. : 35e-6;
    return c;
}
Json FitSettings() {
    Json parameters=Json::array();
    const auto start=RandlesStart();
    for (const auto &e : start.at("schematic").at("elements")) {
        const bool cap=e.at("kind")==1;
        parameters.push_back({{"element",e.at("name")},{"field","value"},{"unit",cap ? "F" : "ohm"},
            {"free",true},{"min",cap ? 1e-6 : 1.},{"max",cap ? 1e-3 : 2000.}});
    }
    return {{"Z_floor_Ohm",1.},{"parameters",parameters},{"max_iterations",350},{"score_tolerance",1e-12}};
}
}
TEST(CapLabISFit, IndependentRandlesRecoveryAndSeededNoiseScore) {
    JobContext context({}, {}, {}, {}, [] {return false;});
    for (bool noisy : {false,true}) {
        const auto m=RandlesMeasurement(noisy), c=RandlesStart(), settings=FitSettings();
        const auto before=c.dump();
        const auto r=FitIS(m,c,settings,context);
        double maximum=0;
        for (const auto &e : r.at("circuit").at("schematic").at("elements")) {
            const double expected=e.at("name")=="R0" ? 20. : e.at("name")=="R1" ? 500. : 20e-6;
            maximum=std::max(maximum,std::abs(e.at("value").get<double>()/expected-1));
        }
        std::cout << "M6 " << (noisy ? "noise" : "noiseless") << " max_relative_error=" << maximum << " initial_score=" << r.at("initial_score") << " score=" << r.at("score") << '\n';
        if (!noisy) EXPECT_LT(maximum,1e-3);
        EXPECT_LT(r.at("score").get<double>(),r.at("initial_score").get<double>());
        EXPECT_EQ(r.at("weighting"),"modulus_floor"); EXPECT_EQ(r.at("Z_floor_Ohm"),1.);
        EXPECT_TRUE(r.at("covariance").is_array()); EXPECT_EQ(r.at("plots").size(),7);
        EXPECT_EQ(c.dump(),before);
        double independent=0;
        for (std::size_t i=0;i<r.at("frequency_Hz").size();++i) {
            const double scale=std::max(std::hypot(r["measured_ReZ_Ohm"][i].get<double>(),r["measured_ImZ_Ohm"][i].get<double>()),1.);
            independent+=(std::pow(r["residual_ReZ_Ohm"][i].get<double>(),2)+std::pow(r["residual_ImZ_Ohm"][i].get<double>(),2))/(scale*scale);
        }
        EXPECT_NEAR(independent,r.at("score").get<double>(),1e-14);
    }
}
TEST(CapLabISFit, FixedBoundsSigmaFrequencySelectionAndStopStatus) {
    JobContext context({}, {}, {}, {}, [] {return false;});
    auto c=RandlesStart(), s=FitSettings();
    c["schematic"]["elements"][0]["value"]=20.; s["parameters"][0]["free"]=false;
    s["parameters"][2]["min"]=30e-6;
    s["sigma_Ohm"]=std::vector<double>(31,2.); s["frequency_min_Hz"]=1.; s["frequency_max_Hz"]=10000.;
    s["max_iterations"]=50; s["score_tolerance"]=0.;
    const auto r=FitIS(RandlesMeasurement(),c,s,context);
    EXPECT_EQ(r["circuit"]["schematic"]["elements"][0]["value"],20.);
    for (const auto &p : r.at("parameters")) {EXPECT_GE(p.at("fitted").get<double>(),p.at("min").get<double>()); EXPECT_LE(p.at("fitted").get<double>(),p.at("max").get<double>());}
    EXPECT_EQ(r.at("weighting"),"sigma_Ohm"); EXPECT_EQ(r.at("stop_status"),"max_iterations");
    for (const auto &f : r.at("frequency_Hz")) {EXPECT_GE(f.get<double>(),1.); EXPECT_LE(f.get<double>(),10000.);}
    for (const auto &v : r.at("scale_Ohm")) EXPECT_EQ(v,2.);
    EXPECT_EQ(r.at("inputs").at(0),RandlesMeasurement());
}
TEST(CapLabISFit, InvalidSettingsPointsNoFreeAndCancellationLeaveCircuitUntouched) {
    auto c=RandlesStart(), s=FitSettings(), m=RandlesMeasurement(); const auto before=c.dump();
    JobContext context({}, {}, {}, {}, [] {return false;});
    auto bad=s; for (auto &p : bad["parameters"]) p["free"]=false;
    EXPECT_THROW(FitIS(m,c,bad,context),std::exception);
    bad=s; bad["parameters"][0]["min"]=100.; EXPECT_THROW(FitIS(m,c,bad,context),std::exception);
    bad=s; bad["parameters"][2]["unit"]="Ohm"; EXPECT_THROW(FitIS(m,c,bad,context),std::exception);
    bad=s; bad["Z_floor_Ohm"]=0.; EXPECT_THROW(FitIS(m,c,bad,context),std::exception);
    bad=s; bad["sigma_Ohm"]={-1.}; EXPECT_THROW(FitIS(m,c,bad,context),std::exception);
    bad=s; bad["frequency_min_Hz"]=1e20; EXPECT_THROW(FitIS(m,c,bad,context),std::exception);
    m["columns"][1][0]=0.; m["columns"][2][0]=0.; EXPECT_THROW(FitIS(m,c,s,context),std::exception);
    auto d=ReadCircuit(c.at("schematic")); const int v=d.add(ccui::Kind::V,{});
    d.wires.push_back({d.elements[v].nodes[0],d.port[0]}); d.wires.push_back({d.elements[v].nodes[1],d.port[1]});
    auto invalid=c; invalid["schematic"]=CircuitJson(d); EXPECT_THROW(FitIS(RandlesMeasurement(),invalid,s,context),std::exception);
    bool cancel=false;
    JobContext cancelled([&](float done,float) {if (done>0) cancel=true;}, {}, {}, {}, [&] {return cancel;});
    EXPECT_THROW(FitIS(RandlesMeasurement(),c,s,cancelled),JobCancelledException);
    EXPECT_TRUE(cancel); EXPECT_EQ(c.dump(),before);
}
TEST(CapLabISFit, DraftRequiresAcceptanceUndoRedoStaleAndCancel) {
    auto jobs=CreateRef<JobSystem>(nullptr,1); auto bus=CreateRef<EventBus>(); auto undo=CreateRef<UndoStack>(); auto commands=CreateRef<CommandRegistry>();
    CapLabWorkflow w(jobs,bus,undo); RegisterCommands(commands,bus,w); w.document.project=FixtureProject();
    CommandContext edit; edit.Set("edit",Json{{"action","create"},{"sample_id",w.document.project.samples[0].id},{"name","R"},{"topology","R0"}});
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.circuit"},std::move(edit)));
    w.analysisOptions={{"type","Circuit"},{"fit",true},{"circuit_id",w.document.project.circuits[0]["id"]},
        {"measurement_id",w.document.project.sweeps[0].id},{"bias_V",0},{"Z_floor_Ohm",1.},{"max_iterations",1},
        {"parameters",Json::array({{{"element","R0"},{"field","value"},{"unit","ohm"},{"free",true},{"min",1.},{"max",1e9}}})}};
    const auto before=Serialize(w.document.project);
    w.Start(Operation::Analyze); jobs->Shutdown(); w.OnUpdate(0);
    ASSERT_TRUE(w.analysisDraft) << w.status; EXPECT_EQ(Serialize(w.document.project),before);
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.accept_analysis"})) << w.status;
    const auto accepted=Serialize(w.document.project); EXPECT_NE(accepted,before);
    EXPECT_EQ(w.document.project.analyses.back()["status"],"valid");
    ASSERT_TRUE(undo->Undo()); EXPECT_EQ(Serialize(w.document.project),before);
    ASSERT_TRUE(undo->Redo()); EXPECT_EQ(Serialize(w.document.project),accepted);
    EXPECT_EQ(Serialize(Parse(accepted)),accepted);
    for (const auto &key : {"score","Z_floor_Ohm","covariance","stop_status"}) {
        auto broken=Json::parse(accepted); broken["analyses"].back()[key]=nullptr;
        if (std::string(key)!="covariance") EXPECT_THROW(Parse(broken.dump()),std::exception);
        else {broken["analyses"].back()[key]=0.; EXPECT_THROW(Parse(broken.dump()),std::exception);}
    }
    w.jobs=CreateRef<JobSystem>(nullptr,1); w.Start(Operation::Analyze); w.jobs->Shutdown(); w.OnUpdate(0);
    ASSERT_TRUE(w.analysisDraft); w.Changed();
    EXPECT_FALSE(commands->Execute(CommandID{"caplab.accept_analysis"})); EXPECT_EQ(Serialize(w.document.project),accepted);
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.cancel"})); EXPECT_FALSE(w.analysisDraft);
    w.jobs=CreateRef<JobSystem>(nullptr,1); ASSERT_TRUE(w.jobs->PauseAllJobs()); w.Start(Operation::Analyze);
    ASSERT_TRUE(commands->Execute(CommandID{"caplab.cancel"}));
    ASSERT_TRUE(w.jobs->ResumeAllJobs()); w.jobs->Shutdown(); w.OnUpdate(0);
    EXPECT_EQ(Serialize(w.document.project),accepted); EXPECT_FALSE(w.analysisDraft); EXPECT_EQ(w.status,"Cancelled.");
}
}
namespace SST::Prototypes::CapLab {
TEST(CapLabISFit, InvalidTrialPointIsPenalizedWithoutPublishingIt) {
    JobContext context({}, {}, {}, {}, [] {return false;});
    auto m=Measurement({100,1000}); m["columns"][1]={-20.,-20.};
    auto c=Circuit("R0");
    Json settings{{"Z_floor_Ohm",1.},{"max_iterations",75},{"score_tolerance",0.},
        {"parameters",Json::array({{{"element","R0"},{"field","value"},{"unit","ohm"},{"free",true},{"min",-100.},{"max",200.}}})}};
    const auto r=FitIS(m,c,settings,context);
    EXPECT_EQ(r.at("status"),"valid"); EXPECT_GT(r.at("invalid_evaluations").get<int>(),0);
    EXPECT_GT(r["circuit"]["schematic"]["elements"][0]["value"].get<double>(),0.);
}
TEST(CapLabISFit, SpiceJunctionCapacitanceIsFitParameterForBothDiodes) {
    JobContext context({}, {}, {}, {}, [] {return false;});
    for (const auto kind : {ccui::Kind::PN,ccui::Kind::Schottky}) {
        ccui::CircuitDocument d; d.add(kind,{},"junction"); d.ground=d.elements[0].nodes[1]; d.port=d.elements[0].nodes;
        d.elements[0].value=1e-12; d.elements[0].secondary=1; d.elements[0].cj0=0.;
        Json c{{"id",3u},{"sample_id",1u},{"name","diode"},{"schematic",CircuitJson(d)}};
        auto m=Measurement({100,1000}); m["bias_V"]=0.;
        for (std::size_t i=0;i<2;++i) {
            // SPICE at zero bias: Cj=Cj0; g=Is/(kB*T/q), SI defining constants.
            const double f=m["columns"][0][i];
            const auto z=1./std::complex<double>(1e-12/(1.380649e-23*270/1.602176634e-19),2*std::acos(-1.)*f*2e-9);
            m["columns"][1][i]=z.real(); m["columns"][2][i]=z.imag();
        }
        Json s{{"Z_floor_Ohm",1.},{"parameters",Json::array({{{"element","junction"},{"field","Cj0"},{"unit","F"},{"free",true},{"min",0.},{"max",1e-8}}})}};
        const auto r=FitIS(m,c,s,context);
        EXPECT_NEAR(r["parameters"][0]["fitted"].get<double>()/2e-9,1.,1e-3);
        EXPECT_EQ(r["parameters"][0]["unit"],"F");
    }
}
}

namespace SST::Prototypes::CapLab {
TEST(CapLabISFit, JacobianCovarianceMatchesIndependentResistorEstimate) {
    JobContext context({}, {}, {}, {}, [] {return false;});
    auto m=Measurement({100,1000,10000}); m["columns"][1]={90.,100.,110.}; m["columns"][2]={0.,0.,0.};
    auto c=Circuit("R0"); c["schematic"]["elements"][0]["value"]=80.;
    Json settings{{"Z_floor_Ohm",1.},{"sigma_Ohm",{2.,2.,2.}},{"max_iterations",300},{"score_tolerance",0.},
        {"parameters",Json::array({{{"element","R0"},{"field","value"},{"unit","ohm"},{"free",true},{"min",1.},{"max",200.}}})}};
    const auto r=FitIS(m,c,settings,context);
    ASSERT_EQ(r.at("covariance_status"),"estimated_from_jacobian");
    EXPECT_NEAR(r["parameters"][0]["fitted"].get<double>(),100.,1e-4);
    // Six complex components, one parameter: s2=(10^2+10^2)/4/5; inv(JtJ)=4/3.
    EXPECT_NEAR(r["covariance"][0][0].get<double>(),40./3,1e-5);
    EXPECT_NEAR(r["parameters"][0]["sigma"].get<double>(),std::sqrt(40./3),1e-5);
    c=Circuit("R0-R1");
    for (auto& e : c["schematic"]["elements"]) e["value"]=50.;
    settings["parameters"].push_back(settings["parameters"][0]);
    settings["parameters"][1]["element"]="R1";
    const auto singular=FitIS(m,c,settings,context);
    EXPECT_TRUE(singular.at("covariance").is_null());
    EXPECT_EQ(singular.at("covariance_status"),"rank_deficient_or_invalid_jacobian");
}
}
