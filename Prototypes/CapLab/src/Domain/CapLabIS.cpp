#include "Domain/CapLabIS.hpp"
#include "AnalysisRunner.h"
#include <sst/physics/impedance.hpp>
#include <complex>

namespace SST::Prototypes::CapLab {
void EditCircuit(Project &p, const Json &edit) {
    const std::string action=edit.at("action");
    if (action=="create") {
        const auto schematic=edit.contains("topology") ? CircuitJson(ccui::importString(edit.at("topology"))) : CircuitJson(ReadCircuit(edit.at("schematic")));
        p.circuits.push_back({{"id",p.engine->reserveId()},{"sample_id",edit.at("sample_id")},
            {"name",edit.at("name")},{"schematic",schematic}});
    } else {
        const auto it=std::find_if(p.circuits.begin(),p.circuits.end(),[&](const auto &c) {return c.at("id")==edit.at("circuit_id");});
        if (it==p.circuits.end()) throw std::runtime_error("Missing circuit");
        if (action=="delete") p.circuits.erase(it);
        else if (action=="replace") {
            (*it)["schematic"]=CircuitJson(ReadCircuit(edit.at("schematic")));
            if (edit.contains("name")) (*it)["name"]=edit.at("name");
        } else throw std::runtime_error("Unknown circuit edit");
    }
    Validate(p);
}
namespace {
const char *Code(circuitcore::DiagnosticCode code) {
    using D=circuitcore::DiagnosticCode;
    switch (code) {
    case D::FloatingNode: return "floating_node";
    case D::DisconnectedSubnetwork: return "disconnected_subnetwork";
    case D::NoPathToGround: return "no_path_to_ground";
    case D::ConflictingVoltageSources: return "conflicting_voltage_sources";
    case D::SingularSystemMatrix: return "singular_matrix";
    case D::IllConditionedSystemMatrix: return "ill_conditioned_matrix";
    case D::NonFiniteValueEncountered: return "nonfinite_value";
    case D::NewtonNonConvergence: return "newton_nonconvergence";
    case D::InvalidParameterValue: return "invalid_parameter";
    case D::InvalidTemperature: return "invalid_temperature";
    case D::InvalidDeviceTopology: return "invalid_topology";
    case D::MinimumTimeStepExceeded: return "minimum_time_step";
    case D::UnknownDeviceType: return "unknown_device";
    default: return "circuit_error";
    }
}
double Condition(const Json &m, const Json &s, const char *key, bool positive) {
    const auto value=m.contains(key) && !m.at(key).is_null() ? m.at(key) : s.at(key);
    const double v=value.get<double>();
    if (!std::isfinite(v) || (positive && v<=0)) throw std::runtime_error(std::string("Invalid/missing ")+key);
    return v;
}
}
Json SimulateIS(const Json &m, const Json &c, const Json &settings, JobContext &context) {
    using namespace circuitcore;
    using namespace sst::physics;
    if (m.at("kind")!="FrequencySweep" || m.at("sample_id")!=c.at("sample_id")) throw std::runtime_error("Circuit requires a same-sample FrequencySweep");
    const double temperature=Condition(m,settings,"temperature_K",true), bias=Condition(m,settings,"bias_V",false);
    auto column=[&](const char *name,const char *unit) {
        const auto &names=m.at("names");
        const auto it=std::find(names.begin(),names.end(),Json(name));
        if (it==names.end()) throw std::runtime_error(std::string("Missing IS column ")+name);
        const auto i=std::distance(names.begin(),it);
        if (m.at("units").at(i)!=unit) throw std::runtime_error("Invalid IS SI units");
        return m.at("columns").at(i).get<std::vector<double>>();
    };
    const auto f=column("f","Hz");
    if (f.empty() || f.size()>1201 || f.size()!=m.at("row_ids").size()) throw std::runtime_error("IS requires 1..1201 measured frequencies");
    std::set<double> unique;
    for (double v : f) if (!std::isfinite(v) || v<=0 || !unique.insert(v).second) throw std::runtime_error("Invalid/duplicate IS frequency");
    const std::string variant=m.at("variant");
    std::vector<double> a,b;
    if (variant=="ReZ_ImZ") {a=column("ReZ","Ohm"); b=column("ImZ","Ohm");}
    else if (variant=="C_G" || variant=="Cp_Rp") {a=column("C","F"); b=variant=="C_G" ? column("G","S") : column("Rp","Ohm");}
    else throw std::runtime_error("Unsupported IS variant");
    if (a.size()!=f.size() || b.size()!=f.size()) throw std::runtime_error("Unequal IS columns");
    Json out{{"type","Circuit"},{"model","CircuitCore-AC"},{"kernel_version",1},{"sample_id",c.at("sample_id")},
        {"circuit",c},{"inputs",Json::array({m})},{"settings",settings},{"temperature_K",temperature},{"bias_V",bias},
        {"frequency_Hz",f},{"row_ids",m.at("row_ids")},{"diagnostics",Json::array()},
        {"status","valid"},{"computed_status","valid"},{"plots",Json::array()},
        {"source","CircuitCore-UI compileDocument; CircuitCore AC linearization; PFS pp.94-96 Y=G+j*omega*C"}};
    for (const char *key : {"measured_ReZ_Ohm","measured_ImZ_Ohm","model_ReZ_Ohm","model_ImZ_Ohm",
        "residual_ReZ_Ohm","residual_ImZ_Ohm"}) out[key]=Json::array();
    auto diagnostic=[&](const char *code,const std::string &message,Json row=nullptr) {
        out["diagnostics"].push_back({{"code",code},{"message",message},{"row_id",row},{"severity","error"}});
        out["status"]=out["computed_status"]="invalid";
    };
    // The registry must outlive the compiled circuit. The virtual source applies
    // the measured bias and supplies a unit AC voltage; it is never persisted.
    auto registry=DeviceRegistry::createWithBuiltinDevices();
    auto built=ccui::compileDocument(ReadCircuit(c.at("schematic")),true);
    DiagnosticSink sink;
    auto compiled=Simulator::compile(built.definition,registry,sink);
    for (const auto &d : sink.entries()) {
        out["diagnostics"].push_back({{"code",Code(d.code)},{"message",d.message},{"row_id",nullptr},
            {"severity",d.severity==DiagnosticSeverity::Error ? "error" : "warning"}});
    }
    if (sink.hasErrors()) out["status"]=out["computed_status"]="invalid";
    auto parameters=compiled.defaultParameters();
    parameters.set(built.portSource,SourceParameters::DcValue,bias);
    parameters.set(built.portSource,SourceParameters::AcMagnitude,1.);
    auto state=compiled.createInitialState(parameters);
    SimulationEnvironment env; env.temperatureK=temperature;
    AcSweepOptions options; options.observations.observeDeviceCurrent(built.portSource);
    for (std::size_t i=0;i<f.size();++i) {
        context.ThrowIfCancellationRequested();
        const auto row=m.at("row_ids").at(i);
        std::optional<std::complex<double>> measured,model;
        try {
            measured=variant=="ReZ_ImZ" ? std::complex<double>{a[i],b[i]} :
                reciprocalImmittance(parallelAdmittance(f[i],a[i],variant=="C_G" ? b[i] : parallelConductance(b[i])));
            (void)reciprocalImmittance(*measured);
        } catch (const std::exception &e) { measured.reset(); diagnostic("invalid_measurement",e.what(),row); }
        options.frequenciesHz={f[i]};
        AcSweepResult result;
        if (!sink.hasErrors()) result=Simulator::acSweep(compiled,parameters,env,state,options);
        for (const auto &d : result.diagnostics) {
            out["diagnostics"].push_back({{"code",Code(d.code)},{"message",d.message},{"row_id",row},
                {"severity",d.severity==DiagnosticSeverity::Error ? "error" : "warning"}});
        }
        if (!result.points.empty() && isUsableResult(result.points[0].status) && !result.points[0].deviceCurrents.empty()) {
            try { model=reciprocalImmittance(-result.points[0].deviceCurrents[0]); }
            catch (const std::exception &e) { diagnostic("invalid_model",e.what(),row); }
        }
        if (!model) diagnostic("unusable_point","CircuitCore failed AC at measured frequency",row);
        auto append=[&](const char *key,std::optional<double> v) {out[key].push_back(v ? Json(*v) : Json(nullptr));};
        append("measured_ReZ_Ohm",measured ? std::optional(measured->real()) : std::nullopt);
        append("measured_ImZ_Ohm",measured ? std::optional(measured->imag()) : std::nullopt);
        append("model_ReZ_Ohm",model ? std::optional(model->real()) : std::nullopt);
        append("model_ImZ_Ohm",model ? std::optional(model->imag()) : std::nullopt);
        append("residual_ReZ_Ohm",measured && model ? std::optional(model->real()-measured->real()) : std::nullopt);
        append("residual_ImZ_Ohm",measured && model ? std::optional(model->imag()-measured->imag()) : std::nullopt);
        context.SetProgress(float(i+1)/f.size(),1);
    }
    for (const auto &kind : {"Nyquist","Bode magnitude","Bode phase","C-f","G/omega-f","ReZ residual","ImZ residual"}) {
        const std::string k=kind;
        Json plot{{"name",k},{"x_label",k=="Nyquist" ? "Re Z [Ohm]" : "f [Hz]"},
            {"y_label",k=="Nyquist" ? "-Im Z [Ohm]" : k=="Bode magnitude" ? "|Z| [Ohm]" : k=="Bode phase" ? "phase [deg]" :
                k=="C-f" ? "C [F]" : k=="G/omega-f" ? "G/omega [F]" : "model - measurement [Ohm]"},
            {"log_x",k!="Nyquist"},{"equal_aspect",k=="Nyquist"},{"curves",Json::array()}};
        const bool residual=k=="ReZ residual" || k=="ImZ residual";
        if (residual) plot["curves"].push_back({{"name","residual"},{"x",f},{"y",out.at(k=="ReZ residual" ? "residual_ReZ_Ohm" : "residual_ImZ_Ohm")}});
        else for (const std::string prefix : {"measured","model"}) {
            Json x=Json::array(),y=Json::array();
            for (std::size_t i=0;i<f.size();++i) {
                const auto &re=out.at(prefix+"_ReZ_Ohm").at(i), &im=out.at(prefix+"_ImZ_Ohm").at(i);
                x.push_back(k=="Nyquist" ? re : Json(f[i]));
                if (re.is_null() || im.is_null()) {y.push_back(nullptr); continue;}
                const std::complex<double> z{re.get<double>(),im.get<double>()};
                const double omega=2*std::acos(-1.)*f[i];
                const auto admittance=reciprocalImmittance(z);
                y.push_back(k=="Nyquist" ? -z.imag() : k=="Bode magnitude" ? std::abs(z) :
                    k=="Bode phase" ? std::arg(z)*180/std::acos(-1.) : k=="C-f" ? admittance.imag()/omega : admittance.real()/omega);
            }
            plot["curves"].push_back({{"name",prefix},{"x",x},{"y",y},{"fit",prefix=="model"}});
        }
        out["plots"].push_back(std::move(plot));
    }
    context.ThrowIfCancellationRequested(); return out;
}
}
