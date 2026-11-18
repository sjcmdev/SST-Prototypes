#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <scaps_core/api.hpp>
#include <scaps_core/device_project_yaml.hpp>
#include <yaml-cpp/yaml.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <sstream>
#include <iomanip>
#include <numbers>
#include <thread>
namespace SST::ScapsLab {
namespace {
struct Temporary {
    std::filesystem::path path;
    Temporary() {
        static std::atomic_uint64_t next{0};
        path = std::filesystem::temp_directory_path() / ("scapslab-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(next++) + ".yaml");
    }
    ~Temporary() { std::error_code ec; std::filesystem::remove(path, ec); }
};
Json FromYaml(const YAML::Node &n) {
    if (n.IsNull()) return nullptr;
    if (n.IsSequence()) { auto a=Json::array(); for (const auto &v:n) a.push_back(FromYaml(v)); return a; }
    if (n.IsMap()) { auto o=Json::object(); for (const auto &v:n) o[v.first.as<std::string>()]=FromYaml(v.second); return o; }
    const auto s=n.Scalar();
    if (n.Tag()=="!" || n.Tag()=="tag:yaml.org,2002:str") return s;
    if (s=="true") return true;
    if (s=="false") return false;
    try { std::size_t used; const auto v=std::stoll(s,&used); if(used==s.size()) return v; } catch (...) {}
    try { std::size_t used; const double v=std::stod(s,&used); if (used==s.size() && std::isfinite(v)) return v; } catch (...) {}
    return s;
}
YAML::Node ToYaml(const Json &j) {
    if (j.is_object()) { YAML::Node n(YAML::NodeType::Map); for (auto it=j.begin();it!=j.end();++it) n[it.key()]=ToYaml(it.value()); return n; }
    if (j.is_array()) { YAML::Node n(YAML::NodeType::Sequence); for (const auto &v:j) n.push_back(ToYaml(v)); return n; }
    if (j.is_null()) return YAML::Node(YAML::NodeType::Null);
    if (j.is_string()) { const auto s=j.get<std::string>(); YAML::Node n(s);
        if (s!=".inf" && s!="-.inf" && s!=".nan") n.SetTag("tag:yaml.org,2002:str"); return n; }
    return YAML::Load(j.dump());
}
std::string Text(const Json &j) { YAML::Emitter e; e.SetDoublePrecision(17); e << ToYaml(j); if (!e.good()) throw std::runtime_error(e.GetLastError()); return e.c_str(); }
void SaveText(const std::filesystem::path &p,const std::string &text) {
    std::string error; if (!TextFileIO::Save(Path{p},text,error)) throw std::runtime_error(error);
}
void Positive(double v,const char *field,bool zero=false) {
    if (!std::isfinite(v) || (zero ? v<0 : v<=0)) throw std::invalid_argument(std::string("Invalid ")+field);
}
Json Failure(const char *code,const std::string &message) { return {{"ok",false},{"error",{{"code",code},{"message",message}}}}; }
class Command final : public ICommand {
    Workflow &w;
public:
    explicit Command(Workflow &w):w(w) {}
    std::string Description() const override {return "ScapsLab operation";}
    Result<void> Execute(CommandContext &c) override {
        const auto *r=c.TryGet<Json>("request");
        if (!r) return StructuredError{ErrorCategory::Validation,Severity::Error,"Missing request","Missing JSON request","Supply a request","ScapsLab","invalid_request"};
        if (r->at("operation")=="cancel") {
            if (!w.active || !w.jobs->RequestCancel(w.active)) return StructuredError{ErrorCategory::Validation,Severity::Error,"No running job","No running job","Start a job","ScapsLab","not_running"};
            return {};
        }
        if (w.Busy()) return StructuredError{ErrorCategory::Validation,Severity::Error,"Busy","A job is still active","Wait or cancel","ScapsLab","busy"};
        w.pending=CreateRef<Operation>(w.document,*r); w.pending->acResults=w.acResults; w.response=Json();
        w.active=w.jobs->Submit(w.pending); w.status="Running"; return {};
    }
};
}
scaps_core::DeviceProject Model(const Json &j) {
    if(j.contains("constants_mode") && j.at("constants_mode")!="codata" && j.at("constants_mode")!="scaps_compat") throw std::invalid_argument("Unknown constants_mode");
    if(j.contains("mobility_temperature_model") && j.at("mobility_temperature_model")!="constant" && j.at("mobility_temperature_model")!="sqrt_T" && j.at("mobility_temperature_model")!="caughey_thomas_T") throw std::invalid_argument("Unknown mobility_temperature_model");
    Temporary t; SaveText(t.path,Text(j)); auto p=scaps_core::load_device_project_yaml(t.path);
    if (p.schema_version!=1) throw std::invalid_argument("Unsupported project schema_version");
    Positive(p.temperature_K,"temperature_K"); Positive(p.solver.residual_tolerance,"residual_tolerance");
    if (p.layers.empty() || p.layers.size()>100) throw std::invalid_argument("Expected 1..100 layers");
    if (p.mesh.points_per_layer<3 || p.mesh.points_per_layer>100000) throw std::invalid_argument("Invalid points_per_layer");
    if (p.solver.max_newton_iterations<1) throw std::invalid_argument("Invalid max_newton_iterations");
    Positive(p.mesh.adaptive_target_relative_J_error,"adaptive_target_relative_J_error");
    if (!std::set<std::string>{"0","1","2","3","A","SCAPS"}.contains(p.mesh.adaptivity_level)) throw std::invalid_argument("Unknown adaptivity_level");
    std::set<std::string> ids;
    for (std::size_t i=0;i<p.layers.size();++i) {
        const auto &l=p.layers[i];
        if(j.at("layers")[i].contains("scapslab_id")) {
            const auto id=j.at("layers")[i].at("scapslab_id").get<std::string>();
            if(id.empty() || !ids.insert(id).second) throw std::invalid_argument("Invalid or duplicate layer ID");
        }
        Positive(l.thickness_m,"thickness_m"); Positive(l.material.eps_r,"eps_r"); Positive(l.material.Eg_eV,"Eg_eV");
        if (!std::isfinite(l.material.chi_eV)) throw std::invalid_argument("Invalid chi_eV");
        Positive(l.material.Nc_ref_m3,"Nc_ref_m3"); Positive(l.material.Nv_ref_m3,"Nv_ref_m3");
        Positive(l.material.mu_n_m2Vs,"mu_n_m2_V_s"); Positive(l.material.mu_p_m2Vs,"mu_p_m2_V_s");
        Positive(l.Nd_m3,"Nd_m3",true); Positive(l.Na_m3,"Na_m3",true);
    }
    for (const auto &c:{p.left_contact,p.right_contact})
        if (!std::isfinite(c.phi_m_eV) || std::isnan(c.Sn_mps) || std::isnan(c.Sp_mps) || c.Sn_mps<0 || c.Sp_mps<0) throw std::invalid_argument("Invalid contact work function or surface velocity");
    if (!std::isfinite(p.voltage.start_V) || !std::isfinite(p.voltage.stop_V) || !std::isfinite(p.voltage.step_V) || p.voltage.step_V==0 ||
        (p.voltage.stop_V-p.voltage.start_V)*p.voltage.step_V<0 || std::abs((p.voltage.stop_V-p.voltage.start_V)/p.voltage.step_V)>10000) throw std::invalid_argument("Invalid voltage sweep (maximum 10001 points)");
    (void)scaps_core::make_device_from_project(p); return p;
}
Json ReadProject(const std::string &path) {
    auto raw=FromYaml(YAML::LoadFile(path)); const auto p=Model(raw);
    Temporary t; scaps_core::save_device_project_yaml(p,t.path);
    auto full=FromYaml(YAML::LoadFile(t.path.string())); full.update(raw,true);
    full["mesh"]["adaptivity_level"]=p.mesh.adaptivity_level;
    std::set<std::string> ids;
    for(const auto &l:full["layers"]) if(l.contains("scapslab_id")) ids.insert(l.at("scapslab_id"));
    unsigned next=1;
    for (auto &l:full["layers"]) if(!l.contains("scapslab_id")) {
        while(ids.contains("layer-"+std::to_string(next))) ++next;
        l["scapslab_id"]="layer-"+std::to_string(next); ids.insert(l["scapslab_id"]);
    }
    return full;
}
void WriteProject(const Json &j,const std::string &path) { (void)Model(j); SaveText(Path{path}.Native(),Text(j)); }
void Operation::Execute(JobContext &ctx) {
    using namespace scaps_core;
    ctx.ThrowIfCancellationRequested(); const std::string op=request.at("operation");
    if (op=="load") document=ReadProject(request.at("path").get<std::string>());
    else if (op=="save") WriteProject(document,request.at("path").get<std::string>());
    else if (op=="edit") { auto candidate=document; const auto &patch=request.contains("document")?request.at("document"):request.at("patch");
        if (!patch.is_object()) throw std::invalid_argument("patch must be an object");
        if(request.contains("document")) candidate=patch; else candidate.update(patch,true); (void)Model(candidate); document=std::move(candidate);
    } else if (op=="layer") {
        auto &layers=document["layers"]; const std::string action=request.at("action");
        if (action=="add") { auto l=layers.back(); unsigned n=1; std::set<std::string> ids;
            for (const auto &v:layers) ids.insert(v.at("scapslab_id"));
            while (ids.contains("layer-"+std::to_string(n))) ++n;
            l["scapslab_id"]="layer-"+std::to_string(n); l["name"]="New layer"; layers.push_back(l);
        } else {
            auto it=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.at("scapslab_id")==request.at("layer_id");});
            if (it==layers.end()) throw std::invalid_argument("Unknown layer_id");
            if (action=="remove") { if(layers.size()==1) throw std::invalid_argument("Cannot remove last layer"); layers.erase(it); }
            else if (action=="move") { const auto index=request.at("index").get<int>(); if(index<0 || index>=int(layers.size())) throw std::invalid_argument("Invalid layer index");
                auto l=*it; layers.erase(it); layers.insert(layers.begin()+index,l);
            } else throw std::invalid_argument("Unknown layer action");
        }
        (void)Model(document);
    } else if (op=="export_ac") {
        const auto kind=request.at("kind").get<std::string>();
        if ((kind!="cv" && kind!="cf") || !acResults.contains(kind)) throw std::invalid_argument("No AC result for kind");
        std::ostringstream csv; csv << std::setprecision(17);
        csv << "point_id,voltage_V,frequency_Hz,temperature_K,capacitance_F,conductance_S,G_over_omega_F,inverse_C2_F_minus2,status\n";
        for (const auto &pt:acResults.at(kind).at("points")) {
            ctx.ThrowIfCancellationRequested();
            for(const auto *key:{"point_id","voltage_V","frequency_Hz","temperature_K","capacitance_F","conductance_S","G_over_omega_F","inverse_C2_F_minus2","status"}) {
                if (std::string(key)!="point_id") csv << ',';
                const auto &v=pt.at(key); if(v.is_string()) csv << v.get<std::string>(); else if(!v.is_null()) csv << v.dump();
            }
            csv << '\n';
        }
        ctx.ThrowIfCancellationRequested(); SaveText(Path{request.at("path").get<std::string>()}.Native(),csv.str());
    } else if (op=="cv" || op=="cf") {
        const auto p=Model(document); const auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
        const double T=request.at("temperature_K"); Positive(T,"temperature_K");
        std::vector<double> volts, frequencies;
        if(op=="cv") {
            const double start=request.at("start_V"), stop=request.at("stop_V"), step=request.at("step_V"), f=request.at("frequency_Hz");
            Positive(f,"frequency_Hz");
            if(!std::isfinite(start) || !std::isfinite(stop) || !std::isfinite(step) || step==0 || (stop-start)*step<0 || std::abs((stop-start)/step)>10000)
                throw std::invalid_argument("Invalid C-V range (maximum 10001 points)");
            const double actualStep=std::copysign(std::max(std::abs(step),1e-12),step);
            if(start!=stop && (start+actualStep==start || stop+actualStep==stop || !std::isfinite(stop+actualStep)))
                throw std::invalid_argument("C-V step cannot advance the voltage range");
            volts=start==stop?std::vector<double>{start}:make_voltage_list({start,stop,step}); for(auto &v:volts) v=-v; frequencies={f};
        } else {
            const double v=request.at("voltage_V"), first=request.at("start_Hz"), last=request.at("stop_Hz");
            Positive(first,"start_Hz"); Positive(last,"stop_Hz");
            const auto &count=request.at("points");
            if(!std::isfinite(v) || last<first || !count.is_number_integer() || count.get<double>()<2 || count.get<double>()>10001)
                throw std::invalid_argument("Invalid C-f range (2..10001 points)");
            volts={-v}; const int n=count.get<int>();
            for(int i=0;i<n;++i) frequencies.push_back(i==0?first:i==n-1?last:std::exp(std::log(first)+(std::log(last)-std::log(first))*i/(n-1)));
        }
        ctx.SetStage("Preparing AC mesh (cancel checked after preparation)");
        const auto mesh=prepare_mesh(device,p.mesh,volts,{T},p.solver,pc);
        ctx.ThrowIfCancellationRequested();
        output={{"kind",op},{"mesh_x_m",mesh.x_m},{"area_m2",device.external.area_m2},{"points",Json::array()},
            {"voltage_convention","consumer: left minus right; Y = G + j omega C"}};
        IVPoint dc;
        const auto total=op=="cv"?volts.size():frequencies.size();
        auto finite=[](double x)->Json {return std::isfinite(x)?Json(x):Json(nullptr);};
        for(std::size_t i=0;i<total;++i) {
            ctx.ThrowIfCancellationRequested(); ctx.SetStage("AC point "+std::to_string(i+1)+" / "+std::to_string(total));
            if(op=="cv" || i==0) {
                // shortcut: replay DC prefixes for exact core continuation; use a cancellable core step API when exposed.
                dc=solve_iv(device,mesh,std::vector<double>(volts.begin(),volts.begin()+(op=="cv"?i+1:1)),T,p.solver,pc).back();
            }
            ctx.ThrowIfCancellationRequested();
            const auto ac=solve_ac_spectrum(device,mesh,dc,{frequencies[op=="cv"?0:i]},1e-3,pc).front();
            output["points"].push_back({{"point_id",op+"-"+std::to_string(i)},{"voltage_V",-ac.voltage_V},{"frequency_Hz",ac.frequency_Hz},{"temperature_K",T},
                {"capacitance_F",finite(ac.C_F)},{"conductance_S",finite(ac.G_S)},{"G_over_omega_F",finite(ac.G_S/(2*std::numbers::pi*ac.frequency_Hz))},
                {"inverse_C2_F_minus2",finite(1/(ac.C_F*ac.C_F))},{"status",to_string(ac.status)},{"conservation_relative",finite(ac.current_conservation_relative)}});
            ctx.SetProgress(float(i+1),float(total));
        }
    } else if (op=="run") {
        const auto p=Model(document); const auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
        auto volts=make_voltage_list(p.voltage); for(auto &v:volts) v=-v;
        ctx.SetStage("Preparing mesh (cancel checked after preparation)");
        const auto mesh=prepare_mesh(device,p.mesh,volts,{p.temperature_K},p.solver,pc);
        ctx.ThrowIfCancellationRequested(); ctx.SetStage("Equilibrium");
        const auto eq=solve_equilibrium(device,mesh,p.temperature_K,p.solver,pc); auto previous=eq.state;
        output={{"mesh_x_m",mesh.x_m},{"points",Json::array()},{"voltage_convention","consumer: left minus right; J: scaps_core signed terminal current"}};
        for(std::size_t i=0;i<volts.size();++i) {
            ctx.ThrowIfCancellationRequested(); ctx.SetStage("I-V point "+std::to_string(i+1)+" / "+std::to_string(volts.size()));
            IVPoint pt;
            if (p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2)) {
                // shortcut: external networks replay continuation prefixes; use a core step API when exposed.
                pt=solve_iv(device,mesh,std::vector<double>(volts.begin(),volts.begin()+i+1),p.temperature_K,p.solver,pc).back();
            } else {
                const auto r=volts[i]==0 ? eq : solve_bias(device,mesh,previous,volts[i],p.temperature_K,p.solver,pc);
                pt.state=r.state; pt.status=r.status; pt.iterations=volts[i]==0 ? 0 : r.iterations; pt.final_residual_norm=r.final_residual_norm;
                const auto &j=r.state.Jtotal_Am2;
                if (!j.empty()) { for(double v:j) pt.J_total_Am2+=v; pt.J_total_Am2/=double(j.size());
                    for(double v:j) pt.J_conservation_error_Am2=std::max(pt.J_conservation_error_Am2,std::abs(v-pt.J_total_Am2)); }
                if(r.status==SolverStatus::Converged) previous=r.state;
            }
            const auto &s=pt.state;
            const bool external=p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);
            const double terminalJ=pt.status!=SolverStatus::Converged ? NAN : external ? pt.J_total_Am2 : s.Jn_Am2.front()+s.Jp_Am2.front();
            output["points"].push_back({{"bias_id","bias-"+std::to_string(i)},{"voltage_V",-volts[i]},{"J_A_m2",std::isfinite(terminalJ)?Json(terminalJ):Json(nullptr)},
                {"conservation_error_A_m2",pt.J_conservation_error_Am2},{"status",to_string(pt.status)},{"iterations",pt.iterations},{"residual",pt.final_residual_norm},
                {"x_m",s.x_m.empty()?mesh.x_m:s.x_m},{"Ec_eV",s.Ec_eV},{"Ev_eV",s.Ev_eV},{"EFn_eV",s.EFn_eV},{"EFp_eV",s.EFp_eV},{"n_m3",s.n_m3},{"p_m3",s.p_m3}});
            ctx.SetProgress(float(i+1),float(volts.size()));
        }
    } else if(op!="inspect") throw std::invalid_argument("Unknown operation");
    if(op!="save" && op!="export_ac") ctx.ThrowIfCancellationRequested();
}
Workflow::Workflow(Ref<JobSystem> j,Ref<CommandRegistry> registry):Layer("ScapsLab workflow"),jobs(std::move(j)),commands(registry?std::move(registry):CreateRef<CommandRegistry>()) {
    for(const auto *op:{"load","save","edit","layer","run","cv","cf","export_ac","inspect","cancel"}) {
        const auto registered=commands->Register({CommandID{std::string("scapslab.")+op},std::string("ScapsLab ")+op,"ScapsLab","",{},CommandFlags::AsyncCapable},
            [this](CommandContext &){return CreateUnique<Command>(*this);});
        if(!registered) throw std::runtime_error("Cannot register ScapsLab command");
    }
}
Workflow::~Workflow() { if(active) { (void)jobs->RequestCancel(active); while(active) {OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(5));} } }
Json Workflow::Dispatch(const Json &r) {
    try { CommandContext c; c.Set("request",r);
        const auto out=commands->Execute(CommandID{"scapslab."+r.at("operation").get<std::string>()},std::move(c));
        if(!out) return Failure("command_rejected",out.Error().technicalDetails);
        return {{"ok",true},{"job_id",active},{"error",nullptr}};
    } catch(const std::exception &e) {return Failure("invalid_request",e.what());}
}
void Workflow::OnUpdate(float) {
    if(!active) return; const auto snapshot=jobs->GetJob(active); if(!snapshot) return;
    if(snapshot->status==JobStatus::Queued || snapshot->status==JobStatus::Running) return;
    const auto op=pending->request.at("operation").get<std::string>();
    if(snapshot->status==JobStatus::Completed) {
        document=pending->document;
        if(op=="run") result=pending->output;
        if(op=="cv" || op=="cf") acResults[op]=pending->output;
        if(op=="edit" || op=="layer" || op=="load") {result=Json::object();acResults=Json::object();}
        if(op=="load" || op=="save") { path=pending->request.at("path"); dirty=false; }
        if(op=="edit" || op=="layer") dirty=true;
        status="Completed: "+op; response={{"ok",true},{"document",document},{"result",result},{"ac_results",acResults},{"error",nullptr}};
    } else {status=snapshot->errorMessage; response=Failure(snapshot->status==JobStatus::Cancelled?"cancelled":"operation_failed",status);}
    active=0; pending.reset();
}
}





