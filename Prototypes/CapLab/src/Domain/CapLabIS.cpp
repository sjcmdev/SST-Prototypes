#include "Domain/CapLabIS.hpp"
#include "AnalysisRunner.h"
#include <sst/physics/impedance.hpp>
#include <complex>
#include <sst/math/nelder_mead.hpp>

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
Json SimulateIS(const Json &m, const Json &c, const Json &settings, JobContext &context, bool plots) {
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
    if (plots) for (const auto &kind : {"Nyquist","Bode magnitude","Bode phase","C-f","G/omega-f","ReZ residual","ImZ residual"}) {
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
namespace SST::Prototypes::CapLab {
Json FitIS(const Json &measurement, const Json &circuit, const Json &settings, JobContext &context) {
    context.ThrowIfCancellationRequested();
    auto m=measurement;
    const double floor=settings.at("Z_floor_Ohm");
    const int limit=settings.value("max_iterations",500);
    const double tolerance=settings.value("score_tolerance",1e-12);
    if (!std::isfinite(floor) || floor<=0 || limit<1 || limit>10000 || !std::isfinite(tolerance) || tolerance<0)
        throw std::runtime_error("Invalid floor, iteration limit or score tolerance");
    const double lower=settings.value("frequency_min_Hz",0.), upper=settings.value("frequency_max_Hz",std::numeric_limits<double>::max());
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower<0 || upper<lower) throw std::runtime_error("Invalid frequency range");
    const auto names=m.at("names").get<std::vector<std::string>>();
    const auto fi=std::find(names.begin(),names.end(),"f");
    if (fi==names.end()) throw std::runtime_error("Missing frequency column");
    const auto frequencies=m.at("columns").at(std::distance(names.begin(),fi));
    std::set<double> unique;
    for (const auto &v : frequencies) {
        const double f=v.get<double>();
        if (!std::isfinite(f) || f<=0 || !unique.insert(f).second) throw std::runtime_error("Invalid/duplicate IS frequency");
    }
    std::vector<double> sigma;
    if (settings.contains("sigma_Ohm")) {
        sigma=settings.at("sigma_Ohm").get<std::vector<double>>();
        if (sigma.size()!=frequencies.size()) throw std::runtime_error("sigma_Ohm must match measurement rows");
        for (double s : sigma) if (!std::isfinite(s) || s<=0) throw std::runtime_error("Invalid sigma_Ohm");
    }
    std::vector<double> selectedSigma;
    for (auto &col : m["columns"]) col=Json::array();
    m["row_ids"]=Json::array();
    for (std::size_t i=0;i<frequencies.size();++i) if (frequencies[i].get<double>()>=lower && frequencies[i].get<double>()<=upper) {
        for (std::size_t j=0;j<names.size();++j) m["columns"][j].push_back(measurement.at("columns").at(j).at(i));
        m["row_ids"].push_back(measurement.at("row_ids").at(i));
        if (!sigma.empty()) selectedSigma.push_back(sigma[i]);
    }
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
    const auto initial=SimulateIS(m,circuit,settings,evaluation,false);
    if (initial.at("status")!="valid") throw std::runtime_error("Invalid initial simulation/measurement: "+initial.at("diagnostics").dump());
    for (std::size_t i=0;i<initial.at("frequency_Hz").size();++i)
        weights.push_back(selectedSigma.empty() ? std::max(std::hypot(initial.at("measured_ReZ_Ohm").at(i).get<double>(),initial.at("measured_ImZ_Ohm").at(i).get<double>()),floor) : selectedSigma[i]);
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
            const double s=score(SimulateIS(m,candidate(values),settings,evaluation,false));
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
    auto out=SimulateIS(m,fitted,settings,evaluation);
    if (out.at("status")!="valid" || !std::isfinite(score(out))) throw std::runtime_error("No valid fitted simulation");
    out["inputs"]=Json::array({measurement}); out["initial_circuit"]=circuit;
    out["model"]="CircuitCore-AC-fit"; out["optimizer"]="SST-Math SANelderMead";
    out["weighting"]=selectedSigma.empty() ? "modulus_floor" : "sigma_Ohm";
    out["Z_floor_Ohm"]=floor; out["scale_Ohm"]=weights;
    out["score"]=score(out); out["initial_score"]=score(initial); out["score_kind"]="weighted_complex_residual_sum";
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
        for (std::size_t j=0;j<n;++j) {
            context.ThrowIfCancellationRequested();
            const auto i=free[j];
            const double value=specs[i]["fitted"], lo=specs[i]["min"], hi=specs[i]["max"];
            const double step=std::cbrt(std::numeric_limits<double>::epsilon())*scales[i];
            const double a=std::max(lo,value-step), b=std::min(hi,value+step);
            auto left=fitted, right=fitted;
            left["schematic"]["elements"][elements[i]][fields[i]]=a;
            right["schematic"]["elements"][elements[i]][fields[i]]=b;
            Json l,r;
            try {l=SimulateIS(m,left,settings,evaluation,false); r=SimulateIS(m,right,settings,evaluation,false);}
            catch (const JobCancelledException&) {throw;}
            catch (const std::exception&) {valid=false; break;}
            if (b<=a || l.at("status")!="valid" || r.at("status")!="valid") {valid=false; break;}
            for (std::size_t k=0;k<weights.size();++k) for (int part=0;part<2;++part) {
                const char* key=part ? "residual_ImZ_Ohm" : "residual_ReZ_Ohm";
                jacobian[j][2*k+part]=(r.at(key).at(k).get<double>()-l.at(key).at(k).get<double>())*scales[i]/((b-a)*weights[k]);
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
    context.ThrowIfCancellationRequested(); return out;
}
}
