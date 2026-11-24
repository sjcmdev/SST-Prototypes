#include "Domain/CapLabIS.hpp"
#include "Domain/PreparedISModel.hpp"
#include "AnalysisRunner.h"
#include <sst/physics/impedance.hpp>
#include <complex>
#include <sst/math/nelder_mead.hpp>

namespace SST::Prototypes::CapLab {
void EditCircuit(Project &p, const Json &edit) {
    const std::string action=edit.at("action");
    if (action=="create") {
        const auto schematic=edit.contains("topology") ? CircuitJson(ccui::importString(edit.at("topology"))) : CircuitJson(ReadCircuit(edit.at("schematic")));
        pl::Id sample;
        if (edit.contains("sample_id")) sample=edit.at("sample_id");
        else {
            const std::string name=edit.at("sample_name");
            if (name.find_first_not_of(" \t\r\n")==std::string::npos) throw std::runtime_error("Sample name is required");
            sample=p.engine->reserveId();
            auto change=p.engine->change("Create circuit sample",{{sample,p.engine->root(),name,0,pl::Workbook{}}});
            change.apply(*p.engine);
            p.samples.push_back({sample,name,std::nullopt});
        }
        p.circuits.push_back({{"id",p.engine->reserveId()},{"sample_id",sample},
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
double Condition(const AnalysisMeasurementData &m, const Json &s, const char *key, bool positive) {
    const auto value=m.metadata.contains(key) && !m.metadata.at(key).is_null() ? m.metadata.at(key) : s.at(key);
    const double v=value.get<double>();
    if (!std::isfinite(v) || (positive && v<=0)) throw std::runtime_error(std::string("Invalid/missing ")+key);
    return v;
}
const std::vector<double>& ISColumn(const AnalysisMeasurementData &m, const char *name, const char *unit) {
    const auto &names=m.metadata.at("names");
    const auto it=std::find(names.begin(),names.end(),Json(name));
    if (it==names.end()) throw std::runtime_error(std::string("Missing IS column ")+name);
    const auto i=std::distance(names.begin(),it);
    if (m.metadata.at("units").at(i)!=unit) throw std::runtime_error("Invalid IS SI units");
    return *m.columns.at(i);
}
std::vector<double> ModelFrequencies(const Json &settings) {
    if (settings.contains("frequency_Hz")==settings.contains("frequency_sweep"))
        throw std::runtime_error("Specify frequency_Hz or frequency_sweep, exclusively");
    if (settings.contains("frequency_Hz")) {
        const auto &f=settings.at("frequency_Hz");
        if (!f.is_array() || f.empty() || f.size()>1201) throw std::runtime_error("IS requires 1..1201 frequencies");
        return f.get<std::vector<double>>();
    }
    const auto &s=settings.at("frequency_sweep");
    const double start=s.at("start_Hz"), stop=s.at("stop_Hz"), density=s.at("points_per_decade");
    if (!std::isfinite(start) || !std::isfinite(stop) || !std::isfinite(density) ||
        start<=0 || stop<start || density<1 || density!=std::floor(density))
        throw std::runtime_error("Invalid logarithmic frequency sweep");
    const double intervals=(std::log10(stop)-std::log10(start))*density;
    // Keep exact decade endpoints from gaining a point through floating-point roundoff.
    const double count=std::abs(intervals-std::round(intervals))<1e-10 ? std::round(intervals) : std::ceil(intervals);
    if (!std::isfinite(count) || count>1200) throw std::runtime_error("IS requires 1..1201 frequencies");
    std::vector<double> f{start};
    for (int i=1;i<count;++i) f.push_back(std::pow(10.,std::log10(start)+i/density));
    if (stop>start) f.push_back(stop);
    return f;
}
AnalysisCurveData ISCurve(const std::string &kind, const std::string &name, const std::vector<double> &f,
    const Json &real, const Json &imaginary, bool model) {
    AnalysisCurveData curve;curve.name=name;curve.fit=model;
    const double nan=std::numeric_limits<double>::quiet_NaN();
    for (std::size_t i=0;i<f.size();++i) {
        const auto &re=real.at(i), &im=imaginary.at(i);
        curve.x.push_back(kind=="Nyquist" ? (re.is_null()?nan:re.get<double>()) : f[i]);
        if (re.is_null() || im.empty()) {curve.y.push_back(nan); continue;}
        const std::complex<double> z{re.get<double>(),im.get<double>()};
        const double omega=2*std::acos(-1.)*f[i];
        const auto admittance=sst::physics::reciprocalImmittance(z);
        curve.y.push_back(kind=="Nyquist" ? -z.imag() : kind=="Bode magnitude" ? std::abs(z) :
            kind=="Bode phase" ? std::arg(z)*180/std::acos(-1.) : kind=="C-f" ? admittance.imag()/omega : admittance.real()/omega);
    }
    return curve;
}
}
static AnalysisResult SimulateISResult(const AnalysisMeasurementData &m, const Json &c, const Json &settings, JobContext &context, bool plots, const PreparedISModel *prepared) {
    using namespace circuitcore;
    using namespace sst::physics;
    const bool measuredGrid=!m.empty();
    if (measuredGrid && (m.metadata.at("kind")!="FrequencySweep" || m.metadata.at("sample_id")!=c.at("sample_id"))) throw std::runtime_error("Circuit requires a same-sample FrequencySweep");
    if (measuredGrid && (settings.contains("frequency_Hz") || settings.contains("frequency_sweep")))
        throw std::runtime_error("Measured grid cannot be combined with an explicit grid");
    const double temperature=Condition(m,settings,"temperature_K",true), bias=Condition(m,settings,"bias_V",false);
    auto column=[&](const char *name,const char *unit) {return ISColumn(m,name,unit);};
    const auto f=measuredGrid ? column("f","Hz") : ModelFrequencies(settings);
    if (f.empty() || f.size()>1201 || (measuredGrid && f.size()!=m.rowIds->size())) throw std::runtime_error("IS requires 1..1201 frequencies");
    std::set<double> unique;
    for (double v : f) if (!std::isfinite(v) || v<=0 || !unique.insert(v).second) throw std::runtime_error("Invalid/duplicate IS frequency");
    const std::string variant=measuredGrid ? m.metadata.at("variant").get<std::string>() : "";
    std::vector<double> a,b;
    if (variant=="ReZ_ImZ") {a=column("ReZ","Ohm"); b=column("ImZ","Ohm");}
    else if (variant=="C_G" || variant=="Cp_Rp") {a=column("C","F"); b=variant=="C_G" ? column("G","S") : column("Rp","Ohm");}
    else if (measuredGrid) throw std::runtime_error("Unsupported IS variant");
    if (measuredGrid && (a.size()!=f.size() || b.size()!=f.size())) throw std::runtime_error("Unequal IS columns");
    AnalysisResult output;
    if(measuredGrid)output.inputs.push_back(m);
    Json &out=output.record;out=Json{{"type","Circuit"},{"model","CircuitCore-AC"},{"kernel_version",1},{"sample_id",c.at("sample_id")},
        {"circuit",c},{"settings",settings},{"temperature_K",temperature},{"bias_V",bias},
        {"frequency_Hz",f},{"grid_source",measuredGrid ? "measurement" : "model"},{"row_ids",measuredGrid ? Json(*m.rowIds) : Json::array()},{"diagnostics",Json::array()},
        {"status","valid"},{"computed_status","valid"},
        {"source","CircuitCore-UI compileDocument; CircuitCore AC linearization; PFS pp.94-96 Y=G+j*omega*C"}};
    for (const char *key : {"measured_ReZ_Ohm","measured_ImZ_Ohm","model_ReZ_Ohm","model_ImZ_Ohm",
        "residual_ReZ_Ohm","residual_ImZ_Ohm"}) out[key]=Json::array();
    auto diagnostic=[&](const char *code,const std::string &message,Json row=nullptr) {
        out["diagnostics"].push_back({{"code",code},{"message",message},{"row_id",row},{"severity","error"}});
        out["status"]=out["computed_status"]="invalid";
    };
    // The registry must outlive the compiled circuit. The virtual source applies
    // the measured bias and supplies a unit AC voltage; it is never persisted.
    if (!prepared) {
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
        const Json row=measuredGrid ? Json(m.rowIds->at(i)) : Json(nullptr);
        std::optional<std::complex<double>> measured,model;
        if (measuredGrid) try {
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
        if (!model) diagnostic("unusable_point","CircuitCore failed AC at requested frequency",row);
        auto append=[&](const char *key,std::optional<double> v) {out[key].push_back(v ? Json(*v) : Json(nullptr));};
        if (measuredGrid) {
            append("measured_ReZ_Ohm",measured ? std::optional(measured->real()) : std::nullopt);
            append("measured_ImZ_Ohm",measured ? std::optional(measured->imag()) : std::nullopt);
        }
        append("model_ReZ_Ohm",model ? std::optional(model->real()) : std::nullopt);
        append("model_ImZ_Ohm",model ? std::optional(model->imag()) : std::nullopt);
        if (measuredGrid) {
            append("residual_ReZ_Ohm",measured && model ? std::optional(model->real()-measured->real()) : std::nullopt);
            append("residual_ImZ_Ohm",measured && model ? std::optional(model->imag()-measured->imag()) : std::nullopt);
        }
        context.SetProgress(float(i+1)/f.size(),1);
    }
    } else {
        for (const auto &entry : prepared->diagnostics()) {
            const auto &d=entry.value;
            out["diagnostics"].push_back({{"code",Code(d.code)},{"message",d.message},
                {"row_id",entry.row ? Json(m.rowIds->at(*entry.row)) : Json(nullptr)},
                {"severity",d.severity==DiagnosticSeverity::Error ? "error" : "warning"}});
        }
        for (std::size_t i=0;i<f.size();++i) {
            const auto measured=prepared->measured()[i], model=prepared->model()[i], residual=prepared->residuals()[i];
            out["measured_ReZ_Ohm"].push_back(measured.real()); out["measured_ImZ_Ohm"].push_back(measured.imag());
            out["model_ReZ_Ohm"].push_back(model.real()); out["model_ImZ_Ohm"].push_back(model.imag());
            out["residual_ReZ_Ohm"].push_back(residual.real()); out["residual_ImZ_Ohm"].push_back(residual.imag());
        }
    }
    if (plots) for (const auto &kind : {"Nyquist","Bode magnitude","Bode phase","C-f","G/omega-f","ReZ residual","ImZ residual"}) {
        const std::string k=kind;
        if (!measuredGrid && k.ends_with("residual")) continue;
        AnalysisPlotData plot;plot.name=k;plot.xLabel=k=="Nyquist" ? "Re Z (Ohm)" : "Frequency (Hz)";
        plot.yLabel=k=="Nyquist" ? "-Im Z (Ohm)" : k=="Bode magnitude" ? "|Z| (Ohm)" : k=="Bode phase" ? "Phase (deg)" :
            k=="C-f" ? "Capacitance (F)" : k=="G/omega-f" ? "G/omega (F)" : "model - measurement (Ohm)";
        plot.logX=k!="Nyquist";plot.equalAspect=k=="Nyquist";
        const bool residual=k=="ReZ residual" || k=="ImZ residual";
        if (residual && measuredGrid) {
            AnalysisCurveData curve;curve.name="residual";curve.x=f;
            for(const auto &v:out.at(k=="ReZ residual" ? "residual_ReZ_Ohm" : "residual_ImZ_Ohm"))curve.y.push_back(v.is_null()?std::numeric_limits<double>::quiet_NaN():v.get<double>());
            plot.curves.push_back(std::move(curve));
        } else if (!residual) for (const std::string prefix : {"measured","model"}) {
            if (prefix=="measured" && !measuredGrid) continue;
            plot.curves.push_back(ISCurve(k,prefix,f,out.at(prefix+"_ReZ_Ohm"),out.at(prefix+"_ImZ_Ohm"),prefix=="model"));
        }
        output.plots.push_back(std::move(plot));
    }
    context.ThrowIfCancellationRequested(); return output;
}
AnalysisResult SimulateIS(const AnalysisMeasurementData &m, const Json &c, const Json &settings, JobContext &context, bool plots) {
    return SimulateISResult(m,c,settings,context,plots,nullptr);
}
void OverlayISMeasurement(AnalysisResult &result, const AnalysisMeasurementData &m) {
    auto &out=result.record;
    if (m.metadata.at("sample_id")!=out.at("sample_id")) throw std::runtime_error("Overlay requires the same sample");
    const auto f=ISColumn(m,"f","Hz");
    const std::string variant=m.metadata.at("variant");
    const auto a=ISColumn(m,variant=="ReZ_ImZ" ? "ReZ" : "C",variant=="ReZ_ImZ" ? "Ohm" : "F");
    const auto b=ISColumn(m,variant=="ReZ_ImZ" ? "ImZ" : variant=="C_G" ? "G" : "Rp",variant=="C_G" ? "S" : "Ohm");
    if (a.size()!=f.size() || b.size()!=f.size()) throw std::runtime_error("Unequal overlay columns");
    Json re=Json::array(),im=Json::array();
    for (std::size_t i=0;i<f.size();++i) {
        try {
            if (!std::isfinite(f[i]) || f[i]<=0) throw std::runtime_error("Invalid overlay frequency");
            const auto z=variant=="ReZ_ImZ" ? std::complex<double>{a[i],b[i]} :
                sst::physics::reciprocalImmittance(sst::physics::parallelAdmittance(f[i],a[i],variant=="C_G" ? b[i] : sst::physics::parallelConductance(b[i])));
            (void)sst::physics::reciprocalImmittance(z);
            re.push_back(z.real()); im.push_back(z.imag());
        } catch (const std::exception &e) {
            re.push_back(nullptr); im.push_back(nullptr);
            out["diagnostics"].push_back({{"code","invalid_overlay"},{"message",e.what()},{"row_id",Json(m.rowIds->at(i))},{"measurement_id",m.metadata.at("id")},{"severity","warning"}});
        }
    }
    for (std::size_t i=0;i<5;++i) {
        auto curve=ISCurve(result.plots[i].name,"measured / "+m.metadata.at("id").dump()+" / T="+m.metadata.value("temperature_K",Json(nullptr)).dump()+" K / bias="+m.metadata.value("bias_V",Json(nullptr)).dump()+" V",f,re,im,false);
        curve.temperature_K=Optional(m.metadata.value("temperature_K",Json(nullptr)));
        result.plots[i].curves.push_back(std::move(curve));
    }
    result.inputs.push_back(m);
}
}
namespace SST::Prototypes::CapLab {
AnalysisResult FitIS(const AnalysisMeasurementData &measurement, const Json &circuit, const Json &settings, JobContext &context, const ISEvaluationObserver &observer) {
    context.ThrowIfCancellationRequested();
    auto m=measurement;
    const double floor=settings.at("Z_floor_Ohm");
    const int limit=settings.value("max_iterations",500);
    const double tolerance=settings.value("score_tolerance",1e-12);
    if (!std::isfinite(floor) || floor<=0 || limit<1 || limit>10000 || !std::isfinite(tolerance) || tolerance<0)
        throw std::runtime_error("Invalid floor, iteration limit or score tolerance");
    const double lower=settings.value("frequency_min_Hz",0.), upper=settings.value("frequency_max_Hz",std::numeric_limits<double>::max());
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower<0 || upper<lower) throw std::runtime_error("Invalid frequency range");
    const auto names=m.metadata.at("names").get<std::vector<std::string>>();
    const auto fi=std::find(names.begin(),names.end(),"f");
    if (fi==names.end()) throw std::runtime_error("Missing frequency column");
    const auto &frequencies=*m.columns.at(std::distance(names.begin(),fi));
    std::set<double> unique;
    for (double f:frequencies) if (!std::isfinite(f) || f<=0 || !unique.insert(f).second) throw std::runtime_error("Invalid/duplicate IS frequency");
    std::vector<double> sigma;
    if (settings.contains("sigma_Ohm")) {
        sigma=settings.at("sigma_Ohm").get<std::vector<double>>();
        if (sigma.size()!=frequencies.size()) throw std::runtime_error("sigma_Ohm must match measurement rows");
        for (double s : sigma) if (!std::isfinite(s) || s<=0) throw std::runtime_error("Invalid sigma_Ohm");
    }
    std::vector<double> selectedSigma;
    std::vector<std::vector<double>> columns(names.size());std::vector<pl::Id> selectedRows;
    for(std::size_t i=0;i<frequencies.size();++i)if(frequencies[i]>=lower && frequencies[i]<=upper) {
        for(std::size_t j=0;j<columns.size();++j)columns[j].push_back(measurement.columns[j]->at(i));
        selectedRows.push_back(measurement.rowIds->at(i));if(!sigma.empty())selectedSigma.push_back(sigma[i]);
    }
    m.columns.clear();for(auto &c:columns)m.columns.push_back(std::make_shared<const std::vector<double>>(std::move(c)));
    m.rowIds=std::make_shared<const std::vector<pl::Id>>(std::move(selectedRows));
    const auto schematic=circuit.at("schematic");
    const auto document=ReadCircuit(schematic);
    for (const auto &e : document.elements) if (e.kind==ccui::Kind::V || e.kind==ccui::Kind::I)
        throw std::runtime_error("IS fit supports R/C/L/CPE/W and SPICE PN/Schottky; independent sources are unsupported");
    std::vector<sst::math::FitParam> parameters;
    std::vector<std::size_t> elements;
    std::vector<std::string> fields;
    std::vector<double> scales;
    std::set<std::pair<std::string,std::string>> seen;
    bool hasFree=false;
    auto specs=settings.at("parameters");
    for (auto &spec : specs) {
        const std::string name=spec.at("element"), field=spec.at("field"), unit=spec.at("unit");
        const auto it=std::find_if(document.elements.begin(),document.elements.end(),[&](const auto &e) {return e.name==name;});
        if (it==document.elements.end() || !seen.emplace(name,field).second) throw std::runtime_error("Missing/duplicate parameter");
        const auto index=std::distance(document.elements.begin(),it);
        const bool diode=it->kind==ccui::Kind::PN || it->kind==ccui::Kind::Schottky;
        std::string expected;
        if (field=="value") expected=ccui::unit(it->kind);
        else if (field=="secondary" && (diode || it->kind==ccui::Kind::CPE)) expected="1";
        else if (diode && schematic.at("parameter_units").contains(field)) expected=schematic.at("parameter_units").at(field);
        else throw std::runtime_error("Unsupported circuit parameter field");
        if (unit!=expected) throw std::runtime_error("Invalid fit parameter unit");
        const double start=schematic.at("elements").at(index).at(field), lo=spec.at("min"), hi=spec.at("max");
        const bool free=spec.at("free");
        if (!std::isfinite(start) || !std::isfinite(lo) || !std::isfinite(hi) || lo>start || hi<start || lo>hi || (free && lo==hi))
            throw std::runtime_error("Invalid bounds/start");
        // Scale F/Ohm separately and shift zero starts so the initial simplex has a useful step.
        const double scale=start!=0 ? std::abs(start) : std::max(std::abs(lo),std::abs(hi));
        scales.push_back(scale>0 ? scale : 1.);
        const double offset=start==0 ? 1. : 0.;
        parameters.emplace_back(name+"."+field,start/scales.back()+offset,lo/scales.back()+offset,hi/scales.back()+offset,free);
        elements.push_back(index); fields.push_back(field); hasFree |= free;
        spec["start"]=start;
    }
    if (!hasFree) throw std::runtime_error("No free parameters");
    auto candidate=[&](const std::vector<double> &values) {
        auto c=circuit;
        for (std::size_t i=0;i<values.size();++i) if (parameters[i].free) {
            const auto &spec=specs.at(i);
            const double value=(values[i]-(spec.at("start").get<double>()==0 ? 1. : 0.))*scales[i];
            c["schematic"]["elements"][elements[i]][fields[i]]=std::clamp(value,spec.at("min").get<double>(),spec.at("max").get<double>());
        }
        return c;
    };
    // SST-CapLab Spec 4.3: modulus-weighted complex residual sum, not statistical chi-squared.
    std::vector<double> weights;
    JobContext evaluation({}, {}, {}, {}, [&] {return context.IsCancellationRequested();});
    if (m.metadata.at("kind")!="FrequencySweep" || m.metadata.at("sample_id")!=circuit.at("sample_id")) throw std::runtime_error("Circuit requires a same-sample FrequencySweep");
    if (settings.contains("frequency_Hz") || settings.contains("frequency_sweep")) throw std::runtime_error("Measured grid cannot be combined with an explicit grid");
    const auto frequency=ISColumn(m,"f","Hz");
    if (frequency.empty() || frequency.size()>1201 || frequency.size()!=m.rowIds->size()) throw std::runtime_error("IS requires 1..1201 frequencies");
    const std::string variant=m.metadata.at("variant");
    if (variant!="ReZ_ImZ" && variant!="C_G" && variant!="Cp_Rp") throw std::runtime_error("Unsupported IS variant");
    const auto a=ISColumn(m,variant=="ReZ_ImZ" ? "ReZ" : "C",variant=="ReZ_ImZ" ? "Ohm" : "F");
    const auto b=ISColumn(m,variant=="ReZ_ImZ" ? "ImZ" : variant=="C_G" ? "G" : "Rp",variant=="C_G" ? "S" : "Ohm");
    if (a.size()!=frequency.size() || b.size()!=frequency.size()) throw std::runtime_error("Unequal IS columns");
    auto invalidInitial=[&] {
        const auto initial=SimulateIS(m,circuit,settings,evaluation,false);
        throw std::runtime_error("Invalid initial simulation/measurement: "+initial.record.at("diagnostics").dump());
    };
    std::vector<std::complex<double>> measured;
    for (std::size_t i=0;i<frequency.size();++i) {
        std::complex<double> z;
        try {
            z=variant=="ReZ_ImZ" ? std::complex<double>{a[i],b[i]} :
                sst::physics::reciprocalImmittance(sst::physics::parallelAdmittance(frequency[i],a[i],variant=="C_G" ? b[i] : sst::physics::parallelConductance(b[i])));
            (void)sst::physics::reciprocalImmittance(z);
        } catch (const std::exception &) {invalidInitial();}
        measured.push_back(z);
        weights.push_back(selectedSigma.empty() ? std::max(std::hypot(z.real(),z.imag()),floor) : selectedSigma[i]);
    }
    PreparedISModel prepared(document,frequency,std::move(measured),Condition(m,settings,"temperature_K",true),Condition(m,settings,"bias_V",false));
    std::vector<std::size_t> slots;
    std::vector<double> minimum,maximum,offsets;
    for (std::size_t i=0;i<specs.size();++i) {
        slots.push_back(prepared.bind(elements[i],fields[i]));
        minimum.push_back(specs[i].at("min")); maximum.push_back(specs[i].at("max"));
        offsets.push_back(specs[i].at("start").get<double>()==0 ? 1. : 0.);
    }
    bool evaluationValid=false;
    auto evaluate=[&] {
        evaluationValid=prepared.evaluate(evaluation);
        if (!evaluationValid) return std::numeric_limits<double>::infinity();
        double sum=0;
        const auto residuals=prepared.residuals();
        for (std::size_t i=0;i<weights.size();++i) {
            const double re=residuals[i].real()/weights[i], im=residuals[i].imag()/weights[i];
            sum+=re*re+im*im;
        }
        if (observer) observer(prepared.document(),residuals,sum);
        return sum;
    };
    const double initialScore=evaluate();
    if (!evaluationValid) invalidInitial();
    auto score=[&](const Json &r) {
        if (r.at("status")!="valid") return std::numeric_limits<double>::infinity();
        double sum=0;
        for (std::size_t i=0;i<weights.size();++i) {
            const double re=r.at("residual_ReZ_Ohm").at(i).get<double>()/weights[i], im=r.at("residual_ImZ_Ohm").at(i).get<double>()/weights[i];
            sum+=re*re+im*im;
        }
        return sum;
    };
    int invalid=0;
    sst::math::SANelderMead optimizer(parameters,[&](const auto &values) {
        context.ThrowIfCancellationRequested();
        try {
            for (std::size_t i=0;i<values.size();++i) if (parameters[i].free)
                prepared.set(slots[i],std::clamp((values[i]-offsets[i])*scales[i],minimum[i],maximum[i]));
            const double s=evaluate();
            if (!std::isfinite(s)) ++invalid;
            return s;
        } catch (const JobCancelledException &) {throw;}
        catch (const std::exception &) {++invalid; return std::numeric_limits<double>::infinity();}
    });
    context.SetStage("Fitting IS draft");
    optimizer.initSimplex();
    std::string stop="max_iterations";
    for (int i=0;i<limit;++i) {
        context.ThrowIfCancellationRequested();
        if (optimizer.bestChiSquared()<=tolerance) {stop="score_tolerance"; break;}
        optimizer.step(); context.SetProgress(float(i+1)/limit,1);
    }
    if (optimizer.bestChiSquared()<=tolerance) stop="score_tolerance";
    auto fitted=candidate(optimizer.bestFullParams());
    for (std::size_t i=0;i<specs.size();++i)
        prepared.set(slots[i],fitted.at("schematic").at("elements").at(elements[i]).at(fields[i]));
    if (!prepared.evaluate(evaluation,true)) throw std::runtime_error("No valid fitted simulation");
    auto result=SimulateISResult(m,fitted,settings,evaluation,true,&prepared);
    auto &out=result.record;
    if (out.at("status")!="valid" || !std::isfinite(score(out))) throw std::runtime_error("No valid fitted simulation");
    result.inputs={measurement}; out["initial_circuit"]=circuit;
    out["model"]="CircuitCore-AC-fit"; out["optimizer"]="SST-Math SANelderMead";
    out["weighting"]=selectedSigma.empty() ? "modulus_floor" : "sigma_Ohm";
    out["Z_floor_Ohm"]=floor; out["scale_Ohm"]=weights;
    out["score"]=score(out); out["initial_score"]=initialScore; out["score_kind"]="weighted_complex_residual_sum";
    out["stop_status"]=stop; out["iterations"]=optimizer.iteration(); out["invalid_evaluations"]=invalid;
    out["covariance"]=nullptr;
    out["covariance_status"]="insufficient_degrees_of_freedom";
    std::vector<std::size_t> free;
    for (std::size_t i=0;i<specs.size();++i) {
        specs[i]["fitted"]=fitted.at("schematic").at("elements").at(elements[i]).at(fields[i]);
        specs[i]["sigma"]=nullptr;
        if (parameters[i].free) free.push_back(i);
    }
    const auto n=free.size(), rows=weights.size()*2;
    out["covariance_parameters"]=Json::array();
    for (auto i : free) out["covariance_parameters"].push_back(specs[i].at("element").get<std::string>()+"."+fields[i]);
    if (rows>n) {
        // Differentiate in scaled coordinates; convert covariance back to each parameter's SI units.
        std::vector<std::vector<double>> jacobian(n,std::vector<double>(rows));
        bool valid=true;
        for (std::size_t i=0;i<specs.size();++i) prepared.set(slots[i],specs[i].at("fitted").get<double>());
        std::vector<std::complex<double>> leftResiduals(weights.size());
        for (std::size_t j=0;j<n;++j) {
            context.ThrowIfCancellationRequested();
            const auto i=free[j];
            const double value=specs[i]["fitted"], lo=specs[i]["min"], hi=specs[i]["max"];
            const double step=std::cbrt(std::numeric_limits<double>::epsilon())*scales[i];
            const double a=std::max(lo,value-step), b=std::min(hi,value+step);
            bool leftValid=false, rightValid=false;
            try {
                prepared.set(slots[i],a); leftValid=prepared.evaluate(evaluation);
                std::copy(prepared.residuals().begin(),prepared.residuals().end(),leftResiduals.begin());
                prepared.set(slots[i],b); rightValid=prepared.evaluate(evaluation);
                prepared.set(slots[i],value);
            }
            catch (const JobCancelledException&) {throw;}
            catch (const std::exception&) {valid=false; break;}
            if (b<=a || !leftValid || !rightValid) {valid=false; break;}
            for (std::size_t k=0;k<weights.size();++k) for (int part=0;part<2;++part) {
                const auto r=prepared.residuals()[k], l=leftResiduals[k];
                jacobian[j][2*k+part]=((part ? r.imag() : r.real())-(part ? l.imag() : l.real()))*scales[i]/((b-a)*weights[k]);
            }
        }
        std::vector<std::vector<double>> matrix(n,std::vector<double>(2*n));
        double largest=0;
        for (std::size_t j=0;valid && j<n;++j) {
            context.ThrowIfCancellationRequested();
            for (std::size_t k=0;k<n;++k) {
                for (std::size_t row=0;row<rows;++row) matrix[j][k]+=jacobian[j][row]*jacobian[k][row];
                largest=std::max(largest,std::abs(matrix[j][k]));
            }
            matrix[j][n+j]=1;
        }
        for (std::size_t j=0;valid && j<n;++j) {
            context.ThrowIfCancellationRequested();
            auto pivot=j;
            for (std::size_t k=j+1;k<n;++k) if (std::abs(matrix[k][j])>std::abs(matrix[pivot][j])) pivot=k;
            if (!std::isfinite(matrix[pivot][j]) || std::abs(matrix[pivot][j])<=largest*1e-12) {valid=false; break;}
            std::swap(matrix[pivot],matrix[j]);
            const double divisor=matrix[j][j];
            for (auto& value : matrix[j]) value/=divisor;
            for (std::size_t k=0;k<n;++k) if (k!=j) {
                const double factor=matrix[k][j];
                for (std::size_t c=0;c<2*n;++c) matrix[k][c]-=factor*matrix[j][c];
            }
        }
        out["covariance_status"]=valid ? "estimated_from_jacobian" : "rank_deficient_or_invalid_jacobian";
        if (valid) {
            Json covariance=Json::array();
            const double variance=score(out)/(rows-n);
            for (std::size_t j=0;j<n;++j) {
                Json row=Json::array();
                for (std::size_t k=0;k<n;++k) row.push_back(matrix[j][n+k]*variance*scales[free[j]]*scales[free[k]]);
                specs[free[j]]["sigma"]=std::sqrt(std::max(0.,row.at(j).get<double>()));
                covariance.push_back(std::move(row));
            }
            out["covariance"]=std::move(covariance);
        }
    }
    out["parameters"]=specs;
    context.ThrowIfCancellationRequested(); return result;
}
}
