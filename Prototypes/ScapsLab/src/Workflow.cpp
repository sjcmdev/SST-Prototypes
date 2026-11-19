#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <scaps_core/api.hpp>
#include <scaps_core/physics.hpp>
#include <scaps_core/device_project_yaml.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
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
class EditRecord final : public ICommand {
    Workflow &w;
    Json before, after;
    std::string gesture;
    Result<void> Apply(const Json &d) {
        w.pending=CreateRef<Operation>(w.document,Json{{"operation","edit"},{"document",d},{"history",true}});
        w.active=w.jobs->Submit(w.pending); return {};
    }
public:
    EditRecord(Workflow &w,Json before,Json after,std::string gesture):w(w),before(std::move(before)),after(std::move(after)),gesture(std::move(gesture)) {}
    std::string Description() const override {return "Edit ScapsLab parameters";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(after);}
    Result<void> Undo(CommandContext &) override {return Apply(before);}
    bool CanMerge(const ICommand &next) const noexcept override {
        const auto *edit=dynamic_cast<const EditRecord*>(&next);
        return edit && !gesture.empty() && gesture==edit->gesture;
    }
    Result<void> Merge(std::unique_ptr<ICommand> next) override {after=static_cast<EditRecord&>(*next).after;return {};}
};
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
        const std::string op=r->at("operation");
        if(op=="auto_recalculate") {
            const bool automatic=r->at("enabled").get<bool>(), band=r->value("band_enabled",w.autoRecalculateBand);
            w.autoRecalculate=automatic;w.autoRecalculateBand=band;
            if(w.autoRecalculate || w.autoRecalculateBand) w.Changed();
            w.response={{"ok",true},{"auto_recalculate",w.autoRecalculate},{"auto_recalculate_band",w.autoRecalculateBand},{"error",nullptr}};
            w.status=(w.autoRecalculate || w.autoRecalculateBand)?"Auto-recalculate enabled (250 ms debounce)":"Auto-recalculate disabled"; return {};
        }
        if(w.active && op=="edit" && (w.pending->request.at("operation")=="load" || w.pending->request.at("operation")=="layer"))
            return StructuredError{ErrorCategory::Validation,Severity::Error,"Busy","Project structure is changing; wait before editing","Wait for the operation","ScapsLab","busy"};
        if(w.Busy() && op=="edit") {
            auto candidate=w.document;
            if(w.pending && w.pending->request.at("operation")=="edit") {
                const auto &edit=w.pending->request;
                if(edit.contains("document")) candidate=edit.at("document");else candidate.update(edit.at("patch"),true);
            }
            if(!w.edits.empty()) candidate=w.edits.back().at("document");
            if(r->contains("document")) candidate=r->at("document");else candidate.update(r->at("patch"),true);
            auto edit=*r;edit["document"]=std::move(candidate);
            const auto gesture=edit.value("gesture_id",std::string{});
            if(!gesture.empty() && !w.edits.empty() && w.edits.back().value("gesture_id",std::string{})==gesture) w.edits.back()=std::move(edit);
            else w.edits.push_back(std::move(edit));
            if(w.active && (w.pending->request.at("operation")=="run" || w.pending->request.at("operation")=="band" || w.pending->request.at("operation")=="cv" || w.pending->request.at("operation")=="cf")) (void)w.jobs->RequestCancel(w.active);
            w.changedAt=std::chrono::steady_clock::now(); return {};
        }
        if (w.Busy()) return StructuredError{ErrorCategory::Validation,Severity::Error,"Busy","A job is still active","Wait or cancel","ScapsLab","busy"};
        if(op=="undo" || op=="redo") {
            const auto result=op=="undo"?w.undo.Undo():w.undo.Redo();
            if(!result) return result;
            w.status=w.active?"Applying history":"No history available";
            w.response={{"ok",true},{"document",w.document},{"error",nullptr}};return {};
        }
        if(op=="run") {w.needsRecalculate=false;w.needsBand=false;}
        if(op=="band") w.needsBand=false;
        w.pending=CreateRef<Operation>(w.document,*r); w.pending->acResults=w.acResults; w.response=Json();
        w.active=w.jobs->Submit(w.pending); w.status="Running"; return {};
    }
};
}
scaps_core::DeviceProject Model(const Json &j) {
    if(j.contains("constants_mode") && j.at("constants_mode")!="codata" && j.at("constants_mode")!="scaps_compat") throw std::invalid_argument("Unknown constants_mode");
    if(j.contains("mobility_temperature_model") && j.at("mobility_temperature_model")!="constant" && j.at("mobility_temperature_model")!="sqrt_T" && j.at("mobility_temperature_model")!="caughey_thomas_T") throw std::invalid_argument("Unknown mobility_temperature_model");
    std::set<std::string> defectIds;
    for(const auto &l:j.at("layers")) for(const auto &d:l.value("defects",Json::array())) {
        if(d.contains("scapslab_id")) {const auto id=d.at("scapslab_id").get<std::string>();if(id.empty() || !defectIds.insert(id).second) throw std::invalid_argument("Invalid or duplicate defect scapslab_id");}
        if(!std::set<std::string>{"relative_to_Ei","relative_to_Ec","relative_to_Ev"}.contains(d.value("energy_reference",std::string("relative_to_Ei")))) throw std::invalid_argument("Unknown defect energy_reference");
        if(!std::set<std::string>{"neutral","donor_like","acceptor_like"}.contains(d.value("charge_type",std::string("neutral")))) throw std::invalid_argument("Unknown defect charge_type");
    }
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
const Json &PointAtBias(const Json &sweep,const Json &band,int selected) {
    if(sweep.empty()) return band.at("points").at(0);
    const auto &points=sweep.at("points");const auto &point=points.at(std::clamp(selected,0,int(points.size())-1));
    return !band.empty() && band.at("points").at(0).at("voltage_V")==point.at("voltage_V")?band.at("points").at(0):point;
}
Json NewDefect(bool sheet) {
    Json d={{"energy_distribution","single"},{"energy_offset_eV",0.0},{"charge_type","neutral"},
        {"characteristic_energy_eV",0.05},{"gaussian_width",6.0},{"energy_levels",63},{"energy_quadrature","gauss_legendre"}};
    if(sheet) d.update(Json{{"id","interface-defect"},{"left_layer_index",0},{"energy_reference",7},{"Nt_m2",1e14},
        {"sigma_n_left_m2",1e-19},{"sigma_p_left_m2",1e-19},{"sigma_n_right_m2",1e-19},{"sigma_p_right_m2",1e-19},
        {"density_convention","integrated_sheet"},{"peak_density_m2_eV",0.0}});
    else d.update(Json{{"scapslab_id","defect"},{"label","Defect"},{"energy_reference","relative_to_Ei"},{"Nt_m3",1e19},
        {"sigma_n_m2",1e-19},{"sigma_p_m2",1e-19},{"tail_width",7.0},{"distribution_normalization","scaps"},
        {"amphoteric",false},{"correlated",true},{"second_energy_offset_eV",0.1},{"second_sigma_n_m2",1e-19},{"second_sigma_p_m2",1e-19}});
    return d;
}
void ChangeSheetDistribution(Json &d,const std::string &shape,const std::string &convention) {
    auto width=[&](const std::string &kind) {
        return d.at("characteristic_energy_eV").get<double>()*(kind=="gaussian"?std::sqrt(std::numbers::pi)*std::erf(d.value("gaussian_width",6.0)*.5):1);
    };
    // Keep the total sheet density when changing its representation.
    const auto oldShape=d.at("energy_distribution").get<std::string>();
    const double total=oldShape=="single" || d.at("density_convention")=="integrated_sheet"?d.at("Nt_m2").get<double>():d.at("peak_density_m2_eV").get<double>()*width(oldShape);
    d["energy_distribution"]=shape;d["density_convention"]=convention;
    const bool integrated=shape=="single" || convention=="integrated_sheet";
    d["Nt_m2"]=integrated?total:0.;d["peak_density_m2_eV"]=integrated?0.:total/width(shape);
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
    unsigned defectId=1;
    std::set<std::string> defectIds;
    for(const auto &l:full["layers"]) for(const auto &d:l["defects"]) if(d.contains("scapslab_id")) defectIds.insert(d.at("scapslab_id"));
    for(auto &l:full["layers"]) for(auto &d:l["defects"]) if(!d.contains("scapslab_id")) {
        while(defectIds.contains("defect-"+std::to_string(defectId))) ++defectId;
        d["scapslab_id"]="defect-"+std::to_string(defectId); defectIds.insert(d.at("scapslab_id"));
    }
    return full;
}
void WriteProject(const Json &j,const std::string &path) { (void)Model(j); SaveText(Path{path}.Native(),Text(j)); }
namespace {
Json DefectCurves(const Json &document,const scaps_core::DeviceProject &p,const scaps_core::DeviceState &state) {
    using namespace scaps_core;
    Json curves=Json::array(); const auto pc=physical_constants(p.constants_mode);
    auto curve=[&](const Json &d,double ev,double ec,double ei,double total,bool sheet) {
        const auto shape=d.value("energy_distribution",std::string("single"));
        const double offset=d.value("energy_offset_eV",0.0),width=d.value("characteristic_energy_eV",0.05);
        double center=ei+offset;
        if(sheet) {
            const int ref=d.value("energy_reference",7);
            const int l=d.at("left_layer_index"); const auto &a=p.layers[l].material,&b=p.layers[l+1].material;
            const auto ta=evaluate_thermal_material(a,p.temperature_K,pc),tb=evaluate_thermal_material(b,p.temperature_K,pc);
            const double va=-a.chi_eV-a.Eg_eV,vb=-b.chi_eV-b.Eg_eV,ca=-a.chi_eV,cb=-b.chi_eV;
            center=(ref==3?std::max(va,vb):ref==4?std::min(ca,cb):ref==5?Ei_of(ca,va,ta.Nc_m3,ta.Nv_m3,ta.Vt_V):ref==6?Ei_of(cb,vb,tb.Nc_m3,tb.Nv_m3,tb.Vt_V):ref==8?vb:ref==9?.5*(std::max(va,vb)+std::min(ca,cb)):va)+(ref==4?-offset:offset);
        } else {
            const auto ref=d.value("energy_reference",std::string("relative_to_Ei"));
            center=ref=="relative_to_Ec"?ec-offset:ref=="relative_to_Ev"?ev+offset:ei+offset;
        }
        const bool single=shape=="single",gaussian=shape=="gaussian",cb=shape=="cb_tail",vb=shape=="vb_tail";
        double lo=std::max(ev,center-(cb?d.value("tail_width",7.0)*width:vb?0:.5*width*(gaussian?d.value("gaussian_width",6.0):1)));
        double hi=std::min(ec,center+(vb?d.value("tail_width",7.0)*width:cb?0:.5*width*(gaussian?d.value("gaussian_width",6.0):1)));
        // Interface quadrature spans its reference-coordinate interval, without bulk gap clipping.
        if(sheet) {lo=center-.5*width*(gaussian?d.value("gaussian_width",6.0):1);hi=2*center-lo;}
        auto mass=[&](double e) {return gaussian?.5*std::erf((e-center)/width):cb?std::exp((e-center)/width):vb?-std::exp((center-e)/width):(e-center)/width;};
        double amplitude=total/width;
        if(sheet && d.value("density_convention",std::string("scaps_peak"))=="scaps_peak") amplitude=d.value("peak_density_m2_eV",0.0)*(gaussian?std::sqrt(std::numbers::pi):1);
        else if((sheet || d.value("distribution_normalization",std::string("scaps"))=="normalized_continuum") && hi>lo) amplitude/=mass(hi)-mass(lo);
        Json c={{"id",d.at(sheet?"id":"scapslab_id")},{"label",d.value("label",d.value("id",std::string("Defect")))},{"sheet",sheet},{"Ev_eV",ev},{"Ec_eV",ec},
            {"energy_eV",Json::array()},{sheet?"density_m2_eV":"density_m3_eV",Json::array()},{"occupation_energy_eV",Json::array()},{"occupation",Json::array()},
            {"single",single},{"level_energy_eV",center},{sheet?"Nt_m2":"Nt_m3",total},{"amphoteric",d.value("amphoteric",false)},{"correlated",d.value("correlated",true)}};
        if(!single) for(int i=0;i<=400;++i) {
            const double e=ev+(ec-ev)*i/400.;double value=0;
            if(e>=lo && e<=hi) value=amplitude*(gaussian?std::exp(-std::pow((e-center)/width,2))/std::sqrt(std::numbers::pi):cb?std::exp((e-center)/width):vb?std::exp((center-e)/width):1);
            c["energy_eV"].push_back(e-ev);c[sheet?"density_m2_eV":"density_m3_eV"].push_back(value);
        }
        c["level_energy_eV"]=center-ev;c["band_edges_eV"]={0.,ec-ev};
        if(sheet) {
            const int l=d.at("left_layer_index");const auto &a=p.layers[l].material,&b=p.layers[l+1].material;
            c["band_edges_eV"]={-a.chi_eV-a.Eg_eV-ev,-a.chi_eV-ev,-b.chi_eV-b.Eg_eV-ev,-b.chi_eV-ev};
        }
        if(d.value("amphoteric",false)) c["second_level_energy_eV"]=center-ev+(d.at("second_energy_offset_eV").get<double>()-offset)*(d.value("energy_reference",std::string("relative_to_Ei"))=="relative_to_Ec"?-1:1);
        return c;
    };
    double x=0;
    for(std::size_t li=0;li<p.layers.size();++li) {
        const auto &l=p.layers[li]; const double middle=x+l.thickness_m*.5;x+=l.thickness_m;
        const auto at=std::min_element(state.x_m.begin(),state.x_m.end(),[&](double a,double b){return std::abs(a-middle)<std::abs(b-middle);});
        const std::size_t node=std::size_t(at-state.x_m.begin());
        const auto tm=evaluate_thermal_material(l.material,p.temperature_K,pc);
        const double ev=-l.material.chi_eV-l.material.Eg_eV,ec=-l.material.chi_eV,ei=Ei_of(ec,ev,tm.Nc_m3,tm.Nv_m3,tm.Vt_V);
        std::size_t levelOffset=0;
        for(std::size_t di=0;di<l.defects.size();++di) {
            auto isolated=p;isolated.interface_defects.clear();for(auto &layer:isolated.layers) layer.defects.clear();isolated.layers[li].defects={l.defects[di]};
            const auto levels=make_device_from_project(isolated).layers[li].defects;
            auto c=curve(document.at("layers")[li].at("defects")[di],ev,ec,ei,l.defects[di].Nt_m3,false);
            c["layer_id"]=document.at("layers")[li].at("scapslab_id");
            for(std::size_t k=0;k<levels.size();++k) {
                const auto &level=levels[k];const double e=level.energy_reference==DefectEnergyReference::RelativeToEc?ec-level.energy_offset_eV:level.energy_reference==DefectEnergyReference::RelativeToEv?ev+level.energy_offset_eV:ei+level.energy_offset_eV;
                c["occupation_energy_eV"].push_back(e-ev);
                c["occupation"].push_back(node<state.trap_occupation.size() && levelOffset+k<state.trap_occupation[node].size()?Json(state.trap_occupation[node][levelOffset+k]):Json(nullptr));
            }
            levelOffset+=levels.size();curves.push_back(std::move(c));
        }
    }
    std::size_t offset=0;
    for(std::size_t i=0;i<p.interface_defects.size();++i) {
        const auto &d=p.interface_defects[i];const auto &a=p.layers[d.left_layer_index].material,&b=p.layers[d.left_layer_index+1].material;
        const double ev=std::min(-a.chi_eV-a.Eg_eV,-b.chi_eV-b.Eg_eV),ec=std::max(-a.chi_eV,-b.chi_eV);
        auto c=curve(document.at("interface_defects")[i],ev,ec,(ev+ec)*.5,d.Nt_m2,true);c["left_layer_index"]=d.left_layer_index;
        auto isolated=p;isolated.interface_defects={d};const auto levels=make_device_from_project(isolated).interface_defects;
        for(std::size_t k=0;k<levels.size();++k) {
            auto sample=document.at("interface_defects")[i];sample["energy_offset_eV"]=levels[k].energy_offset_eV;
            const auto single=curve(sample,ev,ec,(ev+ec)*.5,d.Nt_m2,true);
            c["occupation_energy_eV"].push_back(single.at("level_energy_eV"));
            c["occupation"].push_back(offset+k<state.interface_occupation.size()?Json(state.interface_occupation[offset+k]):Json(nullptr));
        }
        double integrated=0;for(const auto &level:levels) integrated+=level.Nt_m2;c["Nt_m2"]=integrated;
        offset+=levels.size();curves.push_back(std::move(c));
    }
    return curves;
}
}
void Operation::Execute(JobContext &ctx) {
    using namespace scaps_core;
    const auto started=std::chrono::steady_clock::now();
    ctx.ThrowIfCancellationRequested(); const std::string op=request.at("operation");
    if (op=="load") document=ReadProject(request.at("path").get<std::string>());
    else if (op=="save") WriteProject(document,request.at("path").get<std::string>());
    else if (op=="edit") { auto candidate=document; const auto &patch=request.contains("document")?request.at("document"):request.at("patch");
        if (!patch.is_object()) throw std::invalid_argument("patch must be an object");
        if(request.contains("document")) candidate=patch; else candidate.update(patch,true); (void)Model(candidate); document=std::move(candidate);
    } else if (op=="layer") {
        auto &layers=document["layers"]; const std::string action=request.at("action");
        if(action!="add" && !document.value("interface_defects",Json::array()).empty()) throw std::invalid_argument("Remove interface defects before removing or reordering layers; interface attachment must be preserved");
        if (action=="add") { auto l=layers.back(); unsigned n=1; std::set<std::string> ids;
            for (const auto &v:layers) ids.insert(v.at("scapslab_id"));
            while (ids.contains("layer-"+std::to_string(n))) ++n;
            l["scapslab_id"]="layer-"+std::to_string(n); l["name"]="New layer";
            for(auto &d:l["defects"]) d["scapslab_id"]=l["scapslab_id"].get<std::string>()+"-"+d.at("scapslab_id").get<std::string>();
            layers.push_back(l);
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
        csv << "point_id,voltage_V,frequency_Hz,temperature_K,capacitance_F,conductance_S,G_over_omega_F,inverse_C2_F_minus2,status,valid,validity_status,conservation_relative,area_m2\n";
        for (auto pt:acResults.at(kind).at("points")) {
            pt["area_m2"]=acResults.at(kind).at("area_m2");
            ctx.ThrowIfCancellationRequested();
            for(const auto *key:{"point_id","voltage_V","frequency_Hz","temperature_K","capacitance_F","conductance_S","G_over_omega_F","inverse_C2_F_minus2","status","valid","validity_status","conservation_relative","area_m2"}) {
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
    } else if (op=="run" || op=="band") {
        const auto p=Model(document); const auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
        auto volts=op=="band"?std::vector<double>{request.at("voltage_V").get<double>()}:make_voltage_list(p.voltage);
        for(auto &v:volts) {if(!std::isfinite(v)) throw std::invalid_argument("Invalid voltage_V");v=-v;}
        ctx.SetStage("Preparing mesh (cancel checked after preparation)");
        const auto mesh=prepare_mesh(device,p.mesh,volts,{p.temperature_K},p.solver,pc);
        ctx.ThrowIfCancellationRequested(); ctx.SetStage("Equilibrium");
        const auto eq=solve_equilibrium(device,mesh,p.temperature_K,p.solver,pc); auto previous=eq.state;
        output={{"contact_phi_eV",{device.contact_left.phi_m_eV,device.contact_right.phi_m_eV}},{"mesh_x_m",mesh.x_m},{"points",Json::array()},{"voltage_convention","consumer: left minus right; J: scaps_core signed terminal current"}};
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
            const auto curves=DefectCurves(document,p,s);
            const bool external=p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);
            const double terminalJ=pt.status!=SolverStatus::Converged ? NAN : external ? pt.J_total_Am2 : s.Jn_Am2.front()+s.Jp_Am2.front();
            output["points"].push_back({{"defect_previews",curves},{"bias_id","bias-"+std::to_string(i)},{"voltage_V",-volts[i]},{"J_A_m2",std::isfinite(terminalJ)?Json(terminalJ):Json(nullptr)},
                {"conservation_error_A_m2",pt.J_conservation_error_Am2},{"status",to_string(pt.status)},{"iterations",pt.iterations},{"residual",pt.final_residual_norm},
                {"internal_voltage_V",-s.applied_voltage_V},{"terminal_voltage_V",external?-pt.voltage_V:-volts[i]},{"trap_occupation",s.trap_occupation},{"interface_occupation",s.interface_occupation},{"x_m",s.x_m.empty()?mesh.x_m:s.x_m},{"Ec_eV",s.Ec_eV},{"Ev_eV",s.Ev_eV},{"EFn_eV",s.EFn_eV},{"EFp_eV",s.EFp_eV},{"n_m3",s.n_m3},{"p_m3",s.p_m3}});
            ctx.SetProgress(float(i+1),float(volts.size()));
        }
    } else if(op!="inspect") throw std::invalid_argument("Unknown operation");
    if(op=="run" || op=="band" || op=="cv" || op=="cf") {
        output["project_snapshot"]=document;
        bool valid=true;
        for(auto &point:output["points"]) {
            const bool converged=point.at("status")=="Converged";
            const double conservation=!point.contains("conservation_relative")?0:point.at("conservation_relative").is_number()?point.at("conservation_relative").get<double>():NAN;
            bool physical=converged && std::isfinite(conservation) && conservation<=0.01;
            if(point.contains("J_A_m2")) {
                const double current=point.at("J_A_m2").is_number()?std::abs(point.at("J_A_m2").get<double>()):0;
                physical=physical && point.at("J_A_m2").is_number() && point.at("conservation_error_A_m2").is_number() && point.at("conservation_error_A_m2").get<double>()<=std::max(1e-10,current*0.01);
            }
            if(point.contains("capacitance_F")) physical=physical && point.at("capacitance_F").is_number() && point.at("conductance_S").is_number();
            point["valid"]=physical;point["validity_status"]=!converged?"solver_failed":physical?"valid":"current_unresolved";
            valid &= physical;
        }
        output["valid"]=valid;output["validity_status"]=valid?"valid":"contains_invalid_points";
        output["compute_time_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        output["mesh_node_count"]=output.at("mesh_x_m").size();
    }
    if(op!="save" && op!="export_ac") ctx.ThrowIfCancellationRequested();
}
Workflow::Workflow(Ref<JobSystem> j,Ref<CommandRegistry> registry):Layer("ScapsLab workflow"),jobs(std::move(j)),commands(registry?std::move(registry):CreateRef<CommandRegistry>()) {
    for(const auto *op:{"load","save","edit","layer","run","cv","cf","export_ac","inspect","cancel","undo","redo","auto_recalculate","band"}) {
        const auto registered=commands->Register({CommandID{std::string("scapslab.")+op},std::string("ScapsLab ")+op,"ScapsLab","",{},CommandFlags::AsyncCapable},
            [this](CommandContext &){return CreateUnique<Command>(*this);});
        if(!registered) throw std::runtime_error("Cannot register ScapsLab command");
    }
}
Workflow::~Workflow() { if(active) { (void)jobs->RequestCancel(active); while(active) {OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(5));} } }
Json Workflow::Dispatch(const Json &r) {
    try { CommandContext c; c.Set("request",r);
        const auto out=commands->Execute(CommandID{"scapslab."+r.at("operation").get<std::string>()},std::move(c));
        if(!out) {status=out.Error().technicalDetails; return Failure("command_rejected",status);}
        return {{"ok",true},{"job_id",active},{"error",nullptr}};
    } catch(const std::exception &e) {return Failure("invalid_request",e.what());}
}
void Workflow::Changed() {
    needsRecalculate=true;needsBand=true;bandResult=Json::object(); changedAt=std::chrono::steady_clock::now();
    result=Json::object(); acResults=Json::object();
}
void Workflow::OnUpdate(float) {
    if(!active) {
        if(!edits.empty()) {
            pending=CreateRef<Operation>(document,std::move(edits.front()));edits.pop_front();
            active=jobs->Submit(pending);status="Applying queued edit";response=Json();
        }
        else if(autoRecalculate && needsRecalculate && !interacting && std::chrono::steady_clock::now()-changedAt>=std::chrono::milliseconds(250)) {
            needsRecalculate=false; (void)Dispatch({{"operation","run"}});
        } else if(autoRecalculateBand && needsBand && !interacting && std::chrono::steady_clock::now()-changedAt>=std::chrono::milliseconds(250)) {
            needsBand=false;(void)Dispatch({{"operation","band"},{"voltage_V",biasVoltage_V}});
        }
        return;
    } const auto snapshot=jobs->GetJob(active); if(!snapshot) return;
    if(snapshot->status==JobStatus::Queued || snapshot->status==JobStatus::Running) return;
    const auto op=pending->request.at("operation").get<std::string>();
    if(snapshot->status==JobStatus::Completed) {
        if((op=="edit" || op=="layer") && !pending->request.value("history",false) && document!=pending->document)
            (void)undo.PushExecuted(CreateUnique<EditRecord>(*this,document,pending->document,pending->request.value("gesture_id",std::string{})));
        document=pending->document;
        if(op=="run") {result=pending->output;bandResult=Json::object();}
        if(op=="band") bandResult=pending->output;
        if(op=="cv" || op=="cf") acResults[op]=pending->output;
        if(op=="edit" || op=="layer" || op=="load") Changed();
        if(op=="load") undo.Clear();
        if(op=="load" || op=="save") { path=pending->request.at("path"); dirty=false; undo.MarkClean(); }
        if(op=="edit" || op=="layer") dirty=!undo.IsClean();
        status="Completed: "+op;if(pending->output.contains("validity_status")) status+=" | validity: "+pending->output.at("validity_status").get<std::string>(); response={{"ok",true},{"document",document},{"result",result},{"ac_results",acResults},{"auto_recalculate",autoRecalculate},{"auto_recalculate_band",autoRecalculateBand},{"band_result",bandResult},{"error",nullptr}};
    } else {status=snapshot->errorMessage; response=Failure(snapshot->status==JobStatus::Cancelled?"cancelled":"operation_failed",status);}
    active=0; pending.reset();
}
}





