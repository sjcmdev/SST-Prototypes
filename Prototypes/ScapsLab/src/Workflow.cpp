#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <scaps_core/api.hpp>
#include <scaps_core/illumination.hpp>
#include <scaps_core/device_project_def.hpp>
#include <scaps_core/physics.hpp>
#include <scaps_core/grading.hpp>
#include <scaps_core/scaps_material.hpp>
#include <scaps_core/device_project_yaml.hpp>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <numbers>
#include <thread>
#include "Core/JobSystem/ParallelFor.hpp"
#include <mutex>
#include <exception>
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
    std::string deviceId;
    Json before, after;
    std::string gesture;
    Result<void> Apply(const Json &d) {
        w.StartOperation(Json{{"operation","edit"},{"document",d},{"history",true}},deviceId); return {};
    }
public:
    EditRecord(Workflow &w,Json before,Json after,std::string gesture):w(w),deviceId(w.pendingDevice),before(std::move(before)),after(std::move(after)),gesture(std::move(gesture)) {}
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
    Json defaultRequest;
    explicit Command(Workflow &w,Json request=Json::object()):w(w),defaultRequest(std::move(request)) {}
    std::string Description() const override {return "ScapsLab operation";}
    Result<void> Execute(CommandContext &c) override {
        const auto *r=c.TryGet<Json>("request");
        if (!r && !defaultRequest.empty()) r=&defaultRequest;
        if (!r) return StructuredError{ErrorCategory::Validation,Severity::Error,"Missing request","Missing JSON request","Supply a request","ScapsLab","invalid_request"};
        if (r->at("operation")=="cancel") {
            w.runQueue.clear();
            if (!w.active || !w.jobs->RequestCancel(w.active)) return StructuredError{ErrorCategory::Validation,Severity::Error,"No running job","No running job","Start a job","ScapsLab","not_running"};
            return {};
        }
        const std::string op=r->at("operation");
        if(op=="project_save" || (op=="save" && std::filesystem::path(r->at("path").get<std::string>()).extension()==".scapslab")) {(void)r->at("path").get<std::string>();w.StartSave(*r);return {};}
        if((op=="load" || op=="project_load") && w.active) {
            const std::string running=w.pending->request.at("operation");
            if(running=="run" || running=="band" || running=="cv" || running=="cf" || running=="qe" || running=="run_batch") {
                w.queuedProject=*r;w.runQueue.clear();
                (void)w.jobs->RequestCancel(w.active);
                w.status="Cancelling calculation; project will open after pending edits finish";
                return {};
            }
        }
        if(op=="history_list") {if(r->contains("device_id")) (void)w.Device(r->at("device_id").get<std::string>());w.response=w.Snapshot();return {};} // S7 regression: read-only, answered inline like device_list
        if(op=="history_select" || op=="history_clear" || op=="history_update") {w.HistoryOp(*r);return {};}
        if(op.starts_with("tutorial_")) {w.TutorialRequest(*r);return {};}
        if(op=="device_select") {
            if(w.active) {const std::string pending=w.pending->request.at("operation");if(pending!="run" && pending!="band" && pending!="cv" && pending!="cf") throw std::invalid_argument("Wait for the project operation before selecting a device");}
            const auto id=r->at("device_id").get<std::string>(); (void)w.Device(id);
            if(!w.edits.empty()) throw std::invalid_argument("Wait for queued edits before selecting a device");
            w.project.active_device=id; w.projectDirty=true;w.RefreshDirty();w.response=w.Snapshot(); return {};
        }
        if(op=="auto_recalculate") {
            const bool automatic=r->at("enabled").get<bool>(), band=r->value("band_enabled",w.autoRecalculateBand);
            w.autoRecalculate=automatic;w.autoRecalculateBand=band;
            if(w.autoRecalculate || w.autoRecalculateBand) w.Changed();
            w.response={{"ok",true},{"auto_recalculate",w.autoRecalculate},{"auto_recalculate_band",w.autoRecalculateBand},{"error",nullptr}};
            w.status=(w.autoRecalculate || w.autoRecalculateBand)?"Auto-recalculate enabled (250 ms debounce)":"Auto-recalculate disabled"; return {};
        }
        if(w.active && w.pendingDevice==w.project.active_device && op=="edit" && (w.pending->request.at("operation")=="load" || w.pending->request.at("operation")=="project_load" || w.pending->request.at("operation")=="layer"))
            return StructuredError{ErrorCategory::Validation,Severity::Error,"Busy","Project structure is changing; wait before editing","Wait for the operation","ScapsLab","busy"};
        if(w.Busy() && op=="edit") {
            auto candidate=w.Current().document;
            if(w.pending && w.pendingDevice==w.project.active_device && w.pending->request.at("operation")=="edit") {
                const auto &edit=w.pending->request;
                if(edit.contains("document")) candidate=edit.at("document");else candidate.update(edit.at("patch"),true);
            }
            if(!w.edits.empty()) candidate=w.edits.back().at("document");
            if(r->contains("document")) candidate=r->at("document");else candidate.update(r->at("patch"),true);
            auto edit=*r;edit["document"]=std::move(candidate);edit["device_id"]=w.project.active_device;
            const auto gesture=edit.value("gesture_id",std::string{});
            if(!gesture.empty() && !w.edits.empty() && w.edits.back().value("gesture_id",std::string{})==gesture) w.edits.back()=std::move(edit);
            else w.edits.push_back(std::move(edit));
            if(w.active && (w.pending->request.at("operation")=="run" || w.pending->request.at("operation")=="band" || w.pending->request.at("operation")=="cv" || w.pending->request.at("operation")=="cf" || w.pending->request.at("operation")=="qe")) (void)w.jobs->RequestCancel(w.active);
            w.Current().changedAt=std::chrono::steady_clock::now(); return {};
        }
        if (w.Busy()) return StructuredError{ErrorCategory::Validation,Severity::Error,"Busy","A job is still active","Wait or cancel","ScapsLab","busy"};
        if(op=="undo" || op=="redo") {
            auto &stack=w.preferProjectUndo?w.projectUndo:w.Current().undo;
            const auto result=op=="undo"?stack.Undo():stack.Redo();
            if(!result) return result;
            w.status=w.active?"Applying history":"History restored";
            w.RefreshDirty();w.response=w.Snapshot();return {};
        }
        if(op=="device_add" && r->contains("path")) {auto read=*r;read["operation"]="device_read";w.StartOperation(read,w.project.active_device);return {};}
        if(op.starts_with("device_") || op=="project_edit" || op=="action_settings") {w.DeviceOp(*r);return {};}
        if(op=="run") {w.Current().needsRecalculate=false;w.Current().needsBand=false;}
        if(op=="band") w.Current().needsBand=false;
        if(op=="run_all") {
            w.batchTotal=0;w.batchCompleted=0;w.batchResponse=Json::array();
            for(const auto &d:w.project.devices) if(w.project.compare_set.contains(d->id)) w.runQueue.push_back(d->id);
            w.batchTotal=w.runQueue.size();
            if(w.runQueue.empty()) throw std::invalid_argument("Select at least one device for compare");
            const auto id=w.runQueue.front();w.runQueue.pop_front();w.StartOperation({{"operation","run"},{"batch",true}},id);return {};
        }
        w.StartOperation(*r,w.project.active_device);return {};
    }
};
}
namespace {
Json SolverDocument(Json document) {
    for(auto &layer:document["layers"]) {
        if(layer.contains("grading"))for(auto &profile:layer["grading"])profile.erase("scapslab_warnings");
        for(auto &defect:layer["defects"])if(defect.contains("density_profile_m3"))defect["density_profile_m3"].erase("scapslab_warnings");
    }
    return document;
}
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
    Temporary t; SaveText(t.path,Text(SolverDocument(j))); auto p=scaps_core::load_device_project_yaml(t.path);
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
Json AM15Spectrum() {
    return Json::array({
#include "AM15.inc"
    });
}
Json SelfContained(Json j) {
    auto embed=[](Json &object,const char *file,const char *table,auto reader) {
        if(object.contains(file) && object.at(file).is_string() && !object.at(file).get<std::string>().empty()) {
            const auto points=reader(object.at(file).get<std::string>());object[std::string("scapslab_")+file]=object.at(file);object[table]=points;object[file]="";
        }
    };
    if(j.contains("illumination")) {
        auto &l=j["illumination"];
        embed(l,"spectrum_file","spectrum_nm_W_m2",scaps_core::read_scaps_spectrum);
        embed(l,"generation_file","generation_m_m3s",scaps_core::read_scaps_generation);
        embed(l,"front_filter_file","front_filter_nm_fraction",scaps_core::read_scaps_filter);
        embed(l,"back_filter_file","back_filter_nm_fraction",scaps_core::read_scaps_filter);
    }
    for(auto &l:j["layers"]) {
        for(auto &d:l["defects"]) {
            if(d.contains("density_profile_m3") && d["density_profile_m3"].contains("file")) {
                auto &g=d["density_profile_m3"];const auto previous=g;g=ProfileFromFile(g.at("file"),true);for(const auto &[key,value]:previous.items())if(key!="file")g[key]=value;
            }
            if(d.contains("optical")){auto &o=d["optical"];embed(o,"electron_file","electron_table_nm_m2",scaps_core::read_scaps_absorption);embed(o,"hole_file","hole_table_nm_m2",scaps_core::read_scaps_absorption);}
        }
        if(l.contains("absorption")) {
            embed(l["absorption"],"file","table_nm_m1",scaps_core::read_scaps_absorption);
            if(l["absorption"].contains("composition")){auto &c=l["absorption"]["composition"];embed(c,"file_a","table_a_nm_m1",scaps_core::read_scaps_absorption);embed(c,"file_b","table_b_nm_m1",scaps_core::read_scaps_absorption);}
        }
        if(l.contains("grading")) for(auto it=l["grading"].begin();it!=l["grading"].end();++it) if(it->contains("file")) {
            auto &g=it.value();const auto key=it.key();
            const auto profile=scaps_core::read_grading_file(g.at("file").get<std::string>(),key=="Nd_m3" || key=="Na_m3" || key=="Nc_ref_m3" || key=="Nv_ref_m3");
            if(!g.contains("position_relative"))g["position_relative"]=profile.position_relative;
            if(!g.contains("interpolation_logarithmic"))g["interpolation_logarithmic"]=profile.interpolation_logarithmic;
            if(!g.contains("shared_range"))g["shared_range"]=profile.shared_range;
            if(!g.contains("composition_dependent"))g["composition_dependent"]=profile.composition_dependent;
            if(!g.contains("source_file"))g["source_file"]=g.at("file");g.erase("file");g["law"]="table";g["coordinate"]=profile.coordinate;g["value"]=profile.value;
        }
    }
    return j;
}
scaps_core::Profile ProfileFromJson(const Json &spec,bool logarithmic) {
    auto p=spec.contains("file")?scaps_core::read_grading_file(spec.at("file"),logarithmic):scaps_core::Profile{};
    p.law=scaps_core::grading_law_from_string(spec.value("law",std::string(spec.contains("file")?"table":"uniform")));
    for(const auto &[key,field]:std::initializer_list<std::pair<const char*,double scaps_core::Profile::*>>{
        {"left",&scaps_core::Profile::left},{"right",&scaps_core::Profile::right},{"bowing",&scaps_core::Profile::bowing},
        {"midpoint",&scaps_core::Profile::midpoint},{"middle",&scaps_core::Profile::middle},{"background",&scaps_core::Profile::background},
        {"length_left",&scaps_core::Profile::length_left},{"length_right",&scaps_core::Profile::length_right},
        {"beta_a",&scaps_core::Profile::beta_a},{"beta_b",&scaps_core::Profile::beta_b},{"power",&scaps_core::Profile::power},
        {"gaussian_center",&scaps_core::Profile::gaussian_center},{"gaussian_width",&scaps_core::Profile::gaussian_width},
        {"diffusion_depth_m",&scaps_core::Profile::diffusion_depth_m},{"projected_range_m",&scaps_core::Profile::projected_range_m},
        {"dose_m2",&scaps_core::Profile::dose_m2},{"surface_density_m3",&scaps_core::Profile::surface_density_m3}}) p.*field=spec.value(key,p.*field);
    p.coordinate=spec.value("coordinate",p.coordinate);p.value=spec.value("value",p.value);
    p.position_relative=spec.value("position_relative",p.position_relative);p.interpolation_logarithmic=spec.value("interpolation_logarithmic",p.interpolation_logarithmic);
    p.shared_range=spec.value("shared_range",p.shared_range);p.composition_dependent=spec.value("composition_dependent",p.composition_dependent);
    p.shared_offset_m=spec.value("shared_offset_m",0.);p.shared_thickness_m=spec.value("shared_thickness_m",0.);
    scaps_core::validate_profile(p);return p;
}
Json ProfileFromFile(const std::string &path,bool logarithmic) {
    const auto p=scaps_core::read_grading_file(path,logarithmic);
    std::ifstream input(path);std::string line;std::vector<double> coordinates;
    while(std::getline(input,line)){std::istringstream row(line);double x,y;if(row>>x>>y)coordinates.push_back(x);}
    Json warnings=Json::array();if(!std::is_sorted(coordinates.begin(),coordinates.end()) || coordinates.size()!=p.coordinate.size())warnings.push_back("Sorted profile points; kept the first occurrence of duplicate coordinates.");
    return {{"scapslab_warnings",warnings},{"law","table"},{"coordinate",p.coordinate},{"value",p.value},{"position_relative",p.position_relative},
        {"interpolation_logarithmic",p.interpolation_logarithmic},{"shared_range",p.shared_range},{"source_file",path}};
}
Json ProfileSamples(const Json &spec,double thickness,const Json &composition) {
    Positive(thickness,"thickness_m");const auto p=ProfileFromJson(spec);
    const auto c=composition.empty()?scaps_core::Profile{}:ProfileFromJson(composition);
    Json x=Json::array(),y=Json::array();
    for(int i=0;i<=200;++i) {
        const double at=thickness*i/200.;x.push_back(at);
        const double coordinate=!composition.empty()?scaps_core::evaluate_position_profile(c,at,thickness):at/thickness;
        y.push_back(p.composition_dependent?scaps_core::evaluate_profile(p,coordinate):scaps_core::evaluate_position_profile(p,at,thickness));
    }
    return {{"x_m",x},{"value_SI",y},{"source_file",spec.value("source_file",std::string{})},{"source_point_count",spec.value("coordinate",Json::array()).size()}};
}
Json ResolveSharedProfile(Json spec,const Json &document,const std::string &layerId,const std::string &parameter) {
    if(!spec.value("shared_range",false))return spec;
    const auto device=scaps_core::make_device_from_project(Model(document));
    for(std::size_t i=0;i<device.layers.size();++i)if(document.at("layers")[i].at("scapslab_id")==layerId && device.layers[i].grading) {
        const auto &g=*device.layers[i].grading;
        const auto &p=parameter=="composition"?g.composition:parameter=="Na_m3"?g.Na_m3:g.Nd_m3;
        if(p){spec["left"]=p->left;spec["right"]=p->right;spec["shared_range"]=false;spec["shared_offset_m"]=p->shared_offset_m;spec["shared_thickness_m"]=p->shared_thickness_m;}
    }
    return spec;
}
void ExportProfile(const Json &samples,const std::string &path) {
    const bool grd=std::filesystem::path(path).extension()==".grd";
    std::ostringstream out;out<<std::setprecision(17);
    if(grd)out<<"position: absolute\ninterpolation: linear\nrange: this layer only\n";
    else out<<"x_m,"<<samples.value("parameter_id",std::string("value_SI"))<<'\n';
    for(std::size_t i=0;i<samples.at("x_m").size();++i) out<<samples.at("x_m")[i].get<double>()*(grd?1e6:1.)<<(grd?" ":",")<<samples.at("value_SI")[i].get<double>()<<'\n';
    SaveText(path,out.str());
}
Json ReadProject(const std::string &path) {
    auto raw=FromYaml(YAML::LoadFile(path));
    if(raw.value("schema_version",0)==0) raw["schema_version"]=1;
    const auto base=std::filesystem::absolute(path).parent_path();
    auto anchor=[&](Json &object,const char *key) {if(object.contains(key) && object[key].is_string() && !object[key].get<std::string>().empty()) object[key]=(base/std::filesystem::path(object[key].get<std::string>())).lexically_normal().string();};
    if(raw.contains("illumination")) for(const auto *key:{"spectrum_file","generation_file","front_filter_file","back_filter_file"}) anchor(raw["illumination"],key);
    for(auto &layer:raw["layers"]) {anchor(layer,"material_file");if(layer.contains("absorption")) anchor(layer["absorption"],"file");if(layer.contains("grading")) for(auto &profile:layer["grading"]) anchor(profile,"file");}
    for(auto &layer:raw["layers"])if(layer.contains("absorption") && layer["absorption"].contains("composition")) {anchor(layer["absorption"]["composition"],"file_a");anchor(layer["absorption"]["composition"],"file_b");}
    for(auto &layer:raw["layers"])for(auto &d:layer["defects"]) {
        if(d.contains("density_profile_m3"))anchor(d["density_profile_m3"],"file");
        if(d.contains("optical")){anchor(d["optical"],"electron_file");anchor(d["optical"],"hole_file");}
    }
    if(raw.contains("mesh")) anchor(raw["mesh"],"scaps_numerical_definition");
    raw=SelfContained(std::move(raw));
    const auto p=Model(raw);
    Temporary t; scaps_core::save_device_project_yaml(p,t.path);
    auto full=FromYaml(YAML::LoadFile(t.path.string()));
    // JSON array updates replace layers; retain the core defaults within each layer.
    for(auto &l:raw["layers"]) l.erase("material_file");
    if(raw.contains("mesh")) raw["mesh"].erase("scaps_numerical_definition");
    for(std::size_t i=0;i<raw["layers"].size();++i) full["layers"][i].update(raw["layers"][i],true);
    raw["layers"]=full["layers"];full.update(raw,true);
    for(const auto *side:{"left","right"}) if(!raw["contacts"][side].contains("flat_band")) full["contacts"][side].erase("flat_band");
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
Json NormalizeDocument(Json document) {
    // Older workspace documents bypass ReadProject's Core defaults.
    if(!document.contains("illumination") || !document.at("illumination").contains("mode") ||
        !document.at("illumination").contains("intensity_multiplier") || !document.at("illumination").contains("uniform_generation_m3_s") ||
        !document.at("illumination").contains("front_transmission") || !document.at("illumination").contains("wavelength_nm")) {
        const auto model=Model(document);Temporary t;scaps_core::save_device_project_yaml(model,t.path);
        auto light=FromYaml(YAML::LoadFile(t.path.string())).at("illumination");
        if(document.contains("illumination")) light.update(document.at("illumination"),true);
        document["illumination"]=std::move(light);
    }
    return document;
}
Json NewDeviceDocument() {auto d=ReadProject(SCAPSLAB_PROJECT);for(const auto *side:{"left","right"}) d["contacts"][side]["flat_band"]=true;return d;}
void WriteProject(const Json &j,const std::string &path) { (void)Model(j); SaveText(Path{path}.Native(),Text(SolverDocument(j))); }
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
Json GenerationRecombination(const Json &document,const scaps_core::DeviceProject &p,const scaps_core::Device1D &device,const scaps_core::DeviceState &state) {
    using namespace scaps_core;
    const auto mesh=mesh_from_coordinates(device,state.x_m);
    const auto pc=physical_constants(p.constants_mode);
    const auto generation=generation_integrals_m2s(device,mesh,pc);
    Json out={{"channels",Json::array()},{"layers",Json::array()},{"x_m",state.x_m},{"generation_m3_s",Json::array()}};
    double generated=0,recombined=0;
    for(std::size_t l=0;l<p.layers.size();++l) out["layers"].push_back({{"layer_id",document.at("layers")[l].at("scapslab_id")},{"generation_current_A_m2",0.},{"recombination_current_A_m2",0.},{"mechanisms",Json::object()}});
    for(std::size_t i=0;i<generation.size();++i) {
        const double dx=.5*((i?mesh.edges[i-1].h_m:0)+(i+1<mesh.size()?mesh.edges[i].h_m:0));
        out["generation_m3_s"].push_back(dx>0?generation[i]/dx:0.);
        const double j=pc.q_C*generation[i];generated+=j;
        auto &value=out["layers"][mesh.nodes[i].layer_index]["generation_current_A_m2"];value=value.get<double>()+j;
    }
    for(const auto &channel:state.recombination) {
        std::string id=channel.id;int layer=-1,defect=-1;
        if(channel.mechanism=="srh_bulk" && std::sscanf(id.c_str(),"layer:%d/defect:%d",&layer,&defect)==2 && layer>=0 && defect>=0 && layer<int(p.layers.size()) && defect<int(document.at("layers")[layer].at("defects").size()))
            id=document.at("layers")[layer].at("defects")[defect].at("scapslab_id");
        Json byLayer=Json::array();double sum=0;
        for(std::size_t l=0;l<p.layers.size();++l) {
            double value=0;
            for(std::size_t i=0;i<channel.R_m3_s.size();++i) if(mesh.nodes[i].layer_index==l) {
                const double dx=.5*((i?mesh.edges[i-1].h_m:0)+(i+1<mesh.size()?mesh.edges[i].h_m:0));
                value+=pc.q_C*dx*channel.R_m3_s[i];
            }
            if(channel.mechanism=="interface") {
                // Sheet loss belongs to the left layer; never count it twice.
                for(std::size_t i=0;i+1<mesh.size();++i) if(mesh.x_m[i]==channel.x_m.front() && mesh.edges[i].h_m==0 && mesh.nodes[i].layer_index==l) value+=channel.integrated_current_A_m2;
            }
            sum+=value;byLayer.push_back({{"layer_id",document.at("layers")[l].at("scapslab_id")},{"current_A_m2",value}});
            auto &row=out["layers"][l];row["recombination_current_A_m2"]=row["recombination_current_A_m2"].get<double>()+value;
            auto &mechanisms=row["mechanisms"];mechanisms[channel.mechanism]=mechanisms.value(channel.mechanism,0.)+value;
        }
        recombined+=channel.integrated_current_A_m2;
        out["channels"].push_back({{"id",id},{"engine_id",channel.id},{"mechanism",channel.mechanism},{"x_m",channel.x_m.empty()?state.x_m:channel.x_m},
            {"R_m3_s",channel.R_m3_s},{"R_sheet_m2_s",channel.R_sheet_m2_s},{"integrated_current_A_m2",channel.integrated_current_A_m2},{"by_layer",byLayer},
            {"layer_sum_relative_error",std::abs(sum-channel.integrated_current_A_m2)/std::max(1e-30,std::abs(channel.integrated_current_A_m2))}});
    }
    out["generation_current_A_m2"]=generated;out["recombination_current_A_m2"]=recombined;out["net_generation_current_A_m2"]=generated-recombined;
    out["contact_recombination"]=Json::array();
    for(const auto &c:state.contact_recombination) out["contact_recombination"].push_back({{"id",c.id},{"electron_current_A_m2",c.electron_current_A_m2},{"hole_current_A_m2",c.hole_current_A_m2}});
    out["sheet_layer_convention"]="interface assigned to left layer";
    return out;
}

}
void Operation::Execute(JobContext &ctx) {
    using namespace scaps_core;
    const auto started=std::chrono::steady_clock::now();
    ctx.ThrowIfCancellationRequested(); const std::string op=request.at("operation");
    if(op=="load_material" || op=="save_material") {
        auto model=Model(document);std::size_t index=0;
        while(index<document.at("layers").size() && document.at("layers")[index].at("scapslab_id")!=request.at("layer_id"))++index;
        if(index==model.layers.size())throw std::invalid_argument("Unknown layer_id");
        const auto selected=request.value("selection",std::string("both"));
        if(selected!="both" && selected!="A" && selected!="B")throw std::invalid_argument("selection must be A, B or both");
        const auto selection=selected=="A"?MaterialSelection::PureA:selected=="B"?MaterialSelection::PureB:MaterialSelection::Both;
        if(op=="save_material")write_scaps_material(make_scaps_material(model.layers[index]),request.at("path").get<std::string>(),selection);
        else {
            apply_scaps_material(read_scaps_material(request.at("path").get<std::string>()),model.layers[index],selection);
            Temporary t;save_device_project_yaml(model,t.path);const auto full=FromYaml(YAML::LoadFile(t.path.string()));
            auto candidate=document;auto &layer=candidate["layers"][index];
            for(const auto *key:{"material","grading","absorption","recombination"}){layer.erase(key);if(full.at("layers")[index].contains(key))layer[key]=full.at("layers")[index].at(key);}
            (void)Model(candidate);document=std::move(candidate);
        }
        output={{"path",request.at("path")},{"selection",selected}};return;
    }
    if(op=="set_profile" || op=="load_profile" || op=="preview_profile" || op=="export_profile") {
        auto candidate=document;Json *target=nullptr;Json composition=Json::object();double thickness=0.;
        const auto parameter=request.at("parameter_id").get<std::string>();
        if(parameter=="generation_m3_s") {
            for(const auto &l:candidate.at("layers"))thickness+=l.at("thickness_m").get<double>();
            target=&candidate["illumination"];
        } else {
            for(auto &layer:candidate["layers"]) if(layer.at("scapslab_id")==request.at("layer_id")) {
                thickness=layer.at("thickness_m");composition=layer.value("grading",Json::object()).value("composition",Json::object());
                if(parameter=="Nt_m3") {
                    for(auto &d:layer["defects"])if(d.at("scapslab_id")==request.at("defect_id"))target=&d["density_profile_m3"];
                } else {
                    const std::set<std::string> supported={"composition","Eg_eV","chi_eV","eps_r","Nd_m3","Na_m3","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","vth_n_ref_m_s","vth_p_ref_m_s"};
                    if(!supported.contains(parameter))throw std::invalid_argument("Unsupported parameter_id");
                    target=&layer["grading"][parameter];
                }
            }
        }
        if(!target)throw std::invalid_argument("Unknown layer_id or defect_id");
        if(op=="set_profile" || op=="load_profile") {
            if(parameter=="generation_m3_s") {
                (*target)["mode"]="generation_file";
                (*target)["generation_m_m3s"]=op=="load_profile"?Json(read_scaps_generation(request.at("path"))):request.at("generation_m_m3s");
                (*target)["generation_file"]="";
                if(op=="load_profile")(*target)["scapslab_generation_file"]=request.at("path");
            } else *target=op=="load_profile"?ProfileFromFile(request.at("path"),parameter.find("_m3")!=std::string::npos):request.at("profile");
            if(op=="load_profile" && target->contains("source_file") && !request.value("project_path",std::string{}).empty()) {
                std::error_code ec;const auto relative=std::filesystem::relative(request.at("path").get<std::string>(),std::filesystem::path(request.at("project_path").get<std::string>()).parent_path(),ec);if(!ec)(*target)["source_file"]=relative.generic_string();
            }
            candidate=SelfContained(std::move(candidate));(void)Model(candidate);document=std::move(candidate);output={{"parameter_id",parameter}};
        } else {
            if(parameter=="generation_m3_s") {
                Json spec={{"law","table"},{"position_relative",false},{"coordinate",Json::array()},{"value",Json::array()}};
                for(const auto &point:target->at("generation_m_m3s")){spec["coordinate"].push_back(point[0].get<double>()*1e6);spec["value"].push_back(point[1]);}
                output=ProfileSamples(spec,thickness);
            } else {
                if(target->is_null())throw std::invalid_argument("Profile is not active");auto spec=*target;
                if(parameter!="composition" && parameter!="Nd_m3" && parameter!="Na_m3" && parameter!="Nt_m3")spec["composition_dependent"]=true;
                output=ProfileSamples(ResolveSharedProfile(spec,document,request.at("layer_id"),parameter),thickness,ResolveSharedProfile(composition,document,request.at("layer_id"),"composition"));
            }
            output["parameter_id"]=parameter;
            output["value_unit"]=parameter=="generation_m3_s"?"m^-3 s^-1":parameter.find("_m3")!=std::string::npos?"m^-3":parameter.find("_eV")!=std::string::npos?"eV":parameter.find("_m2_V_s")!=std::string::npos?"m^2/(V s)":parameter.find("vth_")==0?"m/s":"1";
            if(op=="export_profile")ExportProfile(output,request.at("path"));
        }
        return;
    }

    if(op=="export_results") output=ExportResults(workspace,request);
    else if(BatchOperation(*this,ctx)) {}
    else if(op=="export_gr") {
        if(!workspace.contains("points") || workspace.at("points").empty()) throw std::invalid_argument("No G/R snapshot; calculate a working point first");
        if(request.contains("point_index") && !request.at("point_index").is_number_integer()) throw std::invalid_argument("point_index must be an integer");
        const auto index=request.value<std::int64_t>("point_index",0);
        if(index<0 || std::uint64_t(index)>=workspace.at("points").size()) throw std::invalid_argument("Invalid point_index");
        const auto &point=workspace.at("points")[index];
        if(!point.contains("generation_recombination")) throw std::invalid_argument("Snapshot has no G/R; recalculate");
        output={{"schema_version",1},{"device_id",request.at("device_id")},{"point",point},{"provenance",workspace.at("provenance")},{"project_snapshot",workspace.at("project_snapshot")}};
        const auto format=request.value("format",std::string("json"));
        std::string text;
        if(format=="json") text=output.dump(2);
        else if(format=="csv") {
            std::ostringstream csv;csv<<std::setprecision(17);
            csv<<"device_id,bias_id,voltage_V,internal_voltage_V,temperature_K,valid,validity_status,illumination_json,provenance_json,channel_id,engine_id,mechanism,layer_id,x_m,rate_m3_s,sheet_rate_m2_s,integrated_current_A_m2,curve_valid,curve_validity_status,terminal_budget_valid,terminal_budget_relative_error\n";
            auto quoted=[](const std::string &v){std::string out="\"";for(char c:v){out+=c;if(c=='"')out+='"';}return out+"\"";};
            const auto &gr=point.at("generation_recombination");
            auto row=[&](const std::string &id,const std::string &engine,const std::string &mechanism,const std::string &layer,const Json &x,const Json &rate,const Json &sheet,const Json &integral,const Json &health) {
                csv<<quoted(request.at("device_id"))<<','<<quoted(point.at("bias_id"))<<','<<point.at("voltage_V")<<','<<point.at("internal_voltage_V")<<','<<workspace.at("project_snapshot").at("temperature_K")<<','<<point.at("valid")<<','<<quoted(point.at("validity_status"))<<','<<quoted(workspace.at("project_snapshot").value("illumination",Json::object()).dump())<<','<<quoted(workspace.at("provenance").dump())<<','<<quoted(id)<<','<<quoted(engine)<<','<<quoted(mechanism)<<','<<quoted(layer)<<',';
                int column=0;for(const auto &v:{x,rate,sheet,integral}) {if(column++)csv<<',';if(v.is_number())csv<<v;}csv<<','<<health.at("valid")<<','<<quoted(health.at("validity_status"))<<','<<gr.at("terminal_budget_valid")<<',';if(gr.at("terminal_budget_relative_error").is_number())csv<<gr.at("terminal_budget_relative_error");csv<<'\n';
            };
            for(std::size_t i=0;i<gr.at("x_m").size();++i) row("generation","","generation","",gr.at("x_m")[i],gr.at("generation_m3_s")[i],nullptr,nullptr,point);
            for(const auto &c:gr.at("channels")) {
                const bool sheet=!c.at("R_sheet_m2_s").empty();
                for(std::size_t i=0;i<c.at("x_m").size();++i) row(c.at("id"),c.at("engine_id"),c.at("mechanism"),"",c.at("x_m")[i],sheet?Json(nullptr):c.at("R_m3_s")[i],sheet?c.at("R_sheet_m2_s")[i]:Json(nullptr),nullptr,c);
                for(const auto &l:c.at("by_layer")) row(c.at("id"),c.at("engine_id"),c.at("mechanism"),l.at("layer_id"),nullptr,nullptr,nullptr,l.at("current_A_m2"),c);
            }
            for(const auto &c:gr.value("contact_recombination",Json::array())) for(const auto *carrier:{"electron","hole"})
                row("contact:"+c.at("id").get<std::string>()+":"+carrier,"","contact","",nullptr,nullptr,nullptr,c.at(std::string(carrier)+"_current_A_m2"),point);
            for(const auto *key:{"shunt_current_A_m2","series_current_A_m2","predicted_terminal_current_A_m2"}) if(gr.contains(key)) row(key,"","terminal_budget","",nullptr,nullptr,nullptr,gr.at(key),point);
            text=csv.str();
        } else throw std::invalid_argument("format must be csv or json");
        SaveText(Path{request.at("path").get<std::string>()}.Native(),text);
    }
    else if(op=="import_def") {
        Temporary temporary; const auto imported=import_def(request.at("path").get<std::string>(),request.value("scaps_root",std::string{}));
        save_device_project_yaml(imported,temporary.path);document=ReadProject(temporary.path.string());
        output={{"warnings",imported.scaps_native.warnings}};
    } else if(op=="export_def") {export_def(Model(document),request.at("path").get<std::string>());output={{"warnings",Model(document).scaps_native.warnings}};}
    else if(op=="project_new") {
        workspace=SingleDeviceWorkspace(NewDeviceDocument(),"Untitled project");
    }
    else if(op=="project_load" || op=="load") {
        const auto file=request.at("path").get<std::string>();
        std::string source,error;if(!TextFileIO::Load(Path{file},source,error)) throw std::runtime_error(error);
        auto raw=source.find_first_not_of(" \t\r\n")!=std::string::npos && source[source.find_first_not_of(" \t\r\n")]=='{'?Json::parse(source):FromYaml(YAML::Load(source));
        if(raw.contains("devices")) {workspace=UnpackResults(raw);workspace["schema_version"]=raw.value("schema_version",0);workspace=NormalizeWorkspace(std::move(workspace));}
        else workspace=SingleDeviceWorkspace(ReadProject(file),std::filesystem::path(file).stem().string());
    }
    else if(op=="project_save" || (op=="save" && std::filesystem::path(request.at("path").get<std::string>()).extension()==".scapslab")) {
        if(!shared.empty()) {auto &devs=workspace["devices"];for(std::size_t i=0;i<shared.size();++i) {auto &d=devs[i];d["history"]=*shared[i][0];d["result"]=*shared[i][1];d["band_result"]=*shared[i][2];d["ac_results"]=*shared[i][3];d["batch_result"]=*shared[i][4];}shared.clear();}
        ValidateWorkspace(workspace);
        if(!request.value("include_results",true)) for(auto &d:workspace["devices"]) for(const auto *key:{"result","band_result","ac_results","batch_result"}) d[key]=Json::object();
        if(!request.value("include_results",true)) for(auto &d:workspace["devices"]) {d["status"]="no results";d["history"]=Json::array();d["selected_run"]="";}
        CompactHistory(workspace);
        const auto text=WorkspaceText(PackResults(workspace));
        ctx.ThrowIfCancellationRequested();SaveText(Path{request.at("path").get<std::string>()}.Native(),text);
        output={{"path",std::filesystem::absolute(request.at("path").get<std::string>()).string()},{"size_bytes",text.size()},{"save_status","saved"}};
    }
    else if(op=="device_read") document=ReadProject(request.at("path").get<std::string>()); // device_add from a file: only the read runs as a job
    else if (op=="save") WriteProject(document,request.at("path").get<std::string>());
    else if (op=="edit") { auto candidate=document; const auto &patch=request.contains("document")?request.at("document"):request.at("patch");
        if (!patch.is_object()) throw std::invalid_argument("patch must be an object");
        if(request.contains("document")) candidate=patch; else candidate.update(patch,true);
        if(request.contains("pointer_set")) for(const auto &s:request.at("pointer_set")) candidate[Json::json_pointer(s.at("path").get<std::string>())]=s.at("value"); // tutorials: "Show me" for array fields
        if(patch.contains("illumination") && patch.at("illumination").value("scapslab_spectrum",std::string{})=="AM1.5G") {
            auto &light=candidate["illumination"];light["mode"]="spectrum";light["spectrum_file"]="";light["spectrum_nm_W_m2"]=AM15Spectrum();
        }
        candidate=NormalizeDocument(SelfContained(std::move(candidate)));(void)Model(candidate); document=std::move(candidate);
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
        document=SelfContained(std::move(document));(void)Model(document);
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
        const auto p=Model(document);const auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
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
        const auto total=op=="cv"?volts.size():frequencies.size();
        auto finite=[](double x)->Json {return std::isfinite(x)?Json(x):Json(nullptr);};
        // One DC continuation (C-V) or one operating point (C-f); AC solves are independent, so run them in parallel.
        std::vector<IVPoint> dcs;
        ctx.SetStage("DC operating points");
        if(op=="cv") dcs=solve_iv(device,mesh,volts,T,p.solver,pc);
        else dcs=solve_iv(device,mesh,std::vector<double>(volts.begin(),volts.begin()+1),T,p.solver,pc);
        ctx.ThrowIfCancellationRequested();
        std::vector<ACPoint> acs(total);
        ctx.SetStage("AC solve ("+std::to_string(total)+" points)");
        if(op=="cf") acs=solve_ac_spectrum(device,mesh,dcs.front(),frequencies,1e-3,pc); // core parallelises the frequencies
        else {
            ParallelFor(ctx,total,[&](std::size_t i){acs[i]=solve_ac_spectrum(device,mesh,dcs[i],{frequencies[0]},1e-3,pc).front();});
        }
        ctx.ThrowIfCancellationRequested();
        for(std::size_t i=0;i<total;++i) {
            const auto &dc=dcs[op=="cv"?i:0];const auto &ac=acs[i];
            output["points"].push_back({{"point_id",op+"-"+std::to_string(i)},{"voltage_V",-ac.voltage_V},{"terminal_voltage_V",-dc.voltage_V},{"internal_voltage_V",-dc.state.applied_voltage_V},{"frequency_Hz",ac.frequency_Hz},{"temperature_K",T},
                {"capacitance_F",finite(ac.C_F)},{"conductance_S",finite(ac.G_S)},{"G_over_omega_F",finite(ac.G_S/(2*std::numbers::pi*ac.frequency_Hz))},
                {"inverse_C2_F_minus2",finite(1/(ac.C_F*ac.C_F))},{"status",to_string(ac.status)},{"conservation_relative",finite(ac.current_conservation_relative)}});
            ctx.SetProgress(float(i+1),float(total));
        }
    } else if(op=="qe") {
        const auto p=Model(document);auto device=make_device_from_project(p);const auto pc=physical_constants(p.constants_mode);
        const double first=request.value("start_nm",350.),last=request.value("stop_nm",1100.);
        const auto countValue=request.value("points",Json(31));if(!countValue.is_number_integer() || countValue.get<double>()<2 || countValue.get<double>()>1001) throw std::invalid_argument("QE points must be an integer in 2..1001");
        const int count=countValue.get<int>();const double probe=request.value(request.value("probe_mode",std::string("constant_flux"))=="constant_flux"?"probe_photon_flux_m2_s":"probe_power_W_m2",request.value("probe_mode",std::string("constant_flux"))=="constant_flux"?1e16:1e-3);
        const auto mode=request.value("probe_mode",std::string("constant_flux"));
        if(!std::isfinite(first) || !std::isfinite(last) || first<=0 || last<=first || count<2 || count>1001) throw std::invalid_argument("Invalid QE wavelength range (2..1001 points)");
        Positive(probe,"probe_value");if(mode!="constant_flux" && mode!="constant_power") throw std::invalid_argument("Unknown QE probe_mode");
        const auto mesh=prepare_mesh(device,p.mesh,{0.},{p.temperature_K},p.solver,pc);
        auto dark=device;dark.illumination.enabled=false;
        output={{"request",request},{"kind","qe"},{"mesh_x_m",mesh.x_m},{"points",Json::array()},{"probe_mode",mode},{mode=="constant_flux"?"probe_photon_flux_m2_s":"probe_power_W_m2",probe},{"temperature_K",p.temperature_K},{"voltage_V",0.},{"background","dark"}};
        OpticalTable response;
        std::vector<double> wavelengths;for(int i=0;i<count;++i) wavelengths.push_back(first+(last-first)*i/(count-1));
        ctx.SetStage("QE sweep ("+std::to_string(count)+" wavelengths)");
        const auto qes=solve_qe(dark,mesh,wavelengths,mode,probe,0.,p.temperature_K,p.solver,pc);ctx.ThrowIfCancellationRequested();
        for(int i=0;i<count;++i) {
            const double wavelength=wavelengths[i];const auto &q=qes[i];
            output["points"].push_back({{"point_id","qe-"+std::to_string(i)},{"voltage_V",0.},{"terminal_voltage_V",0.},{"internal_voltage_V",std::isfinite(q.internal_voltage_V)?Json(-q.internal_voltage_V):Json(nullptr)},{"temperature_K",p.temperature_K},{"wavelength_nm",q.wavelength_nm},{"EQE",std::isfinite(q.EQE)?Json(q.EQE):Json(nullptr)},
                {"incident_photon_flux_m2_s",q.incident_photon_flux_m2_s},{"probe_power_W_m2",q.probe_power_W_m2},{"status",to_string(q.status)}});
            response.emplace_back(wavelength,q.EQE);ctx.SetProgress(float(i+1),float(count));
        }
        bool covered=true;double integrated=0;
        auto add=[&](double wavelength,double power) {
            if(wavelength<device.illumination.wavelength_min_nm || wavelength>device.illumination.wavelength_max_nm) return;
            if(wavelength<first || wavelength>last) {if(power>0) covered=false;return;}
            integrated+=pc.q_C*power*wavelength*(pc.q_C==1.602e-19?5.035e15:1e-9/(6.62607015e-34*299792458.))*optical_interpolate(response,wavelength,false);
        };
        const auto &light=device.illumination;
        if(light.enabled && light.mode=="spectrum") for(const auto &[w,power]:light.spectrum_nm_W_m2) add(w,power*light.intensity_multiplier);
        else if(light.enabled && light.mode=="monochromatic") add(light.wavelength_nm,light.monochromatic_power_W_m2*light.intensity_multiplier);
        else covered=false;
        const auto sc=solve_iv(device,mesh,{0.},p.temperature_K,p.solver,pc).front();
        output["integrated_Jsc_A_m2"]=covered && std::isfinite(integrated)?Json(integrated):Json(nullptr);
        output["light_Jsc_A_m2"]=sc.status==SolverStatus::Converged?Json(-sc.J_total_Am2):Json(nullptr);
        output["integration_note"]="Dark differential EQE weighted by incident spectrum bins; full spectral coverage required. Finite-light nonlinear response can differ.";
    } else if (op=="run" || op=="band") {
        const auto p=Model(document);const auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
        auto volts=op=="band"?std::vector<double>{request.at("voltage_V").get<double>()}:make_voltage_list(p.voltage);
        for(auto &v:volts) {if(!std::isfinite(v)) throw std::invalid_argument("Invalid voltage_V");v=-v;}
        ctx.SetStage("Preparing mesh (cancel checked after preparation)");
        const auto mesh=prepare_mesh(device,p.mesh,volts,{p.temperature_K},p.solver,pc);
        ctx.ThrowIfCancellationRequested(); ctx.SetStage("Equilibrium");
        const auto eq=solve_equilibrium(device,mesh,p.temperature_K,p.solver,pc); auto previous=eq.state;
        output={{"contact_phi_eV",{device.contact_left.phi_m_eV,device.contact_right.phi_m_eV}},{"mesh_x_m",mesh.x_m},{"points",Json::array()},{"voltage_convention","consumer: left minus right; J: scaps_core signed terminal current"}};
        const bool externalNetwork=p.illumination.enabled || p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);
        std::vector<IVPoint> sweep;
        if(externalNetwork) {ctx.SetStage("I-V sweep ("+std::to_string(volts.size())+" points)");sweep=solve_iv(device,mesh,volts,p.temperature_K,p.solver,pc);ctx.ThrowIfCancellationRequested();} // one continuation instead of replaying every prefix (was O(N^2))
        for(std::size_t i=0;i<volts.size();++i) {
            ctx.ThrowIfCancellationRequested(); ctx.SetStage("I-V point "+std::to_string(i+1)+" / "+std::to_string(volts.size()));
            IVPoint pt;
            if (externalNetwork) {
                pt=std::move(sweep[i]);
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
            auto gr=GenerationRecombination(document,p,device,s);
            const bool external=p.illumination.enabled || p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);
            const double terminalJ=pt.status!=SolverStatus::Converged ? NAN : external ? pt.J_total_Am2 : s.Jn_Am2.front()+s.Jp_Am2.front();
            const double net=gr.at("net_generation_current_A_m2");
            // Native J = G-R - right electron removal - left hole removal + shunt.
            double contactLoss=0;for(const auto &c:s.contact_recombination) contactLoss+=c.id=="right"?c.electron_current_A_m2:c.hole_current_A_m2;
            const double predicted=net-contactLoss+pt.J_shunt_Am2;
            const double budgetError=std::abs(predicted-terminalJ)/std::max({1e-30,std::abs(net),std::abs(contactLoss),std::abs(pt.J_shunt_Am2),std::abs(terminalJ)});
            gr["contact_loss_current_A_m2"]=contactLoss;
            gr["shunt_current_A_m2"]=pt.J_shunt_Am2;
            gr["series_current_A_m2"]=pt.series_current_density_A_m2();
            gr["predicted_terminal_current_A_m2"]=predicted;
            gr["terminal_budget_identity"]="J = G - R - right.electron - left.hole + J_shunt (native current sign)";
            gr["terminal_budget_relative_error"]=std::isfinite(budgetError)?Json(budgetError):Json(nullptr);
            gr["terminal_budget_valid"]=std::isfinite(budgetError) && budgetError<=1e-6;
            output["points"].push_back({{"generation_recombination",gr},{"defect_previews",curves},{"bias_id","bias-"+std::to_string(i)},{"voltage_V",-volts[i]},{"J_A_m2",std::isfinite(terminalJ)?Json(terminalJ):Json(nullptr)},
                {"conservation_error_A_m2",pt.J_conservation_error_Am2},{"status",to_string(pt.status)},{"iterations",pt.iterations},{"residual",pt.final_residual_norm},
                {"internal_voltage_V",-s.applied_voltage_V},{"terminal_voltage_V",external?-pt.voltage_V:-volts[i]},{"trap_occupation",s.trap_occupation},{"interface_occupation",s.interface_occupation},{"x_m",s.x_m.empty()?mesh.x_m:s.x_m},{"Ec_eV",s.Ec_eV},{"Ev_eV",s.Ev_eV},{"EFn_eV",s.EFn_eV},{"EFp_eV",s.EFp_eV},{"n_m3",s.n_m3},{"p_m3",s.p_m3}});
            ctx.SetProgress(float(i+1),float(volts.size()));
        }
        const auto generation=generation_integrals_m2s(device,mesh,pc);std::vector<double> density;
        for(std::size_t i=0;i<generation.size();++i) {
            const double width=(i?std::max(0.,mesh.x_m[i]-mesh.x_m[i-1])*.5:0)+(i+1<mesh.x_m.size()?std::max(0.,mesh.x_m[i+1]-mesh.x_m[i])*.5:0);
            density.push_back(width>0?generation[i]/width:0.);
        }
        output["optics"]={{"x_m",mesh.x_m},{"generation_m3_s",density},{"absorption",Json::array()},{"incident_power_W_m2",incident_power_W_m2(device)}};
        for(std::size_t i=0;i<device.layers.size();++i) {
            auto layer=device.layers[i];std::vector<double> wavelengths,alpha,baseline;
            for(int n=0;n<=200;++n) {const double wavelength=300.+n*6.;wavelengths.push_back(wavelength);alpha.push_back(absorption_m1(layer,wavelength,pc));auto plain=layer;plain.absorption.urbach_enabled=false;baseline.push_back(absorption_m1(plain,wavelength,pc));}
            output["optics"]["absorption"].push_back({{"layer_id",document.at("layers")[i].value("scapslab_id",std::string("layer-")+std::to_string(i+1))},{"wavelength_nm",wavelengths},{"alpha_m1",alpha},{"without_tail_m1",baseline}});
        }
        if(op=="run" && p.illumination.enabled) {
            ctx.ThrowIfCancellationRequested();ctx.SetStage("Refining photovoltaic metrics");
            PhotovoltaicResult pv;const double power=incident_power_W_m2(device);
            if(power>0) pv=solve_photovoltaic(device,mesh,-p.voltage.stop_V,p.temperature_K,power,p.solver,pc);
            else pv.message="Photovoltaic metrics require a positive, known incident power [W/m^2]";
            auto finite=[](double v)->Json{return std::isfinite(v)?Json(v):Json(nullptr);};
            output["photovoltaic"]={{"status",to_string(pv.status)},{"message",pv.message},{"Voc_V",finite(-pv.Voc_V)},{"Jsc_A_m2",finite(-pv.Jsc_Am2)},{"FF_fraction",finite(pv.FF)},{"efficiency_fraction",finite(pv.eta)},{"Pmax_W_m2",finite(pv.Pmax_Wm2)},{"Vmpp_V",finite(-pv.Vmpp_V)},{"Jmpp_A_m2",finite(pv.Jmpp_Am2)}};
        }
    } else if(op!="inspect") throw std::invalid_argument("Unknown operation");
    if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe") {
        output["project_snapshot"]=document;
        output["provenance"]={{"device_hash",DocumentHash(document)},{"solver_version",SCAPSLAB_SOLVER_VERSION},
            {"time_unix_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}};
        bool valid=true;
        for(auto &point:output["points"]) {
            const bool converged=point.at("status")=="Converged";
            const double conservation=!point.contains("conservation_relative")?0:point.at("conservation_relative").is_number()?point.at("conservation_relative").get<double>():NAN;
            bool physical=converged && std::isfinite(conservation) && conservation<=0.01;
            if(point.contains("J_A_m2")) {
                const double current=point.at("J_A_m2").is_number()?std::abs(point.at("J_A_m2").get<double>()):0;
                physical=physical && point.at("J_A_m2").is_number() && point.at("conservation_error_A_m2").is_number() && point.at("conservation_error_A_m2").get<double>()<=std::max(1e-10,current*0.01);
            }
            if(point.contains("EQE")) physical=physical && point.at("EQE").is_number();
            if(point.contains("capacitance_F")) physical=physical && point.at("capacitance_F").is_number() && point.at("conductance_S").is_number();
            point["valid"]=physical;point["validity_status"]=!converged?"solver_failed":physical?"valid":"current_unresolved";
            if(point.contains("generation_recombination")) {
                auto &gr=point["generation_recombination"];
                for(auto &channel:gr["channels"]) {
                    bool finite=channel["integrated_current_A_m2"].is_number() && std::isfinite(channel["integrated_current_A_m2"].get<double>());
                    for(const auto *key:{"R_m3_s","R_sheet_m2_s"}) for(const auto &v:channel[key]) finite &= v.is_number() && std::isfinite(v.get<double>());
                    const bool curveValid=physical && finite && channel["layer_sum_relative_error"].get<double>()<=1e-6;
                    channel["valid"]=curveValid;channel["validity_status"]=curveValid?"valid":!physical?point["validity_status"].get<std::string>():"channel_budget_unresolved";
                }
                gr["valid"]=physical;
            }
            valid &= physical;
        }
        output["valid"]=valid;output["validity_status"]=valid?"valid":"contains_invalid_points";
        output["compute_time_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        output["mesh_node_count"]=output.at("mesh_x_m").size();
    }
    if(op!="save" && op!="project_save" && !op.starts_with("export_")) ctx.ThrowIfCancellationRequested();
}
namespace {
// Undo record for destructive history edits (clear/delete): shares the Cow'd results, so it costs no copy.
struct DeviceSnap {
    std::string id,selected,status;Ref<const Json> history,result,band,ac,batch;
    explicit DeviceSnap(const DeviceState &d):id(d.id),selected(d.selectedRun),status(d.status),history(d.history.share()),result(d.result.share()),band(d.bandResult.share()),ac(d.acResults.share()),batch(d.batchResult.share()) {}
    void Apply(DeviceState &d) const {d.selectedRun=selected;d.status=status;d.history.adopt(history);d.result.adopt(result);d.bandResult.adopt(band);d.acResults.adopt(ac);d.batchResult.adopt(batch);}
};
class HistoryRecord final : public ICommand {
    Workflow &w;std::vector<DeviceSnap> before,after;
    Result<void> Apply(const std::vector<DeviceSnap> &v) {for(const auto &s:v)s.Apply(w.Device(s.id));w.projectDirty=true;w.RefreshDirty();return {};}
public:
    HistoryRecord(Workflow &w,std::vector<DeviceSnap> before,std::vector<DeviceSnap> after):w(w),before(std::move(before)),after(std::move(after)) {}
    std::string Description() const override {return "Change ScapsLab history";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(after);}
    Result<void> Undo(CommandContext &) override {return Apply(before);}
};
// Undo record for device rename/colour/compare/reorder and project name/ui: small value snapshots of the metadata, no workspace copy.
struct MetaSnap {
    std::string name,active;Json ui;std::set<std::string> compare;
    std::vector<std::string> order;std::vector<std::tuple<std::string,Json,std::string>> meta; // per device in order: name, colour, description
    std::vector<DeviceSnap> hist; // only when a lower history_retention trimmed runs
    explicit MetaSnap(const Project &p):name(p.name),active(p.active_device),ui(p.ui),compare(p.compare_set) {for(const auto &d:p.devices) {order.push_back(d->id);meta.emplace_back(d->name,d->colour,d->description);}}
};
class MetaRecord final : public ICommand {
    Workflow &w;MetaSnap before,after;
    Result<void> Apply(const MetaSnap &m) {
        auto &p=w.project;p.name=m.name;p.active_device=m.active;p.ui=m.ui;p.compare_set=m.compare;
        std::vector<Ref<DeviceState>> sorted;
        for(std::size_t i=0;i<m.order.size();++i) for(auto &d:p.devices) if(d->id==m.order[i]) {d->name=std::get<0>(m.meta[i]);d->colour=std::get<1>(m.meta[i]);d->description=std::get<2>(m.meta[i]);sorted.push_back(d);}
        for(const auto &h:m.hist) h.Apply(w.Device(h.id));p.devices=std::move(sorted);w.projectDirty=true;w.RefreshDirty();w.Emit(DeviceListChanged());return {};
    }
public:
    MetaRecord(Workflow &w,MetaSnap before,MetaSnap after):w(w),before(std::move(before)),after(std::move(after)) {}
    std::string Description() const override {return "Change ScapsLab devices";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(after);}
    Result<void> Undo(CommandContext &) override {return Apply(before);}
};
class ProjectRecord final : public ICommand {
    Workflow &w;Project before,after;
    Result<void> Apply(const Project &p) {w.project=p;w.projectDirty=true;w.RefreshDirty();return {};}
public:
    ProjectRecord(Workflow &w,Project before,Project after):w(w),before(std::move(before)),after(std::move(after)) {}
    std::string Description() const override {return "Delete ScapsLab device";}
    bool IsUndoable() const noexcept override {return true;}
    Result<void> Execute(CommandContext &) override {return Apply(after);}
    Result<void> Undo(CommandContext &) override {return Apply(before);}
};
}
void Workflow::HistoryOp(const Json &request) {
    const auto op=request.at("operation").get<std::string>();
    const auto scope=request.value("scope",std::string("device"));
    if(scope!="device" && scope!="all") throw std::invalid_argument("scope must be device or all");
    const auto id=request.value("device_id",project.active_device);
    std::vector<DeviceState *> targets;
    for(const auto &d:project.devices) if(scope=="all" || d->id==id) targets.push_back(d.get());
    if(targets.empty()) throw std::invalid_argument("Unknown device_id");
    const int retention=project.ui.value("history_retention",5);
    const bool single=op!="history_clear" && !request.contains("unpin_kind");
    if(single) { // validate before touching anything
        const auto &h=*targets.front()->history;
        if(std::none_of(h.begin(),h.end(),[&](const Json &r){return r.at("run_id")==request.at("run_id");})) throw std::invalid_argument("Unknown run_id");
        if(request.contains("colour")) {
            const auto &c=request.at("colour");if(!c.is_array() || c.size()!=4) throw std::invalid_argument("Invalid history colour");
            for(const auto &v:c) if(!v.is_number() || !std::isfinite(v.get<double>()) || v<0 || v>1) throw std::invalid_argument("Invalid history colour");
        }
        for(const auto *key:{"pinned","visible"}) if(request.contains(key) && !request.at(key).is_boolean()) throw std::invalid_argument(std::string(key)+" must be boolean");
        if(request.contains("name") && !request.at("name").is_string()) throw std::invalid_argument("name must be a string");
    }
    std::vector<DeviceSnap> before,after;
    if(op=="history_clear") for(auto *dp:targets) before.emplace_back(*dp);
    for(auto *dp:targets) {
        auto &d=*dp;
        if(op=="history_select") {d.selectedRun=request.at("run_id").get<std::string>();LimitProfiles(d);continue;}
        auto &history=d.history.mut();
        const auto noSelection=[&]{if(std::none_of(history.begin(),history.end(),[&](const Json &r){return r.at("run_id")==d.selectedRun;}))d.selectedRun.clear();};
        if(op=="history_clear") {
            const bool pinned=request.value("include_pinned",false),byId=request.contains("run_ids"); // run_ids: delete exactly these runs
            const auto ids=request.value("run_ids",Json::array());
            history.erase(std::remove_if(history.begin(),history.end(),[&](const Json &r){return byId?std::find(ids.begin(),ids.end(),r.at("run_id"))!=ids.end():pinned || !r.at("pinned").get<bool>();}),history.end());
            if(byId)noSelection();else d.selectedRun=history.empty()?"":history.back().at("run_id").get<std::string>();
            const auto kept=[&](const Json &result,const std::string &kind) {
                return !result.empty() && std::any_of(history.begin(),history.end(),[&](const Json &run){return run.at("kind")==kind && run.at("provenance")==result.at("provenance");});
            };
            if(!kept(*d.result,"iv"))d.result=Json::object();
            if(!kept(*d.bandResult,"band"))d.bandResult=Json::object();
            auto ac=*d.acResults;for(auto it=ac.begin();it!=ac.end();) {if(!kept(it.value(),it.key()))it=ac.erase(it);else ++it;}
            if(ac.size()!=d.acResults.size())d.acResults=ac;
            d.batchResult=Json::object();d.status=history.empty()?"no results":"previous";
        } else if(request.contains("unpin_kind")) {
            const auto kind=request.at("unpin_kind").get<std::string>();
            for(auto &r:history) if(r.at("pinned")==true && (r.at("kind")==kind || (kind=="iv" && r.at("kind")=="band"))) {r["pinned"]=false;if(r.at("name").get<std::string>().starts_with("#"))r["name"]="";}
            TrimHistory(history,retention);noSelection();
        } else {
            auto run=std::find_if(history.begin(),history.end(),[&](const Json &r){return r.at("run_id")==request.at("run_id");});
            {
                for(const auto *key:{"pinned","name","visible","colour"}) if(request.contains(key)) (*run)[key]=request.at(key);
                if(request.contains("pinned") && run->at("pinned")==true && run->at("name")=="") {
                    // "Single shot": number + parameter diff against the previous pinned shot.
                    int n=0;const Json *prev=nullptr;
                    for(const auto &r:history) {const auto nm=r.at("name").get<std::string>();if(nm.starts_with("#"))n=std::max(n,std::atoi(nm.c_str()+1));if(&r!=&*run && r.at("pinned")==true && &r<&*run)prev=&r;}
                    std::string text="#"+std::to_string(n+1);
                    if(prev){const auto diff=DiffLabel(prev->at("document"),run->at("document"));text+=" "+(diff.empty()?std::string("same parameters"):diff);}
                    (*run)["name"]=text;
                } else if(request.contains("pinned") && run->at("pinned")==false && run->at("name").get<std::string>().starts_with("#"))(*run)["name"]="";
            }
            TrimHistory(history,retention);noSelection();
        }
        LimitProfiles(d);
    }
    if(!before.empty()) {for(auto *dp:targets)after.emplace_back(*dp);(void)projectUndo.PushExecuted(CreateUnique<HistoryRecord>(*this,std::move(before),std::move(after)));preferProjectUndo=true;}
    projectDirty=true;RefreshDirty();status="Completed: "+op;
    for(auto *dp:targets) {Emit(HistoryChanged(dp->id));Emit(SelectionChanged(dp->id));}
    response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();
}
void Workflow::DeviceOp(const Json &request) {
    const auto op=request.at("operation").get<std::string>();
    const std::string id=request.value("device_id",project.active_device);
    auto &devices=project.devices;
    const auto at=std::find_if(devices.begin(),devices.end(),[&](const auto &d){return d->id==id;});
    const auto known=[&]{if(at==devices.end()) throw std::invalid_argument("Unknown device_id");};
    const auto checkName=[&](const char *what) {if(request.contains("name") && (!request.at("name").is_string() || request.at("name").get<std::string>().empty())) throw std::invalid_argument(std::string(what)+" name is empty");};
    MetaSnap before(project);bool undoable=false,trimmedAny=false;std::vector<DeviceSnap> beforeHist;
    if(op=="device_add" || op=="device_duplicate") {
        checkName("Device");
        auto d=CreateRef<DeviceState>();
        if(op=="device_duplicate") {
            known();const auto &src=**at;
            d->name=src.name+" copy";d->description=src.description;d->document=src.document;d->actions=src.actions;d->batchSpec=src.batchSpec;d->recorderColumns=src.recorderColumns;
            d->batchResult.adopt(src.batchResult.share());d->selectedLayer=src.selectedLayer;d->selectedInterface=src.selectedInterface;d->selectedBias=src.selectedBias;d->selectedMember=src.selectedMember;d->biasVoltage_V=src.biasVoltage_V;
        } else {
            const auto doc=request.value("loaded",false)?request.at("document"):request.contains("document")?SelfContained(request.at("document")):NewDeviceDocument();
            (void)Model(doc);d->document=NormalizeDocument(doc);d->name=doc.value("case_id",std::string("Device"));d->description=request.value("description",std::string{});
        }
        if(request.contains("name")) d->name=request.at("name").get<std::string>();
        static std::atomic_uint64_t sequence{0};
        d->id="device-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
        static const double colours[][4]={{.25,.65,.95,1},{.95,.55,.25,1},{.4,.8,.45,1},{.8,.45,.85,1},{.9,.8,.3,1}};
        d->colour=Json::array({colours[devices.size()%5][0],colours[devices.size()%5][1],colours[devices.size()%5][2],colours[devices.size()%5][3]});d->metadataDirty=true;
        devices.push_back(d);project.active_device=d->id;project.compare_set.insert(d->id);
    } else if(op=="action_settings") {
        known();if(!request.at("actions").is_object()) throw std::invalid_argument("Invalid action list");
        ValidateActions(request.at("actions"));(*at)->actions=request.at("actions");
    } else if(op=="project_edit") {
        checkName("Project");
        if(request.contains("ui")) (void)ValidateUi(request.at("ui"));
        if(request.contains("name")) project.name=request.at("name").get<std::string>();
                if(request.contains("ui")) {
            project.ui=request.at("ui");const int retention=project.ui.value("history_retention",5);
            for(auto &d:devices) if(d->history.size()>0) {
                Json h=*d->history;TrimHistory(h,retention);
                if(h.size()==d->history.size()) continue;
                if(!trimmedAny) {trimmedAny=true;for(auto &e:devices) beforeHist.emplace_back(*e);}
                d->history=std::move(h);
                if(std::none_of(d->history.begin(),d->history.end(),[&](const Json &r){return r.at("run_id")==d->selectedRun;}))d->selectedRun.clear();
            }
        }
        undoable=true;
    } else if(op=="device_remove") {
        known();if(devices.size()==1) throw std::invalid_argument("Cannot delete the last device");
        const auto beforeProject=project;
        devices.erase(devices.begin()+(at-devices.begin()));project.compare_set.erase(id);
        if(project.active_device==id) project.active_device=devices.front()->id;
        (void)projectUndo.PushExecuted(CreateUnique<ProjectRecord>(*this,beforeProject,project));preferProjectUndo=true;
    } else if(op=="device_update") {
        known();auto &d=**at;checkName("Device");
        if(request.contains("colour")) {
            const auto &c=request.at("colour");
            if(!c.is_array() || c.size()!=4) throw std::invalid_argument("colour must contain four RGBA values");
            for(const auto &v:c) if(!v.is_number() || !std::isfinite(v.get<double>()) || v.get<double>()<0 || v.get<double>()>1) throw std::invalid_argument("Invalid RGBA colour");
        }
        if(request.contains("description") && !request.at("description").is_string()) throw std::invalid_argument("description must be a string");
        if(request.contains("name")) d.name=request.at("name").get<std::string>();
        if(request.contains("colour")) d.colour=request.at("colour");
        if(request.contains("description")) d.description=request.at("description").get<std::string>();
        if(request.contains("compare")) {project.compare_set.erase(id);if(request.at("compare").get<bool>()) project.compare_set.insert(id);}
        d.metadataDirty=true;undoable=true;
    } else if(op=="device_move") {
        known();const int index=request.at("index");if(index<0 || index>=int(devices.size())) throw std::invalid_argument("Invalid device index");
        const auto d=*at;devices.erase(devices.begin()+(at-devices.begin()));devices.insert(devices.begin()+index,d);undoable=true;
    } else if(op!="device_list") throw std::invalid_argument("Unknown device operation");
    if(undoable) {
        MetaSnap after(project);
        if(trimmedAny) {before.hist=std::move(beforeHist);for(auto &e:devices) after.hist.emplace_back(*e);}
        (void)projectUndo.PushExecuted(CreateUnique<MetaRecord>(*this,std::move(before),std::move(after)));preferProjectUndo=true;}
    if(op!="device_list") {projectDirty=true;Emit(DeviceListChanged());}
    RefreshDirty();status="Completed: "+op;
    response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();
}

Workflow::Workflow(Ref<JobSystem> j,Ref<CommandRegistry> registry,Ref<EventBus> eventBus,Ref<Notifier> note):Layer("ScapsLab workflow"),jobs(std::move(j)),commands(registry?std::move(registry):CreateRef<CommandRegistry>()),bus(eventBus?std::move(eventBus):CreateRef<EventBus>()),notifier(note?std::move(note):CreateRef<Notifier>(bus)) {
    ++constructed;
    auto initial=CreateRef<DeviceState>();initial->id="device-1";initial->name="Device";project.devices.push_back(initial);project.active_device=initial->id;project.compare_set.insert(initial->id);
    for(const auto *op:{"load_material","save_material","set_profile","load_profile","preview_profile","export_profile","tutorial_start","tutorial_stop","tutorial_state","tutorial_list","load","save","edit","layer","run","cv","cf","qe","export_ac","export_gr","export_results","action_settings","inspect","cancel","undo","redo","auto_recalculate","band",
        "history_list","history_update","history_select","history_clear","quick_sweep","project_new","project_load","project_save","project_edit","device_list","device_add","device_duplicate","device_remove","device_select","device_update","device_move","run_all","batch_parameters","batch_select","batch_settings","preview_batch","run_batch","retry_batch","record_batch","export_batch","send_to_plotlab","import_def","export_def"}) {
        const auto registered=commands->Register({CommandID{std::string("scapslab.")+op},std::string("ScapsLab ")+op,"ScapsLab","",{},CommandFlags::AsyncCapable},
            [this,op](CommandContext &){
                const std::string operation=op;
                const bool menuDefault=operation=="device_add" || operation=="run" || operation=="run_batch" || operation=="cancel" || operation=="undo" || operation=="redo" || operation=="tutorial_stop";
                return CreateUnique<Command>(*this,menuDefault?Json{{"operation",operation}}:Json::object());
            });
        if(!registered) throw std::runtime_error("Cannot register ScapsLab command");
    }
}
Workflow::~Workflow() { tutorial.Stop();queuedSave=Json();queuedProject.clear();runQueue.clear();edits.clear();if(active) { (void)jobs->RequestCancel(active); while(active) {OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(5));} } while(saveJob) {OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(5));} }
void Workflow::StartOperation(const Json &r,const std::string &id) {
    auto &d=Device(id);pendingDevice=id;pending=CreateRef<Operation>(d.document,r);const std::string op0=r.at("operation");if(op0.starts_with("export_"))pending->acResults=*d.acResults;if(op0.find("batch")!=std::string::npos || op0.starts_with("export_") || op0=="send_to_plotlab")pending->batchResult=*d.batchResult;pending->batchSpec=d.batchSpec;pending->recorderColumns=d.recorderColumns;
    const std::string op=r.at("operation");
    if(op=="load_profile")pending->request["project_path"]=path;
    if(op=="export_gr") {if(r.contains("source") && r.at("source")!="iv" && r.at("source")!="band") throw std::invalid_argument("source must be iv or band");pending->request["device_id"]=id;pending->workspace=r.value("source",std::string("iv"))=="band"?*d.bandResult:*d.result;}
    if(op=="export_results") pending->workspace=Workspace(); // the only job that still needs the whole workspace

    if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe") d.status="running";
    if(op=="run") {d.needsRecalculate=false;d.needsBand=false;}if(op=="band")d.needsBand=false;
    active=jobs->Submit(pending);if(!r.value("autosave",false)){status="Running: "+d.name;response=Json();}
}
void Workflow::StartSave(const Json &r) {
    const bool autosave=r.value("autosave",false);
    if(saveJob) { // one save at a time: remember the newest request (a manual one is not displaced by an autosave)
        if(queuedSave.empty() || !autosave || queuedSave.value("autosave",false)) queuedSave=r;
        if(!autosave) status="Saving... (queued)";
        return;
    }
    auto op=CreateRef<Operation>(Current().document,r);op->request["operation"]="project_save";
    op->workspace=Workspace(false);
    for(std::size_t i=0;i<project.devices.size();++i) {
        const auto &d=*project.devices[i];op->workspace["devices"][i]["selected_run"]=d.selectedRun;
        op->shared.push_back({d.history.share(),d.result.share(),d.bandResult.share(),d.acResults.share(),d.batchResult.share()});
    }
    savePending=op;saveEpoch=epoch;savingAuto=autosave;saveJob=jobs->Submit(op);
    if(!autosave) { // what is saved is clean; later edits mark the project dirty again
        status="Saving...";projectDirty=false;
        for(auto &d:project.devices) {d->metadataDirty=false;d->undo.MarkClean();}
        RefreshDirty();
    }
}
void Workflow::UpdateSave() {
    if(saveJob) {
        const auto snapshot=jobs->GetJob(saveJob);
        if(snapshot && (snapshot->status==JobStatus::Queued || snapshot->status==JobStatus::Running)) return;
        const auto op=std::move(savePending);const bool autosave=savingAuto,sameProject=saveEpoch==epoch;saveJob=0;savePending.reset();
        if(snapshot && snapshot->status==JobStatus::Completed) {
            if(!autosave) {
                const auto file=op->request.at("path").get<std::string>();
                if(sameProject) path=file;
                recent.erase(std::remove(recent.begin(),recent.end(),file),recent.end());recent.insert(recent.begin(),file);if(recent.size()>10) recent.erase(recent.end()-1);
                if(!recoveryPath.empty()) {std::error_code ec;if(!std::filesystem::equivalent(file,recoveryPath,ec))std::filesystem::remove(recoveryPath,ec);}
                status="Saved "+op->output.at("path").get<std::string>()+" ("+std::to_string(op->output.at("size_bytes").get<std::size_t>())+" bytes)";
                Emit(ProjectSaved(file,false));
                if(!active) {response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();if(!lightResponse)response["operation_result"]=op->output;}
            } else Emit(ProjectSaved(op->request.value("path",std::string{}),true));
        } else if(!autosave) {
            if(sameProject) projectDirty=true;
            status=snapshot?snapshot->errorMessage:std::string("Save failed");
            if(!active) response=Failure("operation_failed",status);
            Emit(ProjectSaveFailed(op->request.value("path",std::string{}),status,false));notifier->Error("Save failed",status,NotificationCategory::Project);
        } else Emit(ProjectSaveFailed(op->request.value("path",std::string{}),snapshot?snapshot->errorMessage:std::string("Save failed"),true)); // autosave: event only, no toast (Notifier has no dedupe yet)
        RefreshDirty();
    }
    if(!saveJob && !queuedSave.empty()) {auto next=std::move(queuedSave);queuedSave=Json();StartSave(next);return;}
    if(!saveJob && dirty && !recoveryPath.empty() && std::chrono::steady_clock::now()-autosavedAt>=std::chrono::seconds(30)) {
        autosavedAt=std::chrono::steady_clock::now();StartSave({{"operation","project_save"},{"path",recoveryPath},{"autosave",true}});
    }
}
Json Workflow::Dispatch(const Json &r) {
    try { CommandContext c; c.Set("request",r);
        const auto out=commands->Execute(CommandID{"scapslab."+r.at("operation").get<std::string>()},std::move(c));
        if(!out) {status=out.Error().technicalDetails; return Failure("command_rejected",status);}
        return {{"ok",true},{"job_id",active},{"error",nullptr}};
    } catch(const std::exception &e) {return Failure("invalid_request",e.what());}
}
void Workflow::Changed() {
    auto &d=Current();d.needsRecalculate=true;d.needsBand=true;d.changedAt=std::chrono::steady_clock::now();
    d.status=d.history.empty()?"no results":"previous";
}
void Workflow::OnUpdate(float) {
    static const bool perf=std::getenv("SCAPSLAB_PERF")!=nullptr; // SCAPSLAB_PERF=1: log UI-thread hitches (>2 ms) to stderr
    const auto t0=std::chrono::steady_clock::now();
    UpdateOperation();
    UpdateTutorial();
    if(perf) {const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();if(ms>2)std::fprintf(stderr,"[perf] Workflow::OnUpdate %.1f ms (status: %s)\n",ms,status.c_str());}
}
void Workflow::UpdateOperation() {
    UpdateSave();
    if(!active) {
        if(!edits.empty()) {auto edit=std::move(edits.front());edits.pop_front();StartOperation(edit,edit.value("device_id",project.active_device));}
        else if(!queuedProject.empty()) {auto request=std::move(queuedProject);queuedProject=Json();StartOperation(request,project.active_device);}
        else if(!runQueue.empty()) {const auto id=runQueue.front();runQueue.pop_front();StartOperation({{"operation","run"},{"batch",true}},id);}
        else {
            auto &d=Current();
            if(autoRecalculate && d.needsRecalculate && !interacting && std::chrono::steady_clock::now()-d.changedAt>=std::chrono::milliseconds(250)) {
                d.needsRecalculate=false;(void)Dispatch({{"operation","run"}});
            } else if(autoRecalculateBand && d.needsBand && !interacting && std::chrono::steady_clock::now()-d.changedAt>=std::chrono::milliseconds(250)) {
                d.needsBand=false;(void)Dispatch({{"operation","band"},{"voltage_V",d.biasVoltage_V}});
            }
        }
        return;
    }
    const auto snapshot=jobs->GetJob(active);if(!snapshot || snapshot->status==JobStatus::Queued || snapshot->status==JobStatus::Running) return;
    const auto op=pending->request.at("operation").get<std::string>();
    auto &d=Device(pendingDevice);
    const bool outValid=pending->output.is_object() && pending->output.value("valid",false);
    auto Take=[&](auto &dst){if(lightResponse)dst=std::move(pending->output);else dst=pending->output;}; // UI app: move big results instead of copying
            if((op=="quick_sweep" || op=="run_batch" || op=="retry_batch") && pending->output.contains("rows")) {
                const auto group="batch-"+std::to_string(pending->output.at("provenance").at("time_unix_ms").get<std::int64_t>());
                for(const auto &row:pending->output.at("rows")) {
                    if(op=="retry_batch" && row.at("id")!=pending->request.at("member_id"))continue;
                    auto model=pending->document;
                    for(const auto &parameter:BatchParameters(model)) if(row.at("parameters").contains(parameter.at("path").get<std::string>()) && row.at("parameters").at(parameter.at("path").get<std::string>())!=parameter.at("value")) model[Json::json_pointer(parameter.at("pointer").get<std::string>())]=row.at("parameters").at(parameter.at("path").get<std::string>());
                    Json result=Json::object();for(const auto &[key,value]:pending->output.items()) if(key!="rows") result[key]=value; /* do not copy every row per row */ result["rows"]=Json::array({row});result["project_snapshot"]=model;result["provenance"]["device_hash"]=DocumentHash(model);
                    result["elapsed_s"]=row.at("elapsed_s");
                    StoreRun(d.history.mut(),"batch",result,model,pending->request,project.ui.value("history_retention",5),group);
                    if(!d.history.empty()) {const double t=pending->output.at("rows").size()<2?0.:double(&row-&pending->output.at("rows")[0])/double(pending->output.at("rows").size()-1);d.history.mut().back()["colour"]=Json::array({.15+.7*t,.4+.3*std::sin(3.14159*t),.9-.7*t,1.});}
                }
                d.selectedRun=d.history.empty()?"":d.history.back().at("run_id").get<std::string>();
                LimitProfiles(d);Emit(HistoryChanged(d.id));Emit(SelectionChanged(d.id));
            }
    if(snapshot->status==JobStatus::Completed) {
        if(op=="project_new" || op=="project_load" || op=="load") {
            SetWorkspace(pending->workspace,true);for(auto &device:project.devices) device->undo.Clear();projectUndo.Clear();projectDirty=false;preferProjectUndo=false;
            ++epoch;path=op=="project_new"?std::string{}:pending->request.at("path").get<std::string>();
            if(op!="project_new" && std::filesystem::path(path).extension()!=".scapslab") {path=std::filesystem::path(path).replace_extension(".scapslab").string();projectDirty=true;}
            for(auto &device:project.devices) {device->dirty=false;device->metadataDirty=false;device->undo.MarkClean();device->needsBand=false;device->needsRecalculate=false;}
            Emit(ProjectLoaded(path));Emit(DeviceListChanged());
        } else if(op=="device_read") {
            Json add={{"operation","device_add"},{"document",pending->document},{"loaded",true}};
            for(const auto *key:{"name","description"}) if(pending->request.contains(key)) add[key]=pending->request.at(key);
            DeviceOp(add);
        } else {
            if((op=="edit" || op=="load_material" || op=="set_profile" || op=="load_profile" || op=="layer" || op=="import_def") && !pending->request.value("history",false) && d.document!=pending->document) {
                (void)d.undo.PushExecuted(CreateUnique<EditRecord>(*this,d.document,pending->document,pending->request.value("gesture_id",std::string{})));preferProjectUndo=false;
            }
            d.document=pending->document;
            const int retention=project.ui.value("history_retention",5);
            if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe") {
                StoreRun(d.history.mut(),op=="run"?"iv":op,pending->output,d.document,pending->request,retention);
                d.selectedRun=d.history.empty()?"":d.history.back().at("run_id").get<std::string>();
                Emit(HistoryChanged(d.id));Emit(SelectionChanged(d.id));
            }
            if(op=="batch_select") d.selectedMember=pending->request.at("index");
            if(op=="batch_settings") {d.batchSpec=pending->batchSpec;d.recorderColumns=pending->recorderColumns;projectDirty=true;}
            if(op=="quick_sweep" || op=="run_batch" || op=="retry_batch" || op=="record_batch") {Take(d.batchResult);projectDirty=true;Emit(RunCompleted(d.id,d.selectedRun,op,d.revision));}
            if(op=="run") {Take(d.result);d.bandResult=Json::object();}
            if(op=="band") {Take(d.bandResult);}
            if(op=="cv" || op=="cf" || op=="qe") {Take(d.acResults.mut()[op]);d.acTyped[op].reset();} // typed view (d.Iv()/Band()/Ac()) is built lazily on first use: eager conversion cost ~20 ms UI + ~75 MB on big results
            if(op=="run" || op=="band") LimitProfiles(d);
            if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe") {d.actions[op=="run"?"iv":op]=pending->request;d.status=outValid?"solved":"failed";projectDirty=true;
                if(outValid) Emit(RunCompleted(d.id,d.selectedRun,op=="run"?"iv":op,d.revision));
                else {const std::string message="solver returned no valid result";Emit(RunFailed(d.id,op,message,false));notifier->Error("Calculation failed",d.name+": "+message);}
            }
            if(op=="edit" || op=="load_material" || op=="set_profile" || op=="load_profile" || op=="layer" || op=="import_def") {
                if(op=="import_def") {d.selectedLayer=0;d.selectedInterface=-1;}
                d.needsRecalculate=true;d.needsBand=true;d.changedAt=std::chrono::steady_clock::now();d.status=d.history.empty()?"no results":"previous";d.dirty=!d.undo.IsClean();
                ++d.revision;Emit(DocumentEdited(d.id,d.revision));
                if(op=="import_def") Emit(SelectionChanged(d.id));
            }
            if(op=="save" && project.devices.size()==1) {d.dirty=false;d.metadataDirty=false;d.undo.MarkClean();}
        }
        if((op=="load" || op=="project_load" || op=="save" || op=="project_save") && !pending->request.value("autosave",false)) {
            const auto file=pending->request.at("path");if(op=="save" || op=="project_save") Emit(ProjectSaved(file.get<std::string>(),false));recent.erase(std::remove(recent.begin(),recent.end(),file),recent.end());recent.insert(recent.begin(),file);if(recent.size()>10) recent.erase(recent.end()-1);
        }
        const bool quiet=op=="project_save" && pending->request.value("autosave",false);const auto keepStatus=status;const auto keepResponse=quiet?response:Json();RefreshDirty();status="Completed: "+op;response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();
        if(pending->output.contains("size_bytes")) status="Saved "+pending->output.at("path").get<std::string>()+" ("+std::to_string(pending->output.at("size_bytes").get<std::size_t>())+" bytes)";
        if(op=="project_load" && !recoveryPath.empty() && pending->request.at("path")==recoveryPath) {path.clear();projectDirty=true;RefreshDirty();response=Snapshot();status="Recovered unsaved project; use Save As";}
        if(op=="project_save" && !pending->request.value("autosave",false) && !recoveryPath.empty()) {std::error_code ec;if(!std::filesystem::equivalent(path,recoveryPath,ec))std::filesystem::remove(recoveryPath,ec);}
        if(!lightResponse && !pending->output.empty()) response["operation_result"]=pending->output;
        if(quiet) {status=keepStatus;response=keepResponse;}
    } else {
        if((op=="quick_sweep" || op=="run_batch" || op=="retry_batch") && pending->output.contains("rows")) {d.batchResult=pending->output;projectDirty=true;}
        if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe") d.status=snapshot->status==JobStatus::Cancelled?(d.result.empty()?"no results":"solved"):"failed";
        status=snapshot->errorMessage;response=Failure(snapshot->status==JobStatus::Cancelled?"cancelled":"operation_failed",status);
        {
            const bool cancelled=snapshot->status==JobStatus::Cancelled,isRun=op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="quick_sweep" || op=="run_batch" || op=="retry_batch";
            if(isRun) Emit(RunFailed(d.id,op,status,cancelled));
            if(!cancelled && !(op=="project_save" && pending->request.value("autosave",false))) {
                if(op=="save" || op=="project_save") Emit(ProjectSaveFailed(pending->request.value("path",std::string{}),status,false));
                if(isRun) notifier->Error("Calculation failed",d.name+": "+status);
                else if(op=="load" || op=="project_load") notifier->Error("Load failed",status,NotificationCategory::Project);
                else if(op=="save" || op=="project_save") notifier->Error("Save failed",status,NotificationCategory::Project);
                else if(op.starts_with("export_")) notifier->Error("Export failed",status,NotificationCategory::Export);
            }
        }
    }
    if(!lightResponse && (op=="run_batch" || op=="retry_batch") && response.is_object()) response["batch_result"]=*d.batchResult;
    if(pending->request.value("batch",false)) {
        ++batchCompleted;batchResponse.push_back({{"device_id",pendingDevice},{"ok",response.at("ok")},{"error",response.at("error")}});
        if(snapshot->status==JobStatus::Cancelled) runQueue.clear();
        if(runQueue.empty()) {const bool ok=std::all_of(batchResponse.begin(),batchResponse.end(),[](const Json &r){return r.at("ok").get<bool>();});response=Snapshot();response["ok"]=ok;response["runs"]=batchResponse;if(!ok)response["error"]={{"code",snapshot->status==JobStatus::Cancelled?"cancelled":"operation_failed"},{"message","One or more device runs failed"}};}
    }
    active=0;pending.reset();
}
}
