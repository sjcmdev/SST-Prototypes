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
