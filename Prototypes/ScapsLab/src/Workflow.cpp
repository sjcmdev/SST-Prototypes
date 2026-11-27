#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Commands/Commands.hpp"
#include "Commands/UndoOps.hpp"
#include "Io.hpp"
#include "Core/App/Managers/ConfigManager.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include "Core/IO/IOLayer.hpp"
#include "Core/Commands/SystemCommandEvents.hpp"
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
#include "Library/PythonLibrary.hpp"
#include "Library/LibraryUi.hpp"
// shortcut: IVSession/prepare_mesh/PV have no public ExecutionContext; use the engine scope until their public APIs accept it.
#include <scaps_core/../../src/parallel.hpp>
#include <mutex>
#include <exception>
namespace SST::ScapsLab {
namespace {
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
    if(j.is_boolean())return YAML::Node(j.get<bool>());
    if(j.is_number_integer())return YAML::Node(j.get<std::int64_t>());
    if(j.is_number_unsigned())return YAML::Node(j.get<std::uint64_t>());
    YAML::Node n(j.get<double>());n.SetTag("tag:yaml.org,2002:float");return n;
}
std::string Text(const Json &j) { YAML::Emitter e; e.SetDoublePrecision(17); e << ToYaml(j); if (!e.good()) throw std::runtime_error(e.GetLastError()); return e.c_str(); }
void SaveText(const std::filesystem::path &p,const std::string &text) {
    std::string error; if (!Io::Save(p,text,error)) throw std::runtime_error(error);
}
void Positive(double v,const char *field,bool zero=false) {
    if (!std::isfinite(v) || (zero ? v<0 : v<=0)) throw std::invalid_argument(std::string("Invalid ")+field);
}
Json Failure(const char *code,const std::string &message) { return {{"ok",false},{"error",{{"code",code},{"message",message}}}}; }
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
    auto p=scaps_core::device_project_from_node(ToYaml(SolverDocument(j)),std::filesystem::temp_directory_path());
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
    const int levels=sheet?scaps_core::ProjectInterfaceDefect{}.energy_levels:scaps_core::ProjectDefect{}.energy_levels;
    const auto quadrature=sheet?scaps_core::ProjectInterfaceDefect{}.energy_quadrature:scaps_core::ProjectDefect{}.energy_quadrature;
    Json d={{"energy_distribution","single"},{"energy_offset_eV",0.0},{"charge_type","neutral"},
        {"characteristic_energy_eV",0.05},{"gaussian_width",6.0},{"energy_levels",levels},{"energy_quadrature",quadrature}};
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
const scaps_core::OpticalTable& AM15SpectrumValues() {
    static const scaps_core::OpticalTable values={
#include "AM15.inc"
    };return values;
}
Json AM15Spectrum() {return AM15SpectrumValues();}

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
    p.source_file=spec.value("source_file",p.source_file);
    p.shared_offset_m=spec.value("shared_offset_m",0.);p.shared_thickness_m=spec.value("shared_thickness_m",0.);
    scaps_core::validate_profile(p);return p;
}
ProfileFileData ReadProfileFile(const std::string& path,bool logarithmic) {
    ProfileFileData out;out.profile=scaps_core::read_grading_file(path,logarithmic);out.profile.source_file=path;
    std::ifstream input(path);std::string line;std::vector<double> coordinates;
    while(std::getline(input,line)){std::istringstream row(line);double x,y;if(row>>x>>y)coordinates.push_back(x);}
    if(!std::is_sorted(coordinates.begin(),coordinates.end()) || coordinates.size()!=out.profile.coordinate.size())out.warnings.push_back("Sorted profile points; kept the first occurrence of duplicate coordinates.");return out;
}
ProfileSamplesData ProfileSamples(const scaps_core::Profile& p,double thickness,const scaps_core::Profile* composition) {
    Positive(thickness,"thickness_m");scaps_core::validate_profile(p);if(composition)scaps_core::validate_profile(*composition);
    ProfileSamplesData out;out.source_file=p.source_file;out.source_point_count=p.coordinate.size();
    for(int i=0;i<=200;++i) {const double at=thickness*i/200.;out.x_m.push_back(at);const double coordinate=composition?scaps_core::evaluate_position_profile(*composition,at,thickness):at/thickness;out.value_SI.push_back(p.composition_dependent?scaps_core::evaluate_profile(p,coordinate):scaps_core::evaluate_position_profile(p,at,thickness));}return out;
}
scaps_core::Profile ResolveSharedProfile(const scaps_core::Profile& source,const ProjectDraft& draft,const std::string& layerId,const std::string& parameter) {
    auto profile=source;const auto field=ProjectFieldPath(draft.project,&source);std::string pointer="/";for(char c:field){if(c=='.' || c=='[')pointer+='/';else if(c!=']')pointer+=c;}
    // Preview historically uses linear interpolation for an omitted flag; solver defaults stay intact.
    if(!draft.profileFields.contains(pointer+"/interpolation_logarithmic"))profile.interpolation_logarithmic=false;
    if(!profile.shared_range)return profile;
    const auto device=scaps_core::make_device_from_project(draft.project);
    for(std::size_t i=0;i<device.layers.size();++i)if(draft.layers[i].id==layerId && device.layers[i].grading) {
        const auto& g=*device.layers[i].grading;const auto& resolved=parameter=="composition"?g.composition:parameter=="Na_m3"?g.Na_m3:g.Nd_m3;
        if(resolved){profile.left=resolved->left;profile.right=resolved->right;profile.shared_range=false;
            if(profile.interpolation_logarithmic!=resolved->interpolation_logarithmic){profile.left=scaps_core::evaluate_position_profile(profile,resolved->shared_offset_m,resolved->shared_thickness_m);profile.right=scaps_core::evaluate_position_profile(profile,resolved->shared_offset_m+draft.project.layers[i].thickness_m,resolved->shared_thickness_m);}
            profile.shared_offset_m=resolved->shared_offset_m;profile.shared_thickness_m=resolved->shared_thickness_m;}
    }return profile;
}
void ExportProfile(const ProfileSamplesData& samples,const std::string& path,const std::string& parameter) {
    const bool grd=std::filesystem::path(path).extension()==".grd";std::ostringstream out;out<<std::setprecision(17);
    if(grd)out<<"position: absolute\ninterpolation: linear\nrange: this layer only\n";else out<<"x_m,"<<parameter<<'\n';
    for(std::size_t i=0;i<samples.x_m.size();++i)out<<samples.x_m[i]*(grd?1e6:1.)<<(grd?" ":",")<<samples.value_SI[i]<<'\n';SaveText(path,out.str());
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
    const auto device=scaps_core::make_device_from_project(DraftFromDocument(document)->project);
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
namespace {
Json ImportProject(Json raw,const std::filesystem::path& base,std::vector<std::string>* importWarnings) {
    if(raw.value("schema_version",0)==0) raw["schema_version"]=1;
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
    const auto bounded=DraftFromDocument(raw,true);
    if(importWarnings)*importWarnings=bounded->importWarnings;
    if(!bounded->importWarnings.empty())raw=DraftDocument(*bounded);
    const auto p=Model(raw);
    auto full=FromYaml(scaps_core::device_project_to_node(p));
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
}
Json ReadProject(const std::string &path,std::vector<std::string>* importWarnings) {return ImportProject(FromYaml(YAML::LoadFile(path)),std::filesystem::absolute(path).parent_path(),importWarnings);}
Json NormalizeDocument(Json document) {
    // Older workspace documents bypass ReadProject's Core defaults.
    if(!document.contains("illumination") || !document.at("illumination").contains("mode") ||
        !document.at("illumination").contains("intensity_multiplier") || !document.at("illumination").contains("uniform_generation_m3_s") ||
        !document.at("illumination").contains("front_transmission") || !document.at("illumination").contains("wavelength_nm")) {
        const auto model=Model(document);
        auto light=FromYaml(scaps_core::device_project_to_node(model)).at("illumination");
        if(document.contains("illumination")) light.update(document.at("illumination"),true);
        document["illumination"]=std::move(light);
    }
    return document;
}
Json NewDeviceDocument() {auto d=ReadProject(SCAPSLAB_PROJECT);for(const auto *side:{"left","right"}) d["contacts"][side]["flat_band"]=true;return d;}
void WriteProject(const Json &j,const std::string &path) { (void)Model(j); SaveText(Path{path}.Native(),Text(SolverDocument(j))); }
namespace {
std::vector<DefectPreview> DefectCurves(const ProjectDraft &draft,const scaps_core::DeviceProject &p,const scaps_core::DeviceState &state) {
    using namespace scaps_core;
    std::vector<DefectPreview> curves; const auto pc=physical_constants(p.constants_mode);
    auto curve=[&](const auto &d,const std::string& id,double ev,double ec,double ei,double total) {
        constexpr bool sheet=std::is_same_v<std::decay_t<decltype(d)>,ProjectInterfaceDefect>;
        const auto shape=d.energy_distribution;
        const double offset=d.energy_offset_eV,width=d.characteristic_energy_eV;
        double center=ei+offset;
        if constexpr(sheet) {
            const int ref=d.energy_reference;
            const int l=d.left_layer_index; const auto &a=p.layers[l].material,&b=p.layers[l+1].material;
            const auto ta=evaluate_thermal_material(a,p.temperature_K,pc),tb=evaluate_thermal_material(b,p.temperature_K,pc);
            const double va=-a.chi_eV-a.Eg_eV,vb=-b.chi_eV-b.Eg_eV,ca=-a.chi_eV,cb=-b.chi_eV;
            center=(ref==3?std::max(va,vb):ref==4?std::min(ca,cb):ref==5?Ei_of(ca,va,ta.Nc_m3,ta.Nv_m3,ta.Vt_V):ref==6?Ei_of(cb,vb,tb.Nc_m3,tb.Nv_m3,tb.Vt_V):ref==8?vb:ref==9?.5*(std::max(va,vb)+std::min(ca,cb)):va)+(ref==4?-offset:offset);
        } else {
            const auto ref=d.energy_reference;
            center=ref==DefectEnergyReference::RelativeToEc?ec-offset:ref==DefectEnergyReference::RelativeToEv?ev+offset:ei+offset;
        }
        const bool single=shape=="single",gaussian=shape=="gaussian",cb=shape=="cb_tail",vb=shape=="vb_tail";
        double lo=std::max(ev,center-(cb?d.tail_width*width:vb?0:.5*width*(gaussian?d.gaussian_width:1)));
        double hi=std::min(ec,center+(vb?d.tail_width*width:cb?0:.5*width*(gaussian?d.gaussian_width:1)));
        // Interface quadrature spans its reference-coordinate interval, without bulk gap clipping.
        if constexpr(sheet) {lo=center-.5*width*(gaussian?d.gaussian_width:1);hi=2*center-lo;}
        auto mass=[&](double e) {return gaussian?.5*std::erf((e-center)/width):cb?std::exp((e-center)/width):vb?-std::exp((center-e)/width):(e-center)/width;};
        double amplitude=total/width;
        if constexpr(sheet) {if(d.density_convention=="scaps_peak")amplitude=d.peak_density_m2_eV*(gaussian?std::sqrt(std::numbers::pi):1);else if(hi>lo)amplitude/=mass(hi)-mass(lo);}
        else if(d.distribution_normalization=="normalized_continuum" && hi>lo)amplitude/=mass(hi)-mass(lo);
        DefectPreview c;c.id=id;c.sheet=sheet;c.Ev_eV=ev;c.Ec_eV=ec;
        if constexpr(sheet)c.label=d.id;else {c.label=d.label;c.amphoteric=d.amphoteric;c.correlated=d.correlated;}
        c.single=single;c.level_energy_eV=center;c.Nt=total;
        if(!single) for(int i=0;i<=400;++i) {
            const double e=ev+(ec-ev)*i/400.;double value=0;
            if(e>=lo && e<=hi) value=amplitude*(gaussian?std::exp(-std::pow((e-center)/width,2))/std::sqrt(std::numbers::pi):cb?std::exp((e-center)/width):vb?std::exp((center-e)/width):1);
            c.energy_eV.push_back(e-ev);c.density.push_back(value);
        }
        c.level_energy_eV=center-ev;c.band_edges_eV={0.,ec-ev};
        if constexpr(sheet) {
            const int l=d.left_layer_index;const auto &a=p.layers[l].material,&b=p.layers[l+1].material;
            c.band_edges_eV={-a.chi_eV-a.Eg_eV-ev,-a.chi_eV-ev,-b.chi_eV-b.Eg_eV-ev,-b.chi_eV-ev};
        }
        if constexpr(!sheet)if(d.amphoteric)c.second_level_energy_eV=center-ev+(d.second_energy_offset_eV-offset)*(d.energy_reference==DefectEnergyReference::RelativeToEc?-1:1);
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
            auto c=curve(l.defects[di],draft.layers[li].defects[di].id,ev,ec,ei,l.defects[di].Nt_m3);
            c.layer_id=draft.layers[li].id;
            for(std::size_t k=0;k<levels.size();++k) {
                const auto &level=levels[k];const double e=level.energy_reference==DefectEnergyReference::RelativeToEc?ec-level.energy_offset_eV:level.energy_reference==DefectEnergyReference::RelativeToEv?ev+level.energy_offset_eV:ei+level.energy_offset_eV;
                c.occupation_energy_eV.push_back(e-ev);
                c.occupation.push_back(node<state.trap_occupation.size() && levelOffset+k<state.trap_occupation[node].size()?state.trap_occupation[node][levelOffset+k]:double(NAN));
            }
            levelOffset+=levels.size();curves.push_back(std::move(c));
        }
    }
    std::size_t offset=0;
    for(std::size_t i=0;i<p.interface_defects.size();++i) {
        const auto &d=p.interface_defects[i];const auto &a=p.layers[d.left_layer_index].material,&b=p.layers[d.left_layer_index+1].material;
        const double ev=std::min(-a.chi_eV-a.Eg_eV,-b.chi_eV-b.Eg_eV),ec=std::max(-a.chi_eV,-b.chi_eV);
        auto c=curve(d,d.id,ev,ec,(ev+ec)*.5,d.Nt_m2);c.left_layer_index=d.left_layer_index;
        auto isolated=p;isolated.interface_defects={d};const auto levels=make_device_from_project(isolated).interface_defects;
        for(std::size_t k=0;k<levels.size();++k) {
            auto sample=d;sample.energy_offset_eV=levels[k].energy_offset_eV;
            const auto single=curve(sample,sample.id,ev,ec,(ev+ec)*.5,d.Nt_m2);
            c.occupation_energy_eV.push_back(single.level_energy_eV);
            c.occupation.push_back(offset+k<state.interface_occupation.size()?state.interface_occupation[offset+k]:double(NAN));
        }
        double integrated=0;for(const auto &level:levels) integrated+=level.Nt_m2;c.Nt=integrated;
        offset+=levels.size();curves.push_back(std::move(c));
    }
    return curves;
}
GenerationRecombinationResult GenerationRecombination(const ProjectDraft &draft,const scaps_core::DeviceProject &p,const scaps_core::Device1D &device,const scaps_core::DeviceState &state) {
    using namespace scaps_core;
    const auto mesh=mesh_from_coordinates(device,state.x_m);
    const auto pc=physical_constants(p.constants_mode);
    const auto generation=generation_integrals_m2s(device,mesh,pc);
    GenerationRecombinationResult out;out.x_m=Column::Intern(state.x_m);std::vector<double> density;
    double generated=0,recombined=0;
    for(std::size_t l=0;l<p.layers.size();++l) out.layers.push_back({draft.layers[l].id});
    for(std::size_t i=0;i<generation.size();++i) {
        const double dx=.5*((i?mesh.edges[i-1].h_m:0)+(i+1<mesh.size()?mesh.edges[i].h_m:0));
        density.push_back(dx>0?generation[i]/dx:0.);
        const double j=pc.q_C*generation[i];generated+=j;
        out.layers[mesh.nodes[i].layer_index].generation_current_A_m2+=j;
    }
    for(const auto &channel:state.recombination) {
        std::string id=channel.id;int layer=-1,defect=-1;
        if(channel.mechanism=="srh_bulk" && std::sscanf(id.c_str(),"layer:%d/defect:%d",&layer,&defect)==2 && layer>=0 && defect>=0 && layer<int(p.layers.size()) && defect<int(draft.layers[layer].defects.size()))
            id=draft.layers[layer].defects[defect].id;
        std::vector<LayerCurrent> byLayer;double sum=0;
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
            sum+=value;byLayer.push_back({draft.layers[l].id,value});
            auto &row=out.layers[l];row.recombination_current_A_m2+=value;
            row.mechanisms[channel.mechanism]+=value;
        }
        recombined+=channel.integrated_current_A_m2;
        RecombinationChannel c;c.id=std::move(id);c.engine_id=channel.id;c.mechanism=channel.mechanism;
        c.x_m=Column::Intern(channel.x_m.empty()?state.x_m:channel.x_m);c.R_m3_s=Column::Intern(channel.R_m3_s);c.R_sheet_m2_s=Column::Intern(channel.R_sheet_m2_s);
        c.integrated_current_A_m2=channel.integrated_current_A_m2;c.by_layer=std::move(byLayer);c.layer_sum_relative_error=std::abs(sum-channel.integrated_current_A_m2)/std::max(1e-30,std::abs(channel.integrated_current_A_m2));
        out.channels.push_back(std::move(c));
    }
    out.generation_m3_s=Column::Intern(std::move(density));out.generation_current_A_m2=generated;out.recombination_current_A_m2=recombined;out.net_generation_current_A_m2=generated-recombined;
    out.contact_recombination=state.contact_recombination;

    return out;
}

}
// Photovoltaic metrics read off the solved sweep (linear interpolation, no extra solves): shown at once and marked "estimate"; the exact refinement replaces them.
// Same conventions as the exact result: voltage = plotted (consumer) voltage, Jsc = -J(0), Pmax = max(-V*J), Jmpp native sign.
PvEstimate EstimatePv(const std::vector<double> &V,const std::vector<double> &J,double power) {
    PvEstimate e;const std::size_t n=std::min(V.size(),J.size());
    const auto ok=[&](std::size_t i){return std::isfinite(V[i]) && std::isfinite(J[i]);};
    double j0=NAN;
    for(std::size_t i=0;i<n && !std::isfinite(j0);++i) {
        if(!ok(i)) continue;
        if(V[i]==0) j0=J[i];
        else if(i+1<n && ok(i+1) && (V[i]<0)!=(V[i+1]<0) && V[i+1]!=0) j0=J[i]+(J[i+1]-J[i])*(0-V[i])/(V[i+1]-V[i]);
    }
    double voc=NAN;
    for(std::size_t i=0;i+1<n;++i) {
        if(!ok(i) || !ok(i+1)) continue;
        if(J[i]==0 && V[i]!=0) {if(!std::isfinite(voc) || std::abs(V[i])<std::abs(voc)) voc=V[i];}
        else if((J[i]<0)!=(J[i+1]<0) && J[i+1]!=0 && J[i]!=0) {const double x=V[i]+(V[i+1]-V[i])*(0-J[i])/(J[i+1]-J[i]);if(x!=0 && (!std::isfinite(voc) || std::abs(x)<std::abs(voc))) voc=x;}
    }
    if(!std::isfinite(j0) || !std::isfinite(voc)) return e;
    double pmax=0,vm=NAN,jm=NAN;
    for(std::size_t i=0;i<n;++i) {
        if(!ok(i)) continue;
        const double between=voc>0?(V[i]>=0 && V[i]<=voc):(V[i]<=0 && V[i]>=voc);
        if(!between) continue;
        if(-V[i]*J[i]>pmax) {pmax=-V[i]*J[i];vm=V[i];jm=J[i];}
    }
    e.Voc_V=voc;e.Jsc_A_m2=-j0;e.Pmax_Wm2=pmax>0?pmax:NAN;e.Vmpp_V=vm;e.Jmpp_A_m2=jm;
    if(pmax>0 && j0!=0) e.FF=pmax/std::abs(voc*j0);
    if(pmax>0 && power>0) e.eta=pmax/power;
    e.valid=true;return e;
}
void Operation::Execute(JobContext &ctx) try {
    using namespace scaps_core;
    const auto started=std::chrono::steady_clock::now();
    ctx.ThrowIfCancellationRequested();if(cancellation.stop_requested())throw JobCancelledException();
    std::stop_callback cancel(ctx.GetStopToken(),[this]{cancellation.request_stop();});
    JobExecutor executor{ctx};
    // Engine calls are synchronous; nested execution scopes only need a cheap executor reference.
    ExecutionContext execution{std::ref(executor),parallelMember?1:ctx.GetWorkerCount(),cancellation.get_token()};
    scaps_core::detail::ExecutionScope engineScope(execution,false);
    if(runPlan && !action && (request.at("operation")=="run_batch" || request.at("operation")=="quick_sweep" || request.at("operation")=="retry_batch")) {BatchOperation(*this,ctx);return;}
    if(runPlan && !action) {
        const auto original=draft;bool valid=completedActions.empty()?true:outputValid;
        if(batchGroup.empty())batchGroup="single-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        try {
            for(std::size_t i=nextAction;i<runPlan->actions.size();++i) {
                const auto& next=runPlan->actions[i];if(!(automatic?next.onEdit:next.include))continue;
                action=next;draft=original;
                if(next.temperature_K>0) {auto d=CreateRef<ProjectDraft>(*original);d->project.temperature_K=next.temperature_K;draft=std::move(d);}
                measurement=PrepareBatchMember(draft->project,*batch,scaps_core::BatchSpec{});
                typedOut.reset();output=Json{};
                try{Execute(ctx);}catch(const JobCancelledException&){
                    const auto partial=std::dynamic_pointer_cast<const AcResult>(typedOut);
                    if(partial && partial->dlts && !partial->dlts->traces.empty() && partial->meta.prim.contains("provenance")) {
                        auto result=std::const_pointer_cast<AcResult>(partial);result->runPlan=runPlan;completedActions.emplace_back(next.kind,std::move(result));
                    }
                    throw;
                }
                valid &=outputValid;
                auto result=std::const_pointer_cast<ResultBase>(typedOut);result->runPlan=runPlan;result->liveOnly=automatic;
                completedActions.emplace_back(next.kind,std::move(result));
                nextAction=i+1;
                const bool more=std::any_of(runPlan->actions.begin()+i+1,runPlan->actions.end(),[&](const auto& a){return automatic?a.onEdit:a.include;});
                if(!automatic && runPlan->pauseAfterAction && more) {
                    paused=true;ctx.SetStage(std::string("Paused after ")+ActionId(next.kind));
                    // Return the worker to Core; the device lane retains the plan until Resume or Stop.
                    draft=original;action.reset();outputValid=valid;return;
                }
            }
        } catch(...) {draft=original;action.reset();throw;}
        nextAction=runPlan->actions.size();draft=original;action.reset();outputValid=valid;return;
    }
    const std::string op=action?ActionOperation(action->kind):request.at("operation").get<std::string>();
    if(action && parallelMember && !retainProfiles && (op=="run" || op=="band")) {
        const auto& p=draft->project;auto spec=measurement.value_or(BatchSpec{});spec.voltages_V=ActionSamples(*action,p);spec.frequencies_Hz.clear();spec.columns.clear();spec.photovoltaic=op=="run" && refinePv;
        BatchOptions options;options.include_curves=true;options.parallel_member=true;options.execution=execution;options.execution.budget=1;
        auto acquired=run_batch_member(p,spec,options);if(acquired.status=="Cancelled")throw JobCancelledException();
        if(acquired.iv.empty())throw std::runtime_error(acquired.message.empty()?"No Action samples":acquired.message);
        auto result=CreateRef<IvResult>();result->has=1u<<31;result->photovoltaic=std::move(acquired.photovoltaic);result->curve_series_ohm_m2=p.external.series_ohm_m2;
        const bool external=p.illumination.enabled || p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);
        if(!external)for(auto& sample:acquired.iv)sample.J_Am2=sample.J_left_contact_Am2;
        result->curve=std::move(acquired.iv);
        output={{"valid",true},{"elapsed_s",acquired.elapsed_s}};result->meta.prim=output;typedOut=std::move(result);outputValid=true;return;
    }
    const auto prepareMeasurement=[&](auto& device,const auto& p,const auto& voltages,const auto& temperatures,const auto& meshConfig,const auto& pc) {
        auto mesh=liveMeshInput?mesh_from_coordinates(device,*liveMeshInput):measurement && measurement->mesh_coordinates_m?mesh_from_coordinates(device,*measurement->mesh_coordinates_m):prepare_mesh(device,meshConfig,voltages,temperatures,p.solver,pc);
        if(action && (measurement || !p.metastable_transitions.empty())) {
            auto spec=measurement.value_or(scaps_core::BatchSpec{});
            if(spec.conditioning || !p.metastable_transitions.empty()) {
                auto input=make_batch_conditioning(p,spec.conditioning);
                if(spec.initial_state){input.initial_temperature_K=spec.initial_state->temperature_K;input.initial_voltage_V=spec.initial_state->voltage_V;input.initial_illumination=spec.initial_state->illumination;}
                BatchConditioningMetadata metadata;mesh=condition_batch_member(p,device,mesh,input,metadata,false);
            }
        }
        if(automatic && !liveMeshInput)liveMeshOutput=CreateRef<const std::vector<double>>(mesh.x_m);
        return mesh;
    };
    if(draft && op=="save")document=DraftDocument(*draft);
    struct Phase {bool on=std::getenv("SCAPSLAB_PERF_PHASES")!=nullptr;Json log=Json::object();std::chrono::steady_clock::time_point t=std::chrono::steady_clock::now();void lap(const char *n){if(!on)return;const auto now=std::chrono::steady_clock::now();log[n]=log.value(n,0.)+std::chrono::duration<double,std::milli>(now-t).count();t=now;}} phase; // opt-in per-phase timing (SCAPSLAB_PERF_PHASES=1) -> output.perf_phase_ms
    if(op=="load_material" || op=="save_material") {
        auto next=CreateRef<ProjectDraft>(*draft);auto& model=next->project;std::size_t index=0;
        while(index<next->layers.size() && next->layers[index].id!=request.at("layer_id").get<std::string>())++index;
        if(index==model.layers.size())throw std::invalid_argument("Unknown layer_id");
        const auto selected=request.value("selection",std::string("both"));
        if(selected!="both" && selected!="A" && selected!="B")throw std::invalid_argument("selection must be A, B or both");
        const auto selection=selected=="A"?MaterialSelection::PureA:selected=="B"?MaterialSelection::PureB:MaterialSelection::Both;
        if(op=="save_material")write_scaps_material(make_scaps_material(model.layers[index]),request.at("path").get<std::string>(),selection);
        else {apply_scaps_material(read_scaps_material(request.at("path").get<std::string>()),model.layers[index],selection);ValidateDraft(*next);draft=std::move(next);}
        output={{"path",request.at("path")},{"selection",selected}};return;
    }
    if(op=="set_profile" || op=="load_profile" || op=="preview_profile" || op=="export_profile") {
        auto next=CreateRef<ProjectDraft>(*draft);auto& project=next->project;std::optional<Profile>* target=nullptr;const Profile* composition=nullptr;double thickness=0.;
        const auto parameter=request.at("parameter_id").get<std::string>();std::string layerId=request.value("layer_id",std::string{});
        if(parameter=="generation_m3_s") {for(const auto& l:project.layers)thickness+=l.thickness_m;}
        else {
            for(std::size_t i=0;i<next->layers.size();++i)if(next->layers[i].id==layerId) {
                auto& layer=project.layers[i];thickness=layer.thickness_m;
                if(layer.grading && layer.grading->composition)composition=&*layer.grading->composition;
                if(parameter=="Nt_m3") {for(std::size_t k=0;k<next->layers[i].defects.size();++k)if(next->layers[i].defects[k].id==request.at("defect_id").get<std::string>())target=&layer.defects[k].density_profile_m3;}
                else {if(!layer.grading)layer.grading.emplace();target=&GradingProfile(*layer.grading,parameter);}
            }
            if(!target)throw std::invalid_argument("Unknown layer_id or defect_id");
        }
        if(op=="set_profile" || op=="load_profile") {
            if(parameter=="generation_m3_s") {
                project.illumination.mode="generation_file";project.illumination.generation_m_m3s=op=="load_profile"?read_scaps_generation(request.at("path")):request.at("generation_m_m3s").get<OpticalTable>();project.illumination.generation_file.clear();
                if(op=="load_profile")next->extensions["/illumination/scapslab_generation_file"]=request.at("path");
            } else {
                if(op=="load_profile") {
                    auto loaded=ReadProfileFile(request.at("path"),parameter.find("_m3")!=std::string::npos);*target=std::move(loaded.profile);ResetProfileFields(*next,**target,{"interpolation_logarithmic","position_relative","shared_range","composition_dependent"});
                    const auto field=ProjectFieldPath(project,&**target);std::string pointer="/";for(char c:field){if(c=='.' || c=='[')pointer+='/';else if(c!=']')pointer+=c;}next->extensions[pointer+"/scapslab_warnings"]=loaded.warnings;
                }else {
                    *target=ProfileFromJson(request.at("profile"));std::set<std::string> fields;for(const auto& [key,value]:request.at("profile").items())fields.insert(key);
                    if(!fields.contains("interpolation_logarithmic") && !fields.contains("file"))(*target)->interpolation_logarithmic=parameter.find("_m3")!=std::string::npos;
                    if(request.at("profile").contains("file"))for(const auto* name:{"interpolation_logarithmic","position_relative","shared_range","composition_dependent"})fields.insert(name);ResetProfileFields(*next,**target,fields);
                }
                if(op=="load_profile" && !request.value("project_path",std::string{}).empty()) {std::error_code ec;const auto relative=std::filesystem::relative(request.at("path").get<std::string>(),std::filesystem::path(request.at("project_path").get<std::string>()).parent_path(),ec);if(!ec)(*target)->source_file=relative.generic_string();}
            }
            ValidateDraft(*next);draft=std::move(next);output={{"parameter_id",parameter}};
        }else {
            ProfileSamplesData samples;
            if(parameter=="generation_m3_s") {
                Profile profile;profile.law=GradingLaw::Table;profile.position_relative=false;for(const auto& [x,value]:project.illumination.generation_m_m3s){profile.coordinate.push_back(x*1e6);profile.value.push_back(value);}samples=ProfileSamples(profile,thickness);
            }else {
                if(!*target)throw std::invalid_argument("Profile is not active");auto profile=ResolveSharedProfile(**target,*next,layerId,parameter);
                if(parameter!="composition" && parameter!="Nd_m3" && parameter!="Na_m3" && parameter!="Nt_m3")profile.composition_dependent=true;
                auto c=composition?std::optional<Profile>(ResolveSharedProfile(*composition,*next,layerId,"composition")):std::nullopt;samples=ProfileSamples(profile,thickness,c?&*c:nullptr);
            }
            output={{"x_m",samples.x_m},{"value_SI",samples.value_SI},{"source_file",samples.source_file},{"source_point_count",samples.source_point_count},{"parameter_id",parameter}};
            output["value_unit"]=parameter=="generation_m3_s"?"m^-3 s^-1":parameter.find("_m3")!=std::string::npos?"m^-3":parameter.find("_eV")!=std::string::npos?"eV":parameter.find("_m2_V_s")!=std::string::npos?"m^2/(V s)":parameter.find("vth_")==0?"m/s":"1";
            if(op=="export_profile")ExportProfile(samples,request.at("path"),parameter);
        }
        return;
    }

    if(op=="export_results") {
        if(exportError)std::rethrow_exception(exportError);
        const auto files=ExportResults(exportRuns,request.at("path").get<std::string>(),request.value("format",std::string("csv")));
        output={{"export_status","saved"},{"run_count",exportRuns.size()},{"files",Json::array()}};
        for(const auto& file:files)output["files"].push_back({{"path",file.string()},{"size_bytes",std::filesystem::file_size(file)}});
    }
    else if(BatchOperation(*this,ctx)) {}
    else if(op=="export_gr") {
        const auto workspace=ivInput?ivInput->Dump():Json::object(); // G/R file codec
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
        const auto imported=import_def(request.at("path").get<std::string>(),request.value("scaps_root",std::string{}));
        draft=DraftFromProject(imported,true);
        output={{"warnings",imported.scaps_native.warnings}};
    } else if(op=="export_def") {export_def(draft->project,request.at("path").get<std::string>());output={{"warnings",draft->project.scaps_native.warnings}};}
    else if(op=="project_new") {
        workspace=SingleDeviceWorkspace(NewDeviceDocument(),"Untitled project");for(const auto& d:workspace.at("devices"))loadedDrafts.push_back(DraftFromDocument(d.at("document")));loaded=ExtractResults(workspace);
    }
    else if(op=="project_load" || op=="load") {
        const auto file=request.at("path").get<std::string>();
        std::string source,error;if(!Io::Load(file,source,error)) throw std::runtime_error(error);
        auto raw=source.find_first_not_of(" \t\r\n")!=std::string::npos && source[source.find_first_not_of(" \t\r\n")]=='{'?Json::parse(source):FromYaml(YAML::Load(source));
        std::vector<std::vector<std::string>> importedWarnings;
        if(raw.contains("devices")) {
            for(auto& item:raw["devices"]) {
                const auto bounded=DraftFromDocument(item.at("document"),true);importedWarnings.push_back(bounded->importWarnings);
                if(!bounded->importWarnings.empty())item["document"]=DraftDocument(*bounded);
            }
            workspace=UnpackResults(raw);workspace["schema_version"]=raw.value("schema_version",0);workspace=NormalizeWorkspace(std::move(workspace));
        }else {
            std::vector<std::string> warnings;auto document=ReadProject(file,&warnings);importedWarnings.push_back(std::move(warnings));
            workspace=SingleDeviceWorkspace(document,std::filesystem::path(file).stem().string());
        }
        for(const auto& item:workspace.at("devices")) {
            auto read=CreateRef<ProjectDraft>(*DraftFromDocument(item.at("document"),true));
            if(loadedDrafts.size()<importedWarnings.size())read->importWarnings=std::move(importedWarnings[loadedDrafts.size()]);
            loadedDrafts.push_back(std::move(read));
        }
        if(workspace.contains("asset_library_schema"))Assets::Require(workspace.at("asset_library_schema")==1,"Unsupported project asset library schema");
        if(workspace.contains("asset_libraries")){assetState=Library::Python::ReadState(workspace.at("asset_libraries"));workspace.erase("asset_libraries");}
        if(workspace.contains("asset_browser")){assetBrowser=Library::Python::ReadBrowser(workspace.at("asset_browser"));workspace.erase("asset_browser");}
        if(workspace.contains("asset_editor_drafts")){assetDrafts=Library::Python::ReadDrafts(workspace.at("asset_editor_drafts"),assetState?*assetState:*Library::Builtins());workspace.erase("asset_editor_drafts");}
        loaded=ExtractResults(workspace); // typed results leave the Json here, on the job thread
    }
    else if(op=="project_save" || (op=="save" && std::filesystem::path(request.at("path").get<std::string>()).extension()==".scapslab")) {
        workspace["schema_version"]=4;
        if(assetState && Library::NeedsPersistence(*assetState,assetBrowser,assetDrafts.get())){workspace["asset_library_schema"]=1;workspace["asset_libraries"]=Library::Python::StateDocument(*assetState);workspace["asset_browser"]=Library::Python::BrowserDocument(assetBrowser);if(assetDrafts && !assetDrafts->empty())workspace["asset_editor_drafts"]=Library::Python::DraftsDocument(*assetDrafts);}
        if(!shared.empty()) {auto &devs=workspace["devices"];for(std::size_t i=0;i<shared.size();++i) AttachResults(devs[i],*shared[i].history,shared[i].results,false,true);shared.clear();}
        ValidateWorkspace(workspace);
        if(!request.value("include_results",true)) for(auto &d:workspace["devices"]) for(const auto *key:{"result","band_result","ac_results","batch_result"}) d[key]=Json::object();
        if(!request.value("include_results",true)) for(auto &d:workspace["devices"]) {d["status"]="no results";d["history"]=Json::array();d["selected_run"]="";d.erase("stored_results");}
        CompactHistory(workspace);
        const auto text=WorkspaceText(PackResults(workspace));
        ctx.ThrowIfCancellationRequested();
        std::string backup;const auto target=request.at("path").get<std::string>();
        if(!request.value("autosave",false) && Io::Exists(target)) {
            std::string original,error;if(!Io::Load(target,original,error))throw std::runtime_error(error);
            const auto first=original.find_first_not_of(" \t\r\n");const auto old=first!=std::string::npos && original[first]=='{'?Json::parse(original):FromYaml(YAML::Load(original));const int version=old.value("schema_version",0);
            if(!old.contains("devices") || version<4)backup=Io::BackupOriginal(target,version).string();
        }
        SaveText(Path{target}.Native(),text);
        output={{"path",std::filesystem::absolute(request.at("path").get<std::string>()).string()},{"size_bytes",text.size()},{"save_status","saved"}};
        output["schema_version"]=4;output["original_copy"]=backup;
    }
    else if(op=="device_read") {std::vector<std::string> warnings;document=ReadProject(request.at("path").get<std::string>(),&warnings);auto read=CreateRef<ProjectDraft>(*DraftFromDocument(document,true));read->importWarnings=std::move(warnings);draft=std::move(read);} // device_add from a file: only the read runs as a job
    else if (op=="save") WriteProject(document,request.at("path").get<std::string>());
    else if(op=="edit") {if(!draft)throw std::invalid_argument("No device document");ValidateDraft(*draft);
    }else if(op=="layer") {
        auto next=CreateRef<ProjectDraft>(*draft);EditLayer(*next,request.at("action"),request.value("layer_id",std::string{}),request.value("index",0));ValidateDraft(*next);draft=std::move(next);
    } else if (op=="export_ac") {
        const auto kind=request.at("kind").get<std::string>();
        if ((kind!="cv" && kind!="cf") || !acInput) throw std::invalid_argument("No AC result for kind");
        const auto result=acInput->Dump(); // AC CSV file codec
        std::ostringstream csv; csv << std::setprecision(17);
        csv << "point_id,voltage_V,frequency_Hz,temperature_K,capacitance_F,conductance_S,G_over_omega_F,inverse_C2_F_minus2,status,valid,validity_status,conservation_relative,area_m2\n";
        for (auto pt:result.at("points")) {
            pt["area_m2"]=result.at("area_m2");
            ctx.ThrowIfCancellationRequested();
            for(const auto *key:{"point_id","voltage_V","frequency_Hz","temperature_K","capacitance_F","conductance_S","G_over_omega_F","inverse_C2_F_minus2","status","valid","validity_status","conservation_relative","area_m2"}) {
                if (std::string(key)!="point_id") csv << ',';
                const auto &v=pt.at(key); if(v.is_string()) csv << v.get<std::string>(); else if(!v.is_null()) csv << v.dump();
            }
            csv << '\n';
        }
        ctx.ThrowIfCancellationRequested(); SaveText(Path{request.at("path").get<std::string>()}.Native(),csv.str());
    } else if(op=="export_dlcp") {
        if(!dlcpInput || dlcpInput->dlcp.empty())throw std::invalid_argument("No DLCP result to export");
        std::ostringstream csv;csv<<std::setprecision(17);
        csv<<"point_id,V_max_V,x_um,C0_F,C1_F_per_V,C2_F_per_V2,N_DL_m3,fit_relative_rms,status,cv_x_um,N_CV_m3,frequency_Hz,temperature_K,Vpp_V,C_F,G_S,periods,periodic_error,current_conservation_relative\n";
        const auto number=[&](double x){if(std::isfinite(x))csv<<x;csv<<',';};
        for(std::size_t i=0;i<dlcpInput->dlcp.size();++i) {
            const auto& row=*dlcpInput->dlcp[i];const auto& p=row.value;
            for(const auto& s:p.samples) {
                csv<<"dlcp-"<<i<<',';for(double x:{p.V_max_V,p.x_m*1e6,p.C0_F,p.C1_F_per_V,p.C2_F_per_V2,p.N_DL_m3,p.fit_relative_rms})number(x);
                csv<<to_string(p.status)<<',';number(row.cv_x_m*1e6);number(row.N_CV_m3);number(dlcpInput->points.at(i).frequency_Hz);number(dlcpInput->points.at(i).temperature_K);
                for(double x:{s.amplitude_V,s.C_F,s.G_S,double(s.periods),s.periodic_error})number(x);
                if(std::isfinite(s.current_conservation_relative))csv<<s.current_conservation_relative;csv<<'\n';
            }
        }
        ctx.ThrowIfCancellationRequested();SaveText(Path{request.at("path").get<std::string>()}.Native(),csv.str());
    } else if(action && IsDlts(action->kind)) {
        auto device=make_device_from_project(draft->project);const auto& p=draft->project;const auto pc=physical_constants(p.constants_mode);
        const auto temperatures=(action->dlts.series==DltsSeries::Temperature || action->dlts.series==DltsSeries::DoublePulse)?ActionSamples(*action,p):std::vector<double>{p.temperature_K};
        std::vector<double> biases={-action->dlts.reverse_V,-action->dlts.pulse_V,-action->dlts.second_pulse_V};if(action->dlts.series==DltsSeries::Depth)for(double bias:ActionSamples(*action,p))biases.push_back(-bias);
        const auto mesh=prepareMeasurement(device,p,biases,temperatures,p.mesh,pc);
        SolveDlts(*this,ctx,mesh);
    } else if(op=="dlcp") {
        if(!draft)throw std::invalid_argument("No device document");const auto& p=draft->project;
        if(!action)throw std::logic_error("DLCP requires a typed RunAction");const auto& controls=*action;
        auto config=controls.dlcp;config.frequency_Hz=controls.frequency_Hz;config.amplitudes_V=SweepValues(controls.amplitudes);
        const auto maxima=ActionSamples(controls,p);const double T=controls.temperature_K>0?controls.temperature_K:p.temperature_K;
        // A zero override uses the thickest layer (the absorber in the bundled devices).
        if(config.profiling_eps_r==0)config.profiling_eps_r=std::max_element(p.layers.begin(),p.layers.end(),[](const auto& a,const auto& b){return a.thickness_m<b.thickness_m;})->material.eps_r;
        const auto pc=physical_constants(p.constants_mode);auto device=make_device_from_project(p);
        std::vector<double> biases;for(double v:maxima){biases.push_back(-v);biases.push_back(config.amplitudes_V.back()-v);}
        ctx.SetStage("Preparing DLCP mesh");const auto mesh=prepareMeasurement(device,p,biases,std::vector<double>{T},p.mesh,pc);ctx.ThrowIfCancellationRequested();
        auto result=CreateRef<AcResult>();result->has=7u|(1u<<31);result->kind="dlcp";result->mesh_x_m=Column::Intern(mesh.x_m);result->area_m2=device.external.area_m2;result->dlcp_eps_r=config.profiling_eps_r;typedOut=result;
        const double eps=pc.eps0_Fm*config.profiling_eps_r,area=device.external.area_m2;
        output={{"amplitude_convention","peak_to_peak_V"},{"voltage_convention","Vdc = Vmax - Vpp/2"}};
        for(std::size_t i=0;i<maxima.size();++i) {
            ctx.ThrowIfCancellationRequested();ctx.SetStage("DLCP point "+std::to_string(i+1)+" / "+std::to_string(maxima.size())+" (PSS)");
            config.V_max_V={maxima[i]};auto solved=solve_dlcp(device,mesh,config,T,p.solver,pc);ctx.ThrowIfCancellationRequested();
            auto row=CreateRef<DlcpProfilePoint>();row->value=std::move(solved.at(0));
            ctx.SetStage("C-V reference at Vmax "+std::to_string(maxima[i])+" V");
            // Symmetric 10 mV finite difference, same device, mesh, frequency, T and illumination.
            const double h=.005;const auto cv=solve_cv(device,mesh,{-maxima[i]+h,-maxima[i],-maxima[i]-h},T,config.frequency_Hz,p.solver,pc);
            ctx.ThrowIfCancellationRequested();const auto& mid=cv.at(1);
            AcPoint point;point.has=((1u<<13)-1)|(1u<<15);point.point_id="cv-"+std::to_string(i);point.voltage_V=maxima[i];point.frequency_Hz=config.frequency_Hz;point.temperature_K=T;point.capacitance_F=mid.C_F;point.conductance_S=mid.G_S;point.G_over_omega_F=mid.G_S/(2*std::numbers::pi*config.frequency_Hz);point.inverse_C2_F_minus2=1/(mid.C_F*mid.C_F);point.status=to_string(mid.status);point.conservation_relative=mid.current_conservation_relative;
            point.valid=std::all_of(cv.begin(),cv.end(),[](const auto& q){return q.status==SolverStatus::Converged && std::isfinite(q.C_F) && q.C_F>0 && std::isfinite(q.current_conservation_relative) && q.current_conservation_relative<=.01;});point.validity_status=point.valid?"valid":"cv_reference_failed";
            if(point.valid) {
                const double slope=(1/(cv[2].C_F*cv[2].C_F)-1/(cv[0].C_F*cv[0].C_F))/(2*h);
                row->cv_x_m=eps*area/mid.C_F;if(slope<0)row->N_CV_m3=-2/(pc.q_C*eps*area*area*slope);
            }
            result->points.push_back(std::move(point));result->dlcp.push_back(row);if(feed)feed->Dlcp(std::move(row));ctx.SetProgress(float(i+1),float(maxima.size()));
        }
    } else if (op=="cv" || op=="cf") {
        if(!draft)throw std::invalid_argument("No device document");const auto& p=draft->project;auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
        const double T=action?(action->temperature_K>0?action->temperature_K:p.temperature_K):request.at("temperature_K").get<double>(); Positive(T,"temperature_K");
        std::vector<double> volts, frequencies;
        if(action) {
            if(op=="cv") {volts=ActionSamples(*action,p);for(auto& v:volts)v=-v;frequencies={action->frequency_Hz};}
            else {volts={-action->voltage_V};frequencies=ActionSamples(*action,p);}
        } else if(op=="cv") {
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
        const auto mesh=prepareMeasurement(device,p,volts,std::vector<double>{T},p.mesh,pc);
        ctx.ThrowIfCancellationRequested();
        auto result=CreateRef<AcResult>();result->has=7u|(1u<<31);result->mesh_x_m=retainProfiles?Column::Intern(mesh.x_m):Column{};if(!retainProfiles)result->has&=~1u;result->kind=op;result->area_m2=device.external.area_m2;typedOut=result;
        output={{"voltage_convention","consumer: left minus right; Y = G + j omega C"}};
        const auto total=op=="cv"?volts.size():frequencies.size();
        auto finite=[](double x)->Json {return std::isfinite(x)?Json(x):Json(nullptr);};
        // One DC continuation (C-V) or one operating point (C-f); AC solves are independent, so run them in parallel.
        std::vector<IVPoint> dcs;
        ctx.SetStage("DC operating points");
        if(op=="cv") dcs=solve_iv(device,mesh,volts,T,p.solver,pc,execution);
        else dcs=solve_iv(device,mesh,std::vector<double>(volts.begin(),volts.begin()+1),T,p.solver,pc,execution);
        ctx.ThrowIfCancellationRequested();
        std::vector<ACPoint> acs(total);
        ctx.SetStage("AC solve ("+std::to_string(total)+" points)");
        if(op=="cf") acs=solve_ac_spectrum(device,mesh,dcs.front(),frequencies,1e-3,pc,execution); // core parallelises the frequencies
        else {
            const auto solve=[&](std::size_t i){acs[i]=solve_ac_spectrum(device,mesh,dcs[i],{frequencies[0]},1e-3,pc,execution).front();};
            if(parallelMember)for(std::size_t i=0;i<total;++i)solve(i);else ParallelFor(ctx,total,solve);
        }
        ctx.ThrowIfCancellationRequested();
        for(std::size_t i=0;i<total;++i) {
            const auto &dc=dcs[op=="cv"?i:0];const auto &ac=acs[i];
            AcPoint point;point.has=((1u<<13)-1)|(1u<<15);point.point_id=op+"-"+std::to_string(i);point.voltage_V=-ac.voltage_V;point.terminal_voltage_V=-dc.voltage_V;point.internal_voltage_V=-dc.state.applied_voltage_V;point.frequency_Hz=ac.frequency_Hz;point.temperature_K=T;
            point.capacitance_F=ac.C_F;point.conductance_S=ac.G_S;point.G_over_omega_F=ac.G_S/(2*std::numbers::pi*ac.frequency_Hz);point.inverse_C2_F_minus2=1/(ac.C_F*ac.C_F);point.status=to_string(ac.status);point.conservation_relative=ac.current_conservation_relative;
            point.valid=ac.status==SolverStatus::Converged && std::isfinite(point.conservation_relative) && point.conservation_relative<=.01 && std::isfinite(ac.C_F) && std::isfinite(ac.G_S);point.validity_status=ac.status!=SolverStatus::Converged?"solver_failed":point.valid?"valid":"current_unresolved";result->points.push_back(std::move(point));
            ctx.SetProgress(float(i+1),float(total));
        }
    } else if(op=="qe") {
        if(!draft)throw std::invalid_argument("No device document");const auto& p=draft->project;auto device=make_device_from_project(p);const auto pc=physical_constants(p.constants_mode);
        const auto samples=action?ActionSamples(*action,p):std::vector<double>{};
        const double first=action?*std::min_element(samples.begin(),samples.end()):request.value("start_nm",350.),last=action?*std::max_element(samples.begin(),samples.end()):request.value("stop_nm",1100.);
        const auto countValue=request.value("points",Json(31));if(!action && (!countValue.is_number_integer() || countValue.get<double>()<2 || countValue.get<double>()>1001)) throw std::invalid_argument("QE points must be an integer in 2..1001");
        const int count=action?int(samples.size()):countValue.get<int>();const double probe=action?(action->constantFlux?action->photon_flux_m2_s:action->probe_power_W_m2):request.value(request.value("probe_mode",std::string("constant_flux"))=="constant_flux"?"probe_photon_flux_m2_s":"probe_power_W_m2",request.value("probe_mode",std::string("constant_flux"))=="constant_flux"?1e16:1e-3);
        const auto mode=action?std::string(action->constantFlux?"constant_flux":"constant_power"):request.value("probe_mode",std::string("constant_flux"));
        if(!std::isfinite(first) || !std::isfinite(last) || first<=0 || last<first || count<1 || count>1001) throw std::invalid_argument("Invalid QE wavelength range (2..1001 points)");
        Positive(probe,"probe_value");if(mode!="constant_flux" && mode!="constant_power") throw std::invalid_argument("Unknown QE probe_mode");
        const auto mesh=prepareMeasurement(device,p,std::vector<double>{0.},std::vector<double>{p.temperature_K},p.mesh,pc);
        auto dark=device;dark.illumination.enabled=false;
        auto result=CreateRef<AcResult>();result->has=3u|(1u<<31);result->mesh_x_m=retainProfiles?Column::Intern(mesh.x_m):Column{};if(!retainProfiles)result->has&=~1u;result->kind="qe";typedOut=result;
        output={{"request",request},{"probe_mode",mode},{mode=="constant_flux"?"probe_photon_flux_m2_s":"probe_power_W_m2",probe},{"temperature_K",p.temperature_K},{"voltage_V",0.},{"background","dark"}};
        OpticalTable response;
        std::vector<double> wavelengths=samples;if(!action)for(int i=0;i<count;++i) wavelengths.push_back(first+(last-first)*i/(count-1));
        ctx.SetStage("QE sweep ("+std::to_string(count)+" wavelengths)");
        const auto qes=solve_qe(dark,mesh,wavelengths,mode,probe,0.,p.temperature_K,p.solver,pc,execution);ctx.ThrowIfCancellationRequested();
        for(int i=0;i<count;++i) {
            const double wavelength=wavelengths[i];const auto &q=qes[i];
            AcPoint point;point.has=((1u<<6)-1)|(1u<<7)|(1u<<13)|(1u<<14)|(1u<<15)|(1u<<16)|(1u<<17);point.point_id="qe-"+std::to_string(i);point.voltage_V=0;point.terminal_voltage_V=0;point.internal_voltage_V=-q.internal_voltage_V;point.temperature_K=p.temperature_K;point.wavelength_nm=q.wavelength_nm;point.EQE=q.EQE;point.incident_photon_flux_m2_s=q.incident_photon_flux_m2_s;point.probe_power_W_m2=q.probe_power_W_m2;point.status=to_string(q.status);
            point.valid=q.status==SolverStatus::Converged && std::isfinite(q.EQE);point.validity_status=q.status!=SolverStatus::Converged?"solver_failed":point.valid?"valid":"current_unresolved";result->points.push_back(std::move(point));
            response.emplace_back(wavelength,q.EQE);ctx.SetProgress(float(i+1),float(count));
        }
        bool covered=std::adjacent_find(wavelengths.begin(),wavelengths.end(),std::greater_equal<double>{})==wavelengths.end() && wavelengths.size()>1;double integrated=0;
        auto add=[&](double wavelength,double power) {
            if(wavelength<device.illumination.wavelength_min_nm || wavelength>device.illumination.wavelength_max_nm) return;
            if(wavelength<first || wavelength>last) {if(power>0) covered=false;return;}
            integrated+=pc.q_C*power*wavelength*(pc.q_C==1.602e-19?5.035e15:1e-9/(6.62607015e-34*299792458.))*optical_interpolate(response,wavelength,false);
        };
        const auto &light=device.illumination;
        if(light.enabled && light.mode=="spectrum") for(const auto &[w,power]:light.spectrum_nm_W_m2) add(w,power*light.intensity_multiplier);
        else if(light.enabled && light.mode=="monochromatic") add(light.wavelength_nm,light.monochromatic_power_W_m2*light.intensity_multiplier);
        else covered=false;
        const auto sc=solve_iv(device,mesh,{0.},p.temperature_K,p.solver,pc,execution).front();
        output["integrated_Jsc_A_m2"]=covered && std::isfinite(integrated)?Json(integrated):Json(nullptr);
        output["light_Jsc_A_m2"]=sc.status==SolverStatus::Converged?Json(-sc.J_total_Am2):Json(nullptr);
        output["integration_note"]="Dark differential EQE weighted by incident spectrum bins; full spectral coverage required. Finite-light nonlinear response can differ.";
    } else if (op=="run" || op=="band") {
        if(!draft)throw std::invalid_argument("No device document");const auto& p=draft->project;auto device=make_device_from_project(p); const auto pc=physical_constants(p.constants_mode);
        auto volts=action?ActionSamples(*action,p):op=="band"?std::vector<double>{request.at("voltage_V").get<double>()}:make_voltage_list(p.voltage);
        for(auto &v:volts) {if(!std::isfinite(v)) throw std::invalid_argument("Invalid voltage_V");v=-v;}
        const bool preview=op=="run" && request.value("preview",false);
        auto meshConfig=p.mesh;
        if(preview) {
            // Owner 2026-11-23: same mesh as the exact run (no visible jump on release), every stride-th bias (4 when a full run is slow).
            volts=PreviewSamples(std::move(volts),request.value("stride",1));
        }
        phase.lap("model_device");ctx.SetStage("Preparing mesh");
        const auto mesh=prepareMeasurement(device,p,volts,std::vector<double>{p.temperature_K},meshConfig,pc);phase.lap("mesh");
        // Only previews use a previous model's state as a Newton guess; exact runs keep the qualified engine sweep.
        const bool liveSolve=preview || (automatic && op=="band" && request.value("preview",false));
        auto solver=p.solver; if(preview)solver.full_diagnostics=false;
        auto guess=liveSolve && warmInput && warmInput->x_m==mesh.x_m && device.external.series_ohm_m2==0?warmInput:Ref<const scaps_core::DeviceState>{};
        std::unique_ptr<IVSession> cold;
        const auto nextPoint=[&](double voltage) {
            IVPoint pt;
            if(guess) {
                auto solved=solve_bias(device,mesh,*guess,voltage,p.temperature_K,solver,pc);
                pt.voltage_V=voltage;pt.temperature_K=p.temperature_K;pt.status=solved.status;pt.iterations=solved.iterations;pt.final_residual_norm=solved.final_residual_norm;pt.state=std::move(solved.state);
                const auto& currents=pt.state.Jtotal_Am2;
                if(!currents.empty()) {for(double j:currents)pt.J_total_Am2+=j;pt.J_total_Am2/=currents.size();for(double j:currents)pt.J_conservation_error_Am2=std::max(pt.J_conservation_error_Am2,std::abs(j-pt.J_total_Am2));}
                if(!std::isinf(device.external.shunt_ohm_m2)){pt.J_shunt_Am2=-voltage/device.external.shunt_ohm_m2;pt.J_total_Am2+=pt.J_shunt_Am2;}
            }
            if(!guess || pt.status!=SolverStatus::Converged) {
                if(!cold)cold=std::make_unique<IVSession>(device,mesh,p.temperature_K,solver,pc);
                pt=cold->next(voltage);
            }
            if((liveSolve || (automatic && op=="band") || (retainBiasProfiles && !warmOutput)) && pt.status==SolverStatus::Converged && device.external.series_ohm_m2==0) {
                auto seed=CreateRef<const scaps_core::DeviceState>(pt.state);if(liveSolve)guess=seed;
                if(!warmOutput)warmOutput=std::move(seed); // first bias seeds the next parameter tick; subsequent biases continue locally
            }
            return pt;
        };
        if(preview) {
            auto result=CreateRef<IvResult>();result->has=1u<<31;typedOut=result;
            // One continuation sweep point by point (IVSession == solve_iv bit for bit): progressive feed and a cancellation
            // check after every point. The former 7-point chunks restarted from equilibrium each time and made previews slow.
            const bool external=p.illumination.enabled || p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);
            for(std::size_t i=0;i<volts.size();++i) {
                ctx.ThrowIfCancellationRequested();
                IVPoint pt;
                try {pt=nextPoint(volts[i]);}
                catch(const ExecutionCancelled&) {throw JobCancelledException();}
                const auto& st=pt.state;
                IvPoint point;point.has=(1u<<3)|(1u<<4);point.voltage_V=-volts[i];point.J_A_m2=pt.status!=SolverStatus::Converged?NAN:external?pt.J_total_Am2:st.Jn_Am2.front()+st.Jp_Am2.front();
                if(feed)feed->Point(point.voltage_V,point.J_A_m2);result->points.push_back(std::move(point));
                ctx.SetProgress(float(i+1),float(volts.size()));
            }
            output={{"preview",true},{"valid",true},{"mesh_node_count",mesh.x_m.size()}};outputValid=true;result->meta.prim=output;return;
        }
        ctx.ThrowIfCancellationRequested(); ctx.SetStage("Equilibrium");
        const bool live=feed && op=="run" && !preview,externalNetwork=p.illumination.enabled || p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);std::vector<double> pvV,pvJ;
        auto result=CreateRef<IvResult>();result->has=1u|(1u<<31);result->mesh_x_m=retainProfiles?Column::Intern(mesh.x_m):Column{};if(!retainProfiles)result->has&=~1u;typedOut=result;
        phase.lap("equilibrium");output={{"contact_phi_eV",{device.contact_left.phi_m_eV,device.contact_right.phi_m_eV}},{"voltage_convention","consumer: left minus right; J: scaps_core signed terminal current"}};

        for(std::size_t i=0;i<volts.size();++i) {
            ctx.ThrowIfCancellationRequested(); ctx.SetStage("I-V point "+std::to_string(i+1)+" / "+std::to_string(volts.size()));
            IVPoint pt=nextPoint(volts[i]);
            const auto &s=pt.state;
            if(live) {const double tj=pt.status!=SolverStatus::Converged ? NAN : externalNetwork ? pt.J_total_Am2 : s.Jn_Am2.front()+s.Jp_Am2.front();feed->Point(-volts[i],tj);pvV.push_back(-volts[i]);pvJ.push_back(tj);} // partial point for the UI: no Json
            phase.lap("solve_points");
            const bool external=p.illumination.enabled || p.external.series_ohm_m2!=0 || !std::isinf(p.external.shunt_ohm_m2);
            const double terminalJ=pt.status!=SolverStatus::Converged ? NAN : external ? pt.J_total_Am2 : s.Jn_Am2.front()+s.Jp_Am2.front();
            IvPoint point;point.has=(1u<<18)-1;point.bias_id="bias-"+std::to_string(i);point.voltage_V=-volts[i];point.J_A_m2=terminalJ;
            point.conservation_error_A_m2=pt.J_conservation_error_Am2;point.status=to_string(pt.status);point.iterations=pt.iterations;point.residual=pt.final_residual_norm;
            point.internal_voltage_V=-s.applied_voltage_V;point.terminal_voltage_V=external?-pt.voltage_V:-volts[i];
            point.valid=pt.status==SolverStatus::Converged && std::isfinite(terminalJ) && std::isfinite(pt.J_conservation_error_Am2) && pt.J_conservation_error_Am2<=std::max(1e-10,std::abs(terminalJ)*.01);
            point.validity_status=pt.status!=SolverStatus::Converged?"solver_failed":point.valid?"valid":"current_unresolved";
            if(retainProfiles && (retainBiasProfiles || jsonResponse || op=="band" || int(i)==std::clamp(detailBias,0,int(volts.size())-1))) {
                auto curves=DefectCurves(*draft,p,s);phase.lap("defect_curves");
                auto gr=GenerationRecombination(*draft,p,device,s);phase.lap("gen_recomb");
                const double net=gr.net_generation_current_A_m2;
                // Native J = G-R - right electron removal - left hole removal + shunt.
                double contactLoss=0;for(const auto &c:s.contact_recombination) contactLoss+=c.id=="right"?c.electron_current_A_m2:c.hole_current_A_m2;
                const double predicted=net-contactLoss+pt.J_shunt_Am2;
                const double budgetError=std::abs(predicted-terminalJ)/std::max({1e-30,std::abs(net),std::abs(contactLoss),std::abs(pt.J_shunt_Am2),std::abs(terminalJ)});
                gr.contact_loss_current_A_m2=contactLoss;gr.shunt_current_A_m2=pt.J_shunt_Am2;gr.series_current_A_m2=pt.series_current_density_A_m2();
                gr.predicted_terminal_current_A_m2=predicted;gr.terminal_budget_relative_error=budgetError;gr.terminal_budget_valid=std::isfinite(budgetError) && budgetError<=1e-6;
                gr.valid=point.valid;
                for(auto& channel:gr.channels) {
                    bool finite=std::isfinite(channel.integrated_current_A_m2);
                    for(const auto* values:{&channel.R_m3_s,&channel.R_sheet_m2_s})for(double value:values->get())finite &=std::isfinite(value);
                    channel.valid=point.valid && finite && channel.layer_sum_relative_error<=1e-6;channel.validity_status=channel.valid?"valid":!point.valid?point.validity_status:"channel_budget_unresolved";
                }
                auto details=CreateRef<IvDetails>();details->generation_recombination=std::move(gr);details->defect_previews=std::move(curves);
                details->trap_occupation=std::move(pt.state.trap_occupation);details->interface_occupation=std::move(pt.state.interface_occupation);point.details=std::move(details);
                point.x_m=s.x_m.empty()?result->mesh_x_m:Column::Intern(std::move(pt.state.x_m));
                point.Ec_eV=Column::Intern(std::move(pt.state.Ec_eV));point.Ev_eV=Column::Intern(std::move(pt.state.Ev_eV));point.EFn_eV=Column::Intern(std::move(pt.state.EFn_eV));point.EFp_eV=Column::Intern(std::move(pt.state.EFp_eV));
                point.n_m3=Column::Intern(std::move(pt.state.n_m3));point.p_m3=Column::Intern(std::move(pt.state.p_m3));
            }
            result->points.push_back(std::move(point));
            phase.lap("json_points");ctx.SetProgress(float(i+1),float(volts.size()));
        }
        if(live && p.illumination.enabled) feed->Estimate(EstimatePv(pvV,pvJ,incident_power_W_m2(device))); // marked "estimate" in the UI until the exact refinement below replaces it
        if(!deferMetadata) {
        const auto generation=generation_integrals_m2s(device,mesh,pc);std::vector<double> density;
        for(std::size_t i=0;i<generation.size();++i) {
            const double width=(i?std::max(0.,mesh.x_m[i]-mesh.x_m[i-1])*.5:0)+(i+1<mesh.x_m.size()?std::max(0.,mesh.x_m[i+1]-mesh.x_m[i])*.5:0);
            density.push_back(width>0?generation[i]/width:0.);
        }
        result->optics=OpticsResult{result->mesh_x_m,Column::Intern(std::move(density)),incident_power_W_m2(device)};
        for(std::size_t i=0;i<device.layers.size();++i) {
            auto layer=device.layers[i];std::vector<double> wavelengths,alpha,baseline;
            for(int n=0;n<=200;++n) {const double wavelength=300.+n*6.;wavelengths.push_back(wavelength);alpha.push_back(absorption_m1(layer,wavelength,pc));auto plain=layer;plain.absorption.urbach_enabled=false;baseline.push_back(absorption_m1(plain,wavelength,pc));}
            result->optics->absorption.push_back({draft->layers[i].id,Column::Intern(std::move(wavelengths)),Column::Intern(std::move(alpha)),Column::Intern(std::move(baseline))});
        }
        }
        phase.lap("optics");
        if(op=="run" && p.illumination.enabled && refinePv) {
            ctx.ThrowIfCancellationRequested();ctx.SetStage("Refining photovoltaic metrics");
            PhotovoltaicResult pv;const double power=measurement?measurement->incident_power_W_m2.value_or(incident_power_W_m2(device)):incident_power_W_m2(device);
            const double limit=measurement?measurement->photovoltaic_voltage_limit_V:p.voltage.stop_V;
            if(power>0) pv=solve_photovoltaic(device,mesh,-limit,p.temperature_K,power,p.solver,pc);
            else pv.message="Photovoltaic metrics require a positive, known incident power [W/m^2]";
            result->photovoltaic=std::move(pv);
        }
        phase.lap("photovoltaic");
    } else if(op!="inspect") throw std::invalid_argument("Unknown operation");
    if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts") {
        if(!deferMetadata) {
        document=DraftDocument(*draft); // legacy history/provenance adapter; solver and panels consume the typed snapshot
        output["project_snapshot"]=document;
        output["provenance"]={{"device_hash",DocumentHash(document)},{"solver_version",SCAPSLAB_SOLVER_VERSION},
            {"time_unix_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}};
        }
        bool valid=true;
        if(const auto iv=std::dynamic_pointer_cast<const IvResult>(typedOut))for(const auto& point:iv->points)valid &=point.valid;
        if(const auto ac=std::dynamic_pointer_cast<const AcResult>(typedOut)) {
            for(const auto& point:ac->points)valid &=point.valid;
            for(const auto& point:ac->dlcp)valid &=point->value.status==DlcpStatus::Converged;
        }
        output["valid"]=valid;output["validity_status"]=valid?"valid":"contains_invalid_points";
        output["compute_time_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        output["mesh_node_count"]=std::dynamic_pointer_cast<const IvResult>(typedOut)?std::static_pointer_cast<const IvResult>(typedOut)->mesh_x_m.size():std::static_pointer_cast<const AcResult>(typedOut)->mesh_x_m.size();phase.lap("validate_snapshot");if(phase.on)output["perf_phase_ms"]=phase.log;
    }
    // typed result (S4): built here, on the job thread, so the UI thread only swaps pointers
    const bool runOp=op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts";
    if(runOp && typedOut) {
        std::const_pointer_cast<ResultBase>(typedOut)->liveOnly=automatic;std::const_pointer_cast<ResultBase>(typedOut)->snapshot=deferMetadata?Ref<const ProjectDraft>{}:draft;
        outputValid=output.value("valid",false);output.erase("points");
        if(const auto iv=std::dynamic_pointer_cast<const IvResult>(typedOut))std::const_pointer_cast<IvResult>(iv)->meta.prim=output;else std::const_pointer_cast<AcResult>(std::static_pointer_cast<const AcResult>(typedOut))->meta.prim=output;
    }
    if(op!="save" && op!="project_save" && !op.starts_with("export_")) ctx.ThrowIfCancellationRequested();
} catch(const scaps_core::ExecutionCancelled&) {throw JobCancelledException();}
std::size_t Workflow::StoredSimulationCount() const {
    std::size_t count=0;
    for(const auto& d:project.devices){std::set<std::string> groups;for(const auto& r:d->history){const auto group=r.value("group_id",std::string{});groups.insert(group.empty()?r.at("run_id").get<std::string>():group);}count+=groups.size();}
    return count;
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
        if(!(op=="history_select" && request.at("run_id")=="") && std::none_of(h.begin(),h.end(),[&](const Json &r){return r.at("run_id")==request.at("run_id");})) throw std::invalid_argument("Unknown run_id");
        if(request.contains("colour")) {
            const auto &c=request.at("colour");if(!c.is_array() || c.size()!=4) throw std::invalid_argument("Invalid history colour");
            for(const auto &v:c) if(!v.is_number() || !std::isfinite(v.get<double>()) || v<0 || v>1) throw std::invalid_argument("Invalid history colour");
        }
        for(const auto *key:{"pinned","visible"}) if(request.contains(key) && !request.at(key).is_boolean()) throw std::invalid_argument(std::string(key)+" must be boolean");
        if(request.contains("name") && !request.at("name").is_string()) throw std::invalid_argument("name must be a string");
        if(op=="history_select") {
            for(const auto* key:{"member_index","attempt"})if(request.contains(key) && (!request.at(key).is_number_integer() || request.at(key).get<int>()<0))throw std::invalid_argument("Invalid Results selection index");
            const int member=request.value("member_index",0),attempt=request.value("attempt",0);
            const auto source=targets.front()->RunResult(request.at("run_id"));
            if(auto batch=std::dynamic_pointer_cast<const BatchResult>(source)) {if(member>=int(batch->rows.size()) || (attempt && (batch->runPlan?!batch->Member(member,attempt):attempt!=batch->rows[member]->attempt)))throw std::invalid_argument("Unknown member/attempt");}
            else if(member || attempt>1)throw std::invalid_argument("Unknown member/attempt");
            if(request.contains("action")){bool found=false;for(int i=0;i<kActionCount;++i)found|=request.at("action")==ActionId(ActionKind(i));if(!found)throw std::invalid_argument("Unknown Action");}
        }
    }
    std::vector<Undo::DeviceBefore> before; // refs only (Cow history, typed results): the undo step keeps just the delta computed below
    for(auto *dp:targets) before.emplace_back(*dp);
    for(auto *dp:targets) {
        auto &d=*dp;
        if(op=="history_select") {d.selectedRun=request.at("run_id").get<std::string>();d.pinStoredView=false;if(auto batch=std::dynamic_pointer_cast<const BatchResult>(d.RunResult(d.selectedRun)))d.batchResult.adopt(batch);d.selectedMember=request.value("member_index",0);d.selectedAttempt=request.value("attempt",0);if(request.contains("action"))project.ui["results_action"]=request.at("action");LimitProfiles(d);continue;}
        auto &history=d.history.mut();
        const auto noSelection=[&]{if(std::none_of(history.begin(),history.end(),[&](const Json &r){return r.at("run_id")==d.selectedRun;}))d.selectedRun.clear();};
        if(op=="history_clear") {
            const bool pinned=request.value("include_pinned",false),byId=request.contains("run_ids"); // run_ids: delete exactly these runs
            const auto ids=request.value("run_ids",Json::array());
            history.erase(std::remove_if(history.begin(),history.end(),[&](const Json &r){return byId?std::find(ids.begin(),ids.end(),r.at("run_id"))!=ids.end():pinned || !r.at("pinned").get<bool>();}),history.end());
            if(byId)noSelection();else d.selectedRun=history.empty()?"":history.back().at("run_id").get<std::string>();
            const auto kept=[&](const ResultBase *result,const std::string &kind) {
                return result && std::any_of(history.begin(),history.end(),[&](const Json &run){return run.at("kind")==kind && run.at("provenance")==result->Provenance();});
            };
            d.ClearChange();d.storedBand.reset();d.detailRun.clear();
            if(scope=="all" && pinned && !byId){d.previewV.clear();d.previewJ.clear();d.liveV.clear();d.liveJ.clear();d.warmIv.reset();d.warmBand.reset();}
            if(!kept(d.result.typed().get(),"iv"))d.result.clear();
            if(!kept(d.bandResult.typed().get(),"band"))d.bandResult.clear();
            for(const auto &[kind,r]:std::map(d.acResults.all())) if(!kept(r.get(),kind))d.acResults.erase(kind);
            d.selectedMember=0;d.selectedAttempt=0;d.batchResult.clear();if(auto batch=std::dynamic_pointer_cast<const BatchResult>(d.RunResult(d.selectedRun)))d.batchResult.adopt(batch);d.status=history.empty()?"no results":"previous";
        } else if(request.contains("unpin_kind")) {
            const auto kind=request.at("unpin_kind").get<std::string>();
            for(auto &r:history) if(r.at("pinned")==true && (r.at("kind")==kind || (kind=="iv" && r.at("kind")=="band"))) {r["pinned"]=false;if(r.at("name").get<std::string>().starts_with("#"))r["name"]="";}
            TrimHistory(history,retention);noSelection();
        } else {
            auto run=std::find_if(history.begin(),history.end(),[&](const Json &r){return r.at("run_id")==request.at("run_id");});
            {
                for(const auto *key:{"pinned","name","visible","colour"}) if(request.contains(key)) (*run)[key]=request.at(key);
                if(request.contains("pinned") && run->at("pinned")==true) {
                    const auto result=d.RunResult(run->at("run_id"));const auto kind=run->at("kind").get<std::string>();
                    if(kind=="iv" || kind=="band")d.changeBias=d.selectedBias;
                    if(kind=="iv")d.changeIv=std::dynamic_pointer_cast<const IvResult>(result);
                    else if(kind=="band")d.changeBand=std::dynamic_pointer_cast<const IvResult>(result);
                    else if(auto ac=std::dynamic_pointer_cast<const AcResult>(result))d.changeAc[kind]=std::move(ac);
                }
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
        d.SyncRuns();LimitProfiles(d);
    }
    {
        auto deltas=Undo::Compare(before,*this,op=="history_clear");before.clear(); // run deltas by run_id; the shared "before" history is released here
        if(op!="history_select" && !deltas.Empty() && projectUndo.PushExecuted(Undo::History(*this,op=="history_clear"?"Clear history":"Update run",std::move(deltas)))) preferProjectUndo=true; // selecting a run is navigation, not an undoable edit (as before S9)
    }
    if(op=="history_clear")std::erase_if(resultCompare,[&](const auto& key){const auto& [device,run,member,action,attempt]=key;return HasDevice(device) && !Device(device).RunResult(run);});
    projectDirty=true;RefreshDirty();status="Completed: "+op;
    for(auto *dp:targets) {Emit(HistoryChanged(dp->id));Emit(SelectionChanged(dp->id));}
    response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();
}
void Workflow::DeviceOp(const Json &request,Ref<const ProjectDraft> importedDraft) {
    const auto op=request.at("operation").get<std::string>();
    const std::string id=request.value("device_id",project.active_device);
    auto &devices=project.devices;
    const auto at=std::find_if(devices.begin(),devices.end(),[&](const auto &d){return d->id==id;});
    const auto known=[&]{if(at==devices.end()) throw std::invalid_argument("Unknown device_id");};
    const auto checkName=[&](const char *what) {if(request.contains("name") && (!request.at("name").is_string() || request.at("name").get<std::string>().empty())) throw std::invalid_argument(std::string(what)+" name is empty");};
    Unique<ICommand> record; // the undo step of this operation (a small delta), pushed once the operation succeeded
    const std::string activeBefore=project.active_device;
    if(op=="device_add" || op=="device_duplicate") {
        checkName("Device");
        auto d=CreateRef<DeviceState>();
        if(op=="device_duplicate") {
            known();const auto &src=**at;
            d->name=src.name+" copy";d->description=src.description;d->fileExtensions=src.fileExtensions;d->SetDraft(src.draft);d->actions=src.actions;d->runPlan=src.runPlan;d->runPlanEdited=src.runPlanEdited;d->SetBatch(src.batch);d->SetRecorder(src.recorder);
            d->batchResult.adopt(src.batchResult.share());d->selectedLayer=src.selectedLayer;d->selectedInterface=src.selectedInterface;d->selectedBias=src.selectedBias;d->selectedMember=src.selectedMember;d->biasVoltage_V=src.biasVoltage_V;
        } else {
            d->runPlan=Current().runPlan;d->runPlanEdited=Current().runPlanEdited;
            if(importedDraft)d->SetDraft(std::move(importedDraft));
            else {const auto doc=request.contains("document")?SelfContained(request.at("document")):NewDeviceDocument();d->SetDocument(NormalizeDocument(doc));}
            d->name=d->draft->project.case_id;d->description=request.value("description",std::string{});
        }
        if(request.contains("name")) d->name=request.at("name").get<std::string>();
        static std::atomic_uint64_t sequence{0};
        d->id="device-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
        static const double colours[][4]={{.25,.65,.95,1},{.95,.55,.25,1},{.4,.8,.45,1},{.8,.45,.85,1},{.9,.8,.3,1}};
        d->colour=Json::array({colours[devices.size()%5][0],colours[devices.size()%5][1],colours[devices.size()%5][2],colours[devices.size()%5][3]});d->metadataDirty=true;
        devices.push_back(d);project.active_device=d->id;project.compare_set.insert(d->id);
        d->needsRecalculate=d->needsBand=true;d->changedAt=std::chrono::steady_clock::now();
        record=Undo::DeviceAdded(*this,d,devices.size()-1,activeBefore,op=="device_duplicate"?"Duplicate device":"Add device");
    } else if(op=="action_settings") {
        known();if(!request.at("actions").is_object()) throw std::invalid_argument("Invalid action list");
        ValidateActions(request.at("actions"));(*at)->actions=request.at("actions");(*at)->runPlan=CreateRef<const RunPlan>(ReadRunPlan((*at)->actions));(*at)->runPlanEdited=false;
    } else if(op=="project_edit") {
        checkName("Project");
        if(request.contains("ui")) (void)ValidateUi(request.at("ui"));
        const std::string nameBefore=project.name;const Json uiBefore=project.ui;
        std::vector<Undo::DeviceBefore> trimBefore; // devices whose history a lower history_retention shortens
        if(request.contains("name")) project.name=request.at("name").get<std::string>();
        if(request.contains("ui")) {
            project.ui=request.at("ui");const int retention=project.ui.value("history_retention",5);
            for(auto &d:devices) if(d->history.size()>0) {
                Json h=*d->history;TrimHistory(h,retention);
                if(h.size()==d->history.size()) continue;
                trimBefore.emplace_back(*d);
                d->history=std::move(h);d->SyncRuns();
                if(std::none_of(d->history.begin(),d->history.end(),[&](const Json &r){return r.at("run_id")==d->selectedRun;}))d->selectedRun.clear();
            }
        }
        record=Undo::ProjectEdit(*this,nameBefore,project.name,uiBefore,project.ui,Undo::Compare(trimBefore,*this,false));
    } else if(op=="device_remove") {
        known();if(devices.size()==1) throw std::invalid_argument("Cannot delete the last device");
        const auto removed=*at;const std::size_t index=at-devices.begin();const bool compared=project.compare_set.contains(id);
        devices.erase(devices.begin()+index);project.compare_set.erase(id);
        if(project.active_device==id) project.active_device=devices.front()->id;
        record=Undo::DeviceRemoved(*this,removed,index,activeBefore,project.active_device,compared);
    } else if(op=="device_update") {
        known();auto &d=**at;checkName("Device");
        const Undo::DeviceMeta metaBefore{d.name,d.description,d.colour,project.compare_set.contains(id)};
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
        d.metadataDirty=true;
        record=Undo::DeviceUpdated(*this,id,metaBefore,Undo::DeviceMeta{d.name,d.description,d.colour,project.compare_set.contains(id)});
    } else if(op=="device_move") {
        known();const int index=request.at("index");if(index<0 || index>=int(devices.size())) throw std::invalid_argument("Invalid device index");
        const auto d=*at;const std::size_t from=at-devices.begin();devices.erase(devices.begin()+from);devices.insert(devices.begin()+index,d);
        record=Undo::DeviceMoved(*this,id,from,std::size_t(index));
    } else if(op!="device_list") throw std::invalid_argument("Unknown device operation");
    if(record && projectUndo.PushExecuted(std::move(record))) preferProjectUndo=true;
    if(op!="device_list") {projectDirty=true;Emit(DeviceListChanged());}
    RefreshDirty();status="Completed: "+op;
    response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();
}

Workflow::Workflow(Ref<JobSystem> j,Ref<CommandRegistry> registry,Ref<EventBus> eventBus,Ref<Notifier> note):Layer("ScapsLab workflow"),jobs(std::move(j)),commands(registry?std::move(registry):CreateRef<CommandRegistry>()),bus(eventBus?std::move(eventBus):CreateRef<EventBus>()),notifier(note?std::move(note):CreateRef<Notifier>(bus)) {
    if(UserBoundsFile().empty()) {
        if(const auto executable=Platform::CurrentExecutablePath()) {
            const auto dir=executable.Value().Native().parent_path();
            const auto user=(ConfigManager::ResolvePortablePaths(Path{dir/"install/app/config"}).userConfigDirectory/Path{"scapslab/parameter_bounds.yaml"}).Utf8();
            const auto diagnostic=InitializeParameterBounds(Path{dir/"parameter_bounds.yaml"}.Utf8(),user);
            if(!diagnostic.empty())notifier->Warning("Parameter bounds",diagnostic,NotificationCategory::Project);
        }
    }
    ++constructed;
    projectUndo.SetLimits(kUndoMaxDepth,kUndoMaxBytes);
    {static std::atomic<std::uint64_t> epoch{0};plotGen=++epoch<<32;} // high bits identify this Workflow so a plot cache never serves another instance's data
    auto initial=CreateRef<DeviceState>();initial->id="device-1";initial->name="Device";project.devices.push_back(initial);project.active_device=initial->id;project.compare_set.insert(initial->id);
    if(!Cmd::RegisterAll(*commands,*this)) throw std::runtime_error("Cannot register ScapsLab command"); // typed commands (Commands/); ids scapslab.<operation>
}
namespace {
class IoJob final : public IJob {
    std::string name;std::function<std::string()> fn;Ref<std::string> error;
public:
    IoJob(std::string n,std::function<std::string()> f,Ref<std::string> e):name(std::move(n)),fn(std::move(f)),error(std::move(e)) {}
    std::string GetName() const override {return "ScapsLab IO: "+name;}
    std::string GetType() const override {return "scapslab.io";}
    void Execute(JobContext &) override {try{*error=fn();}catch(const std::exception &e){*error=e.what();}}
};
}
void Workflow::AsyncIo(std::string what,std::function<std::string()> fn,std::function<void()> done) {
    auto error=CreateRef<std::string>();
    const auto id=jobs->Submit(CreateRef<IoJob>(what,std::move(fn),error));
    ioTasks.push_back({id,std::move(what),std::move(error),std::move(done)});
}
void Workflow::PollIo() {
    for(std::size_t i=0;i<ioTasks.size();) {
        const auto live=jobs->TryGetStatus(ioTasks[i].id);
        if(live && (*live==JobStatus::Queued || *live==JobStatus::Running)) {++i;continue;}
        auto task=std::move(ioTasks[i]);ioTasks.erase(ioTasks.begin()+i);
        if(!task.error->empty()) notifier->Warning(task.what+" failed",*task.error,NotificationCategory::Project);
        if(task.done) task.done();
    }
}
void Workflow::TouchRecent(const std::string &file) {
    recent.erase(std::remove(recent.begin(),recent.end(),Json(file)),recent.end());recent.insert(recent.begin(),file);if(recent.size()>10) recent.erase(recent.end()-1);
}
void Workflow::SetRecentPath(const std::string &file) {
    recentPath=file;if(file.empty()) return;
    const auto directory=std::filesystem::path(file).parent_path();
    recoveryPath=(directory/"scapslab-recovery.scapslab").string();
    recentLoaded=false;auto loaded=CreateRef<Json>(Json::array());
    AsyncIo("Read recent files",[file,loaded]()->std::string {
        std::string text,error;if(!Io::Load(file,text,error)) return {}; // a missing file is the first start
        try {auto items=Json::parse(text);if(items.is_array() && items.size()<=10 && std::all_of(items.begin(),items.end(),[](const auto &p){return p.is_string();})) *loaded=std::move(items);}catch(...) {}
        return {};
    },[this,loaded]{ // entries added meanwhile stay in front
        for(const auto &item:*loaded) if(recent.size()<10 && std::find(recent.begin(),recent.end(),item)==recent.end()) recent.push_back(item);
        savedRecent=*loaded;recentLoaded=true;
    });
    const auto recovery=recoveryPath;
    auto found=CreateRef<bool>(false);
    AsyncIo("Look for autosave",[recovery,found]()->std::string {*found=Io::Exists(recovery);return {};},[this,found]{
        if(*found) {recoveryAvailable=true;status="Autosave found: File > Recover autosave";notifier->Info("Autosave found","File > Recover autosave",NotificationCategory::Project);}
    });
}
void Workflow::PersistRecent() {
    if(recentPath.empty() || !recentLoaded || recentWriting || recent==savedRecent) return;
    recentWriting=true;savedRecent=recent; // a failed write is reported once and retried only after the next change
    AsyncIo("Write recent files",[file=recentPath,text=recent.dump(2)]()->std::string {std::string error;return Io::Save(file,text,error)?std::string{}:error;},[this]{recentWriting=false;});
}
void Workflow::BoundsFileTyped(BoundsFileAction action) {
    if(action==BoundsFileAction::Inspect)return;
    if(boundsFileBusy)throw std::invalid_argument("Parameter bounds file operation is already running");
    boundsFileBusy=true;boundsFileDiagnostic.clear();auto diagnostic=CreateRef<std::string>();
    AsyncIo("Parameter bounds",[action,diagnostic]{*diagnostic=action==BoundsFileAction::Reset?ResetParameterBounds():ReloadParameterBounds();return *diagnostic;},[this,diagnostic]{boundsFileBusy=false;boundsFileDiagnostic=*diagnostic;status=diagnostic->empty()?"Parameter bounds reloaded":*diagnostic;});
}
void Workflow::SaveSettings() {
    if(settingsPath.empty()) return;
    if(settingsWriting) {settingsDirty=true;return;}
    settingsWriting=true;settingsDirty=false;
    AsyncIo("Write settings",[file=settingsPath,text=settings.dump(2)]()->std::string {std::string error;return Io::Save(file,text,error)?std::string{}:error;},[this]{settingsWriting=false;if(settingsDirty)SaveSettings();});
}
void Workflow::DropRecovery(const std::string &keep) {
    if(recoveryPath.empty()) return;
    if(keep!=recoveryPath) recoveryAvailable=false;
    AsyncIo("Remove autosave",[recovery=recoveryPath,keep]()->std::string {
        std::error_code ec;if(!keep.empty() && std::filesystem::equivalent(keep,recovery,ec)) return {};
        Io::Remove(recovery);return {};
    });
}
void Workflow::RegisterIo(IOLayer &io) {
    io.RegisterHandler<CoreEvents::ProjectSaveRequested>([this](const CoreEvents::ProjectSaveRequested &) {fileAction="save";}); // Core Ctrl+S / project.save: the UI layer picks the target (dialog when untitled)
    io.RegisterHandler<ProjectSaveRequested>([this](const ProjectSaveRequested &e) {ioResult=Dispatch({{"operation","project_save"},{"path",e.path},{"include_results",e.includeResults}});});
    io.RegisterHandler<ProjectOpenRequested>([this](const ProjectOpenRequested &e) {ioResult=Dispatch({{"operation","project_load"},{"path",e.path}});});
    io.RegisterHandler<ProjectExportRequested>([this](const ProjectExportRequested &e) {
        ioResult=Dispatch({{"operation","export_results"},{"path",e.path},{"format",e.format},{"all",e.all},{"run_ids",e.runIds}});
        if(!ioResult.value("ok",false)) status=ioResult.at("error").at("message");
    });
}
// Plot caches (Panel.cpp) follow these events. Subscribed from the UI thread by the first Panel that renders (not in the constructor:
// Workflows are also built on worker threads, and EventBus::Subscribe is not thread-safe).
void Workflow::WatchPlots() {
    if(!plotSubs.empty())return;
    const auto bump=[this](auto tag){using E=typename decltype(tag)::type;plotSubs.push_back(bus->Subscribe<E>([this](const E &){++plotGen;}));};
    bump(std::type_identity<RunCompleted>{});bump(std::type_identity<HistoryChanged>{});bump(std::type_identity<SelectionChanged>{});plotSubs.push_back(bus->Subscribe<DocumentEdited>([this](const DocumentEdited &){++docGen;}));bump(std::type_identity<DeviceListChanged>{});bump(std::type_identity<ProjectLoaded>{});
}
Workflow::~Workflow() {
    tutorial.Stop();autoRecalculate=autoRecalculateBand=false;saveLane.queue.clear();projectLane.queue.clear();
    for(auto &[id,l]:lanes) {l.queue.clear();if(l.active) (void)CancelLane(l);}
    while(AnyActive()) {OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    while(saveLane.active) {OnUpdate(0); std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    while(library.Busy()) {PollLibrary();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    while(!ioTasks.empty()) {PollIo(); std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
void Workflow::Submit(const std::string &device,Json request,std::string key) {
    auto &l=LaneOf(device);
    if(l.active || !l.queue.empty()) {l.Enqueue(std::move(request),std::move(key));return;} // serial per device: waits its turn, newest of a key wins
    StartOperation(request,device,{},{},{},{},{},{},key=="auto" || key=="auto-band");
}
void Workflow::CancelDevice(const std::string &device) {
    auto it=lanes.find(device);if(it==lanes.end()) return;
    std::erase_if(it->second.queue,[](const Queued &q){return q.request.value("batch",false);});
    if(it->second.active) (void)CancelLane(it->second);
}
const ProjectDraft *Workflow::EditDraft() const {
    auto it=lanes.find(project.active_device);if(it==lanes.end())return nullptr;
    const auto& lane=it->second;
    if(!lane.queue.empty() && lane.queue.back().draft)return lane.queue.back().draft.get();
    if(lane.pending && lane.Op()=="edit")return lane.pending->draft.get();
    return nullptr;
}
namespace {
Ref<const ProjectDraft> ReadDraftEdit(const ProjectDraft* source,const Json& request) {
    Ref<const ProjectDraft> result;
    if(request.contains("document"))result=DraftFromDocument(SelfContained(request.at("document")));
    else {if(!source)throw std::invalid_argument("No device document");result=PatchDraft(*source,request.at("patch"),request.value("pointer_set",Json::array()));}
    const auto& patch=request.contains("document")?request.at("document"):request.at("patch");
    if(patch.contains("illumination") && patch.at("illumination").value("scapslab_spectrum",std::string{})=="AM1.5G") {
        auto next=CreateRef<ProjectDraft>(*result);next->project.illumination.mode="spectrum";next->project.illumination.spectrum_file.clear();next->project.illumination.spectrum_nm_W_m2=AM15SpectrumValues();result=std::move(next);
    }
    return result;
}
}
bool Workflow::ReplacingProject() const {
    auto it=lanes.find(project.active_device);if(it==lanes.end() || !it->second.active) return false;
    const std::string op=it->second.Op();return op=="project_load" || op=="load" || op=="layer";
}
void Workflow::StartOperation(const Json &r,const std::string &id,Ref<const ProjectDraft> edit,Ref<const BatchDraft> batch,Ref<const RecorderDraft> recorder,Ref<const RunPlan> plan,std::string detailRun,Ref<const AcResult> dlcp,bool automatic) {
    const auto operation=r.at("operation").get<std::string>();
    if(operation=="run" || operation=="band" || operation=="cv" || operation=="cf" || operation=="qe" || operation=="dlcp" || operation=="single_shot" || operation=="run_batch" || operation=="quick_sweep")Library::ValidateLinks(*Device(id).draft,*library.state);
    auto &d=Device(id);auto &L=LaneOf(id);if(L.active) throw std::invalid_argument("A job is still active on this device");L.pending=CreateRef<Operation>(d.draft,r);auto &pending=L.pending;pending->jsonResponse=!lightResponse;const std::string op0=r.at("operation");if(op0=="edit"){pending->draft=edit?std::move(edit):ReadDraftEdit(d.draft.get(),r);pending->request.erase("document");pending->request.erase("patch");pending->request.erase("pointer_set");}if(op0=="export_dlcp")pending->dlcpInput=dlcp?std::move(dlcp):d.acResults.get("dlcp");else if(op0=="export_ac" && r.contains("kind") && r.at("kind").is_string())pending->acInput=d.acResults.get(r.at("kind").get<std::string>());if(op0.find("batch")!=std::string::npos || op0.starts_with("export_") || op0=="send_to_plotlab")pending->batchInput=d.batchResult.typed();pending->batch=batch?std::move(batch):r.contains("spec")?ReadBatchDraft(r.at("spec")):d.batch;pending->recorder=recorder?std::move(recorder):r.contains("columns")?ReadRecorderDraft(r.at("columns")):d.recorder;
    const std::string op=r.at("operation");
    if(!detailRun.empty()){pending->draft=std::move(edit);pending->detailRun=std::move(detailRun);}
    if(op=="retry_batch" || op=="record_batch")pending->request["source_run"]=d.selectedRun;
    if(op=="run_batch" || op=="quick_sweep" || op=="retry_batch") {
        if(plan)pending->runPlan=std::move(plan);
        else if(op=="retry_batch" && pending->batchInput && pending->batchInput->runPlan)pending->runPlan=pending->batchInput->runPlan;
        else if(r.contains("actions"))pending->runPlan=CreateRef<const RunPlan>(ReadRunPlan(r.at("actions")));
        else if(d.runPlanEdited || (lightResponse && op=="quick_sweep"))pending->runPlan=d.runPlan;
        if(pending->runPlan)ValidateRunPlan(*pending->runPlan,*pending->draft);
    }
    pending->revision=d.revision;pending->detailBias=d.selectedBias;pending->retainBiasProfiles=op=="run";
    if(op=="band")pending->warmInput=d.warmBand;else if(op=="run")pending->warmInput=d.warmIv;
    if(d.liveGesture && (op=="run" || op=="band"))pending->liveMeshInput=d.liveMesh;
    if(op=="record_batch" && !d.selectedRun.empty() && !std::dynamic_pointer_cast<const BatchResult>(d.RunResult(d.selectedRun))) {
        auto chosen=d.RunResult(d.selectedRun);
        if(chosen && chosen->snapshot && chosen->runPlan) {
            auto table=CreateRef<BatchResult>();table->native=true;table->runPlan=chosen->runPlan;table->projectSnapshot=chosen->snapshot;table->recorderDraft=d.recorder;
            auto config=CreateRef<BatchDraft>();config->axes[0].values=std::vector<double>{chosen->snapshot->project.temperature_K};table->batchDraft=config;table->parentRun=d.selectedRun;
            auto member=CreateRef<MemberActions>();member->illuminated=chosen->snapshot->project.illumination.enabled;std::string group;
            for(const auto& entry:d.history)if(entry.at("run_id")==d.selectedRun)group=entry.value("group_id",std::string{});
            for(const auto& entry:d.history)if(entry.at("run_id")==d.selectedRun || (!group.empty() && entry.value("group_id",std::string{})==group)) {
                for(int k=0;k<kActionCount;++k)if(entry.at("kind")==ActionId(ActionKind(k)))member->results.emplace_back(ActionKind(k),d.RunResult(entry.at("run_id")));
            }
            auto plan=CreateRef<RunPlan>(*chosen->runPlan);for(auto& a:plan->actions)a.include=std::any_of(member->results.begin(),member->results.end(),[&](const auto& r){return r.first==a.kind;});table->runPlan=std::move(plan);
            auto row=CreateRef<scaps_core::BatchRow>(ExtractActions(*member,*d.recorder));row->id="point_0";row->parameters["temperature_K"]=table->projectSnapshot->project.temperature_K;
            member->summary=row;table->rows.push_back(row);table->actionMembers.push_back(member);table->time_unix_ms=chosen->MetaOf().prim.at("provenance").at("time_unix_ms");table->solverVersion=chosen->MetaOf().prim.at("provenance").at("solver_version").get<std::string>();
            pending->batchInput=std::move(table);
        }
    }
    if(op=="load_profile")pending->request["project_path"]=path;
    if(op=="export_gr") {if(r.contains("source") && r.at("source")!="iv" && r.at("source")!="band") throw std::invalid_argument("source must be iv or band");pending->request["device_id"]=id;pending->ivInput=r.value("source",std::string("iv"))=="band"?d.bandResult.typed():d.result.typed();}
    if(op=="export_results") {
        auto runs=ResultRuns();
        try {
            const auto input=r.value("run_ids",Json::array());std::vector<std::string> ids;
            if(!input.is_array())throw std::invalid_argument("run_ids must be an array");
            for(const auto& value:input) {
                if(!value.is_string())throw std::invalid_argument("Unknown run_id");
                auto runId=value.get<std::string>();
                if(std::none_of(runs.begin(),runs.end(),[&](const auto& run){return run.runId==runId;}))throw std::invalid_argument("Unknown run_id");
                ids.push_back(std::move(runId));
            }
            if(!r.value("all",false))std::erase_if(runs,[&](const auto& run){return std::find(ids.begin(),ids.end(),run.runId)==ids.end();});
            pending->exportRuns=std::move(runs);
        }catch(...){pending->exportError=std::current_exception();} // retain asynchronous export error codes
    }

    const bool liveBand=op=="band" && r.value("live",false); // background band refresh: no "running" status, no history
    if(liveBand && pending->detailRun.empty()) {
        pending->action=d.runPlan->actions[0];pending->action->voltage_V=r.at("voltage_V").get<double>();
        if(pending->action->temperature_K>0){auto snapshot=CreateRef<ProjectDraft>(*d.draft);snapshot->project.temperature_K=pending->action->temperature_K;pending->draft=std::move(snapshot);}
    }
    pending->automatic=liveBand || automatic;
    if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts" || op=="run_batch" || op=="quick_sweep" || op=="retry_batch")ValidateDraft(*pending->draft);
    if((op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts") && !liveBand) d.status="running";
    const bool preview=op=="run" && r.value("preview",false);if(op=="run" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts" || op=="run_batch" || op=="quick_sweep" || op=="retry_batch") pending->feed=CreateRef<LiveFeed>();L.liveSeq=0;L.liveAt=std::chrono::steady_clock::time_point{};
    if(preview){pending->action=d.runPlan->actions[1];if(d.liveGesture)pending->action->range=SweepRange{SweepSampling::List,false,0,0,1,int(d.liveBiases.size()),d.liveBiases};}
    if(op=="run") L.runStarted=std::chrono::steady_clock::now();
    if(op=="run" && !preview) {d.needsRecalculate=false;d.needsBand=false;}if(op=="band" && pending->detailRun.empty())d.needsBand=false;
    L.active=jobs->Submit(pending);if(!r.value("autosave",false) && !liveBand){status=preview?(r.value("stride",1)>1?"Preview (coarse, every "+std::to_string(r.value("stride",1))+". point): "+d.name:"Preview: "+d.name):"Running: "+d.name;response=Json();}
}
void Workflow::StartSave(const Json &r) {
    const bool autosave=r.value("autosave",false);
    if(saveLane.active) { // one save at a time: remember the newest request (a manual one is not displaced by an autosave)
        if(saveLane.queue.empty() || !autosave || saveLane.queue.front().request.value("autosave",false)) {saveLane.queue.clear();saveLane.queue.push_back({r,"save"});}
        if(!autosave) status="Saving... (queued)";
        return;
    }
    auto op=CreateRef<Operation>(Current().draft,r);op->request["operation"]="project_save";
    op->assetState=library.state;op->assetBrowser=library.browser;op->assetDrafts=library.drafts;
    op->workspace=Workspace(false);
    for(std::size_t i=0;i<project.devices.size();++i) {
        const auto &d=*project.devices[i];op->workspace["devices"][i]["selected_run"]=d.selectedRun;
        op->shared.push_back({d.history.share(),d.Results()});
    }
    saveLane.pending=op;saveEpoch=epoch;savingAuto=autosave;saveLane.active=jobs->Submit(op);
    if(!autosave) { // what is saved is clean; later edits mark the project dirty again
        status="Saving...";projectDirty=false;
        for(auto &d:project.devices) {d->metadataDirty=false;d->undo.MarkClean();}
        RefreshDirty();
    }
}
void Workflow::UpdateSave() {
    if(saveLane.active) {
        const auto live=jobs->TryGetStatus(saveLane.active);
        if(live && (*live==JobStatus::Queued || *live==JobStatus::Running)) return;
        const auto snapshot=jobs->GetJob(saveLane.active);
        const auto op=std::move(saveLane.pending);const bool autosave=savingAuto,sameProject=saveEpoch==epoch;saveLane.active=0;saveLane.pending.reset();
        if(snapshot && snapshot->status==JobStatus::Completed) {
            if(!autosave) {
                const auto file=op->request.at("path").get<std::string>();
                if(sameProject) path=file;
                TouchRecent(file);DropRecovery(file);
                status="Saved "+op->output.at("path").get<std::string>()+" ("+std::to_string(op->output.at("size_bytes").get<std::size_t>())+" bytes)";
                if(!op->output.value("original_copy",std::string{}).empty())status+="; v4 requires this ScapsLab version. Original: "+op->output.at("original_copy").get<std::string>();
                Emit(ProjectSaved(file,false));
                if(!AnyActive()) {response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();if(!lightResponse)response["operation_result"]=op->output;}
            } else {autosaveFailing=false;recoveryAvailable=true;Emit(ProjectSaved(op->request.value("path",std::string{}),true));}
        } else if(!autosave) {
            if(sameProject) projectDirty=true;
            status=snapshot?snapshot->errorMessage:std::string("Save failed");
            if(!AnyActive()) response=Failure("operation_failed",status);
            Emit(ProjectSaveFailed(op->request.value("path",std::string{}),status,false));notifier->Error("Save failed",status,NotificationCategory::Project);
        } else { // autosave: one warning per failing streak (the next try is 30 s later; a success re-arms it)
            const auto message=snapshot?snapshot->errorMessage:std::string("Save failed");
            Emit(ProjectSaveFailed(op->request.value("path",std::string{}),message,true));
            if(!autosaveFailing) {autosaveFailing=true;notifier->Warning("Autosave failed",message,NotificationCategory::Project);}
        }
        RefreshDirty();
    }
    if(!saveLane.active && !saveLane.queue.empty()) {auto next=std::move(saveLane.queue.front().request);saveLane.queue.clear();StartSave(next);return;}
    if(!saveLane.active && dirty && !recoveryPath.empty() && std::chrono::steady_clock::now()-autosavedAt>=std::chrono::seconds(30)) {
        autosavedAt=std::chrono::steady_clock::now();StartSave({{"operation","project_save"},{"path",recoveryPath},{"autosave",true}});
    }
}
Json Workflow::Reply(const std::function<Result<CommandOutcome>()> &run) {
    try {
        const auto out=run();
        if(!out) {status=out.Error().technicalDetails; return Failure("command_rejected",status);}
        return {{"ok",true},{"job_id",ActiveJob()},{"error",nullptr}};
    } catch(const std::exception &e) {return Failure("invalid_request",e.what());}
}
Json Workflow::Dispatch(const Json &r) { // JSON -> typed command: the registry factory of "scapslab.<operation>" parses the request into its Cmd:: type
    return Reply([&]{CommandContext c;c.Set("request",r);return commands->Execute(CommandID{"scapslab."+r.at("operation").get<std::string>()},std::move(c));});
}
void Workflow::RequireDevice(const std::string &id) const {
    if(std::none_of(project.devices.begin(),project.devices.end(),[&](const auto &d){return d->id==id;})) throw std::invalid_argument("Unknown device_id: "+id);
}
std::vector<std::string> Workflow::ActiveOps() const {
    std::vector<std::string> ops;for(const auto &[id,lane]:lanes) if(lane.active) ops.push_back(lane.Op());return ops;
}
bool Workflow::EditQueued() const {
    for(const auto &[i,lane]:lanes) for(const auto &q:lane.queue) if(q.request.value("operation",std::string{})=="edit") return true;return false;
}
bool Workflow::CancelAll() {
    bool any=false;
    if(library.Busy() && library.cancellable)any=jobs->RequestCancel(library.job);
    for(auto &[id,lane]:lanes) {std::erase_if(lane.queue,[](const Queued &q){return q.request.value("batch",false);});if(lane.active && CancelLane(lane)) any=true;}
    return any;
}
void Workflow::DeferProjectLoad(const Json &r) {
    projectLane.queue.clear();projectLane.Enqueue(r,"project");
    for(auto &[id,lane]:lanes) {std::erase_if(lane.queue,[](const Queued &q){return q.request.value("batch",false);});if(lane.active) (void)CancelLane(lane);}
    status="Cancelling calculation; project will open after pending edits finish";
}
void Workflow::BatchTyped(Ref<const BatchDraft> batch,Ref<const RecorderDraft> recorder) {StartOperation({{"operation","batch_settings"}},project.active_device,{},std::move(batch),std::move(recorder));}
void Workflow::RunBatchTyped(Ref<const BatchDraft> batch,Ref<const RecorderDraft> recorder,Ref<const RunPlan> plan) {StartOperation({{"operation","run_batch"}},project.active_device,{},std::move(batch),std::move(recorder),std::move(plan));}
void Workflow::QuickSweepTyped(Ref<const BatchDraft> batch,Ref<const RecorderDraft> recorder,Ref<const ProjectDraft> snapshot,Ref<const RunPlan> plan) {
    if(!snapshot || Current().draft!=snapshot)throw std::invalid_argument("Project changed; refresh the quick-batch snapshot");
    (void)PrepareBatch(*snapshot,*batch,*recorder);if(plan && plan!=Current().runPlan)throw std::invalid_argument("Run Actions changed; refresh the quick-batch snapshot");
    StartOperation({{"operation","quick_sweep"}},project.active_device,{},std::move(batch),std::move(recorder),std::move(plan));
}
void Workflow::QueueEdit(const Json& request) {
    const auto* latest=EditDraft();const auto source=latest?latest:Current().draft.get();
    if(!source && !request.contains("document"))throw std::invalid_argument("No device document");
    EditTyped(ReadDraftEdit(source,request),request.value("gesture_id",std::string{}));
}
void Workflow::EditTyped(Ref<const ProjectDraft> draft,const std::string& gesture) {
    ValidateDraft(*draft);
    auto& d=Current();if(d.draft && CompareDrafts(*d.draft,*draft).Empty())return;
    auto step=d.draft?Undo::Edit(*this,d.id,*d.draft,*draft,gesture):Unique<ICommand>{};
    auto& lane=LaneOf(d.id);
    // Publish a completed tick before accepting the next edit; only work still running needs cancellation.
    if(lane.active && lane.pending->automatic)if(const auto state=jobs->TryGetStatus(lane.active);state && *state==JobStatus::Completed)FinishOperation(lane);
    const auto running=lane.Op();
    if(lane.active && (running=="run" || running=="band" || running=="cv" || running=="cf" || running=="qe" || running=="dlcp" || running=="single_shot")) {lane.pending->superseded=true;(void)CancelLane(lane);}
    if(!Current().liveV.empty()){Current().previewV=std::move(Current().liveV);Current().previewJ=std::move(Current().liveJ);Current().liveRunning=false;Current().previewCoarse=true;}
    {bool same=d.draft && d.draft->project.layers.size()==draft->project.layers.size();if(same)for(std::size_t i=0;i<draft->project.layers.size();++i)same&=d.draft->project.layers[i].thickness_m==draft->project.layers[i].thickness_m;if(!same){d.liveMesh.reset();d.warmIv.reset();d.warmBand.reset();if(lane.active){lane.pending->superseded=true;(void)CancelLane(lane);}}}
    ApplyDraft(d,std::move(draft));
    if(step && d.undo.PushExecuted(std::move(step)))preferProjectUndo=false;
    d.dirty=!d.undo.IsClean();RefreshDirty();response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();
}
void Workflow::SelectDevice(const std::string &id) {project.active_device=id;projectDirty=true;RefreshDirty();response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();}
void Workflow::SetAutoRecalculate(bool automatic,std::optional<bool> band) {
    autoRecalculate=automatic;autoRecalculateBand=band.value_or(autoRecalculateBand);
    if(autoRecalculate || autoRecalculateBand) Changed();
    response={{"ok",true},{"auto_recalculate",autoRecalculate},{"auto_recalculate_band",autoRecalculateBand},{"error",nullptr}};
    status=(autoRecalculate || autoRecalculateBand)?"Auto-recalculate enabled (250 ms debounce)":"Auto-recalculate disabled";
}
bool Workflow::CancelLane(Lane& lane) {
    bool cancelled=lane.active && jobs->RequestCancel(lane.active);
    if(lane.pending) {
        lane.pending->cancellation.request_stop();
        if(lane.pending->paused && !cancelled) {lane.active=jobs->Submit(lane.pending);cancelled=true;}
        lane.pending->paused=false;
    }
    return cancelled;
}
void Workflow::ExportDlcpTyped(Ref<const AcResult> result,const std::string& path) {StartOperation({{"operation","export_dlcp"},{"path",path}},project.active_device,{},{},{},{},{},std::move(result));}
void Workflow::SetRunPlanTyped(Ref<const RunPlan> plan,const std::string& gesture) {
    if(!plan)throw std::invalid_argument("Run settings are missing");
    // Editing may leave Include empty; execution validates the selected subset.
    for(const auto& a:plan->actions) {auto one=*plan;for(auto& b:one.actions)b.include=b.kind==a.kind;ValidateRunPlan(one,*Current().draft);}
    auto& d=Current();auto before=d.runPlan;d.runPlan=std::move(plan);d.runPlanEdited=true;
    if(d.undo.PushExecuted(Undo::RunSettings(*this,d.id,std::move(before),d.runPlan,gesture)))preferProjectUndo=false;
    autoRecalculate=false;autoRecalculateBand=false; // per-device Actions replace the legacy global toggles
    projectDirty=true;Changed();RefreshDirty();
}
void Workflow::StartActions(const std::string& device,Ref<const RunPlan> plan,bool automatic,bool preview) {
    auto& d=Device(device);Library::ValidateLinks(*d.draft,*library.state);ValidateDraft(*d.draft);ValidateRunPlan(*plan,*d.draft,automatic);
    auto& lane=LaneOf(device);if(!lane.Idle())throw std::invalid_argument("A job is still active on this device");
    auto pending=CreateRef<Operation>(d.draft,Json{{"operation","single_shot"},{"preview",preview}});
    pending->runPlan=std::move(plan);pending->automatic=automatic;pending->revision=d.revision;pending->detailBias=d.selectedBias;
    if(automatic && preview && d.liveGesture)pending->liveMeshInput=d.liveMesh;
    pending->batch=d.batch;
    pending->retainBiasProfiles=true;
    pending->jsonResponse=!lightResponse;pending->feed=CreateRef<LiveFeed>();
    lane.pending=std::move(pending);lane.liveSeq=0;lane.liveAt={};lane.runStarted=std::chrono::steady_clock::now();lane.active=jobs->Submit(lane.pending);
    if(!preview){d.needsRecalculate=false;d.needsBand=false;}d.status="running";status=preview?"Live preview":automatic?"Live Actions":"Single shot";response=Json{};
}
void Workflow::RunDlcpTyped(std::optional<RunAction> action) {
    auto plan=CreateRef<RunPlan>(*Current().runPlan);if(action)plan->actions[5]=std::move(*action);
    for(auto& a:plan->actions)a.include=a.kind==ActionKind::Dlcp;StartActions(project.active_device,std::move(plan));
}
void Workflow::SingleShotTyped(Ref<const RunPlan> plan) {StartActions(project.active_device,plan?std::move(plan):Current().runPlan);}
void Workflow::RecalculateLive(ActionKind kind) {
    auto& d=Current();auto plan=CreateRef<RunPlan>(*d.runPlan);
    for(auto& a:plan->actions)a.onEdit=a.kind==kind;
    if(kind==ActionKind::Band)plan->actions[0].voltage_V=d.biasVoltage_V;
    StartActions(d.id,std::move(plan),true);
    d.pinStoredView=false;ReturnToLive(d);
}
void Workflow::SelectBias(int index) {
    RequireParameter("selection.bias_index",index);
    auto& d=Current();auto source=std::dynamic_pointer_cast<const IvResult>(d.RunResult(d.selectedRun));
    if(!source)source=d.Iv();
    if(index<0 || index>=int(source->points.size()))throw std::invalid_argument("Bias index out of range");
    if(index!=d.selectedBias && d.selectedRun.empty())d.RememberChange();
    d.selectedBias=index;d.biasVoltage_V=source->points[index].voltage_V;
    if(d.selectedRun.empty())d.needsBiasDetails=!source->points[index].details && !(d.BandFresh() && !d.Band()->points.empty() && d.Band()->points.front().voltage_V==d.biasVoltage_V);
    d.changedAt=std::chrono::steady_clock::now();Emit(SelectionChanged(d.id));
}
void Workflow::RecalculateStoredBias() {
    auto& d=Current();auto source=std::dynamic_pointer_cast<const IvResult>(d.RunResult(d.selectedRun));
    if(!source || source->points.empty() || !source->snapshot)throw std::invalid_argument("Stored model snapshot unavailable");
    const auto voltage=source->points.at(std::clamp(d.selectedBias,0,int(source->points.size())-1)).voltage_V;
    StartOperation({{"operation","band"},{"voltage_V",voltage},{"live",true}},d.id,source->snapshot,{},{},{},d.selectedRun);
}
void Workflow::ResumeActions() {
    auto& lane=LaneOf(project.active_device);
    if(!lane.pending || !lane.pending->paused)throw std::invalid_argument("No paused Actions");
    lane.pending->paused=false;
}
void Workflow::FocusResults(const DeviceState& d) {
    if(d.id!=project.active_device || d.selectedRun.empty())return;
    focusView=18;focusResultDevice=d.id;focusResultRun=d.selectedRun;
}
void Workflow::RestoreResultFocus() {
    if(dragging || interacting)return;
    const auto device=std::exchange(focusResultDevice,{}),run=std::exchange(focusResultRun,{});
    if(device!=project.active_device || !HasDevice(device) || !Device(device).RunResult(run))return;
    (void)Exec(Cmd::SelectRun::Make(device,run));
    const auto& d=Device(device);
    if(auto table=std::dynamic_pointer_cast<const BatchResult>(d.RunResult(run));table && table->runPlan){for(const auto& a:table->runPlan->actions)if(a.include){project.ui["results_action"]=ActionId(a.kind);break;}}
    else for(const auto& entry:d.history)if(entry.at("run_id")==run){project.ui["results_action"]=entry.at("kind");break;}
}
void Workflow::CaptureLiveTyped(ActionKind kind) {
    auto& d=Current();if(!LaneOf(d.id).Idle())throw std::invalid_argument("Wait for the current exact live result");
    Ref<const ResultBase> result=kind==ActionKind::Iv?std::static_pointer_cast<const ResultBase>(d.result.typed()):kind==ActionKind::Band?std::static_pointer_cast<const ResultBase>(d.bandResult.typed()):std::static_pointer_cast<const ResultBase>(d.acResults.get(ActionId(kind)));
    if(!result || (kind==ActionKind::Iv && d.resultRevision!=d.revision) || (kind==ActionKind::Band && d.bandRevision!=d.revision) || (d.acRevisions.contains(ActionId(kind)) && d.acRevisions.at(ActionId(kind))!=d.revision))throw std::invalid_argument("No current exact live result");
    const auto id=StoreRun(d.history.mut(),ActionId(kind),result->MetaOf().prim,result->MetaOf().prim.at("project_snapshot"),{{"operation","capture_live"}},project.ui.value("history_retention",5));
    auto runs=CreateRef<RunMap>(*d.runs);(*runs)[id]=std::move(result);d.runs=std::move(runs);d.SyncRuns();d.selectedRun=id;d.selectedAttempt=0;FocusResults(d);
    projectDirty=true;Emit(HistoryChanged(d.id));Emit(SelectionChanged(d.id));RefreshDirty();
}
namespace {
bool IsEditOp(const std::string &op) {return op=="edit" || op=="load_material" || op=="set_profile" || op=="load_profile" || op=="layer" || op=="import_def";}
}
// Undo/redo never wait for a job. Unapplied edits of the active device (running or queued document-changing jobs) are the newest thing the user did: the first
// undo discards them (and redo has nothing to redo); with none, the step pops the preferred stack at once and cancels the device's running calculation, whose
// result is for the old document. Auto-recalculate then starts a fresh run from the restored document.
bool Workflow::DiscardPendingEdits(const std::string &id) {
    auto it=lanes.find(id);if(it==lanes.end()) return false;
    auto &l=it->second;
    bool any=std::erase_if(l.queue,[](const Queued &q){return IsEditOp(q.request.value("operation",std::string{}));})>0;
    if(l.active && IsEditOp(l.Op())) {l.pending->superseded=true;(void)CancelLane(l);any=true;}
    return any;
}
void Workflow::SupersedeJobs(const std::string &id) {
    auto it=lanes.find(id);if(it==lanes.end() || !it->second.active || !it->second.pending) return;
    it->second.pending->superseded=true;(void)CancelLane(it->second);
}
void Workflow::DropDeviceLane(const std::string &id) {
    auto it=lanes.find(id);if(it==lanes.end()) return;
    it->second.queue.clear();SupersedeJobs(id);
}
bool Workflow::CanUndo() const {
    if(const auto available=Library::EditorCanStep(library,true))return *available;
    if(const auto it=lanes.find(project.active_device);it!=lanes.end()) {
        const auto &l=it->second;
        if((l.active && IsEditOp(l.Op())) || std::any_of(l.queue.begin(),l.queue.end(),[](const Queued &q){return IsEditOp(q.request.value("operation",std::string{}));})) return true;
    }
    return (preferProjectUndo?projectUndo:const_cast<Workflow *>(this)->Current().undo).CanUndo();
}
bool Workflow::CanRedo() const {if(const auto available=Library::EditorCanStep(library,false))return *available;return (preferProjectUndo?projectUndo:const_cast<Workflow *>(this)->Current().undo).CanRedo();}
void Workflow::ApplyDraft(DeviceState &d,Ref<const ProjectDraft> draft) {
    d.RememberChange();
    d.liveV.clear();d.liveJ.clear(); // the last preview curve stays visible until a newer one replaces it point by point (owner: no blinking)
    d.SetDraft(std::move(draft));
    d.needsRecalculate=true;d.needsBand=true;d.changedAt=std::chrono::steady_clock::now();d.status=d.history.empty()?"no results":"previous";
    ++d.revision;Emit(DocumentEdited(d.id,d.revision));
    ReturnToLive(d);
}
void Workflow::ReturnToLive(DeviceState &d) { // owner 2026-11-23: an edit shows Live again; the stored run stays drawn as "(previous)"
    if(d.selectedRun.empty() || d.pinStoredView)return;
    d.selectedRun.clear();d.selectedAttempt=0;Emit(SelectionChanged(d.id));
}
Result<void> Workflow::Step(bool undo) {
    const auto libraryBefore=library.state;
    if(const auto result=Library::EditorStep(library,undo)){if(*result){if(library.state!=libraryBefore)RefreshLinkedAssets();projectDirty=true;RefreshDirty();}return *result;}
    const auto id=project.active_device;
    if(DiscardPendingEdits(id)) {
        preferProjectUndo=false;++plotGen;status=undo?"Pending edit discarded":"Nothing to redo";
        if(undo) Changed(); // the calculation the edit cancelled is not coming back on its own: re-arm auto-recalculate
        RefreshDirty();response=Snapshot();return {};
    }
    auto &stack=preferProjectUndo?projectUndo:Current().undo;
    if(undo?!stack.CanUndo():!stack.CanRedo()) return {};
    if(!preferProjectUndo) SupersedeJobs(id);
    const auto result=undo?stack.Undo():stack.Redo();
    if(!result) return result;
    status="History restored";
    ++plotGen;RefreshDirty();response=Snapshot();return {};
}
void Workflow::RunAll() {
    batchTotal=0;batchCompleted=0;batchResponse=Json::array();
    std::vector<std::string> ids;
    for(const auto &d:project.devices) if(project.compare_set.contains(d->id)) ids.push_back(d->id);
    if(ids.empty()) throw std::invalid_argument("Select at least one device for compare");
    batchTotal=ids.size();
    for(const auto &id:ids) Submit(id,{{"operation","run"},{"batch",true}}); // one job per device: the lanes run in parallel
}
void Workflow::Changed() {
    auto &d=Current();d.needsRecalculate=true;d.needsBand=true;d.changedAt=std::chrono::steady_clock::now();
    d.status=d.history.empty()?"no results":"previous";ReturnToLive(d);
}
namespace {
class FreeJob final : public IJob { // last owner of replaced/dropped results: destroying a big result (megabytes of Json) takes tens of ms, so it happens here
    std::vector<Ref<const ResultBase>> results;std::vector<Ref<const Json>> views;Ref<IJob> finished;
public:
    FreeJob(std::vector<Ref<const ResultBase>> r,std::vector<Ref<const Json>> v,Ref<IJob> done={}):results(std::move(r)),views(std::move(v)),finished(std::move(done)) {}
    std::string GetName() const override {return "ScapsLab release results";}
    std::string GetType() const override {return "scapslab.free";}
    void Execute(JobContext &) override {results.clear();views.clear();finished.reset();}
};

}
// UI app only (viewIdleSeconds>0): Json views of results that no panel has read for a while are freed; the typed result stays. Plot caches are keyed by view
// addresses, so a drop bumps plotGen (they rebuild at the next frame boundary).
void Workflow::TrimViews() {
    if(viewIdleSeconds<=0) return;
    const auto now=std::chrono::steady_clock::now();
    if(now-viewTrimAt<std::chrono::seconds(1)) return;
    viewTrimAt=now;const auto idle=now-std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(viewIdleSeconds));
    bool dropped=false;std::vector<Ref<const Json>> dead;
    const auto drop=[&](const ResultBase *r) {Ref<const Json> v;if(r && r->DropIdleView(idle,&v)) {dropped=true;dead.push_back(std::move(v));}};
    for(const auto &d:project.devices) {
        if(d->id!=project.active_device) { // the active device's live results stay readable (rebuilding a view on the UI thread is a visible stall)
            drop(d->result.typed().get());drop(d->bandResult.typed().get());drop(d->batchResult.typed().get());
            for(const auto &[k,r]:d->acResults.all()) drop(r.get());
        }
        for(const auto &[id,r]:*d->runs) drop(r.get());
    }
    if(dropped) {++plotGen;(void)jobs->Submit(CreateRef<FreeJob>(std::vector<Ref<const ResultBase>>{},std::move(dead)));}
}
bool Workflow::ViewsReady() {return true;}

void Workflow::OnUpdate(float) {
    static const bool perf=std::getenv("SCAPSLAB_PERF")!=nullptr; // SCAPSLAB_PERF=1: log UI-thread hitches (>2 ms) to stderr
    const auto t0=std::chrono::steady_clock::now();
    auto& d=Current();
    if(dragging && !d.liveGesture) {
        const auto& meta=d.Iv()->meta.prim;const double estimate=lastFullMs>0?lastFullMs:meta.is_object()?meta.value("compute_time_ms",0.):0.;
        d.liveGesture=true;d.liveStride=livePreviewStride>0?std::clamp(livePreviewStride,1,4):std::clamp(int(std::clamp(std::ceil(estimate/25.),1.,4.)),1,4);d.liveBiases=ActionSamples(d.runPlan->actions[1],d.draft->project);
        const auto result=d.bandResult.empty()?d.Iv():d.Band();const auto& layers=d.draft->project.layers;
        bool sameGeometry=result->snapshot && result->snapshot->project.layers.size()==layers.size();
        if(sameGeometry)for(std::size_t i=0;i<layers.size();++i)sameGeometry&=result->snapshot->project.layers[i].thickness_m==layers[i].thickness_m;
        if(sameGeometry){const auto warm=d.warmBand?d.warmBand:d.warmIv;if(warm)d.liveMesh=CreateRef<const std::vector<double>>(warm->x_m);else if(!result->mesh_x_m.empty())d.liveMesh=CreateRef<const std::vector<double>>(result->mesh_x_m.get());}
    }else if(!dragging && d.liveGesture){d.liveGesture=false;d.liveMesh.reset();d.liveBiases.clear();}
    UpdateOperation();PollLibrary();PollIo();PersistRecent();
    const auto t1=std::chrono::steady_clock::now();
    UpdateTutorial();
    TrimViews();
    if(perf) {const auto t2=std::chrono::steady_clock::now();const double ms=std::chrono::duration<double,std::milli>(t2-t0).count();if(ms>2)std::fprintf(stderr,"[perf] Workflow::OnUpdate %.1f ms (operation %.1f, tutorial+trim %.1f) (status: %s)\n",ms,std::chrono::duration<double,std::milli>(t1-t0).count(),std::chrono::duration<double,std::milli>(t2-t1).count(),status.c_str());}
}
// UI thread: copy the running job's partial data (<= ~10x per second; the worker only appends under a lock, no Json is built for partials).
void Workflow::PullLive(Lane &lane) {
    const auto now=std::chrono::steady_clock::now();
    if(!lane.pending || lane.pending->superseded || !lane.pending->feed || now-lane.liveAt<std::chrono::milliseconds(16) || !HasDevice(lane.device)) return; // (the device may have left the project: undo of "add device")
    auto &feed=*lane.pending->feed;auto &d=Device(lane.device);
    std::lock_guard g(feed.lock);
    if(feed.seq==lane.liveSeq) return;
    lane.liveSeq=feed.seq;lane.liveAt=now;d.liveDlcp=feed.dlcp;d.liveDlts=feed.dlts;
    // Owner 2026-11-23: the changing curve stays visible the whole time. New points replace the old curve from the sweep start;
    // beyond the last computed voltage the previous curve (preview, else the last exact result) is kept.
    auto merge=[](const std::vector<double>& v,const std::vector<double>& j,const std::vector<double>& oldV,const std::vector<double>& oldJ,std::vector<double>& outV,std::vector<double>& outJ) {
        outV=v;outJ=j;if(v.empty() || oldV.size()<2)return;
        const bool up=oldV.back()>=oldV.front();const double last=v.back();
        for(std::size_t i=0;i<oldV.size();++i) if(up?oldV[i]>last:oldV[i]<last){outV.push_back(oldV[i]);outJ.push_back(oldJ[i]);}
    };
    std::vector<double> baseV=d.previewV,baseJ=d.previewJ;
    if(baseV.empty() && !d.result.empty()) for(const auto& pt:d.Iv()->points){baseV.push_back(pt.voltage_V);baseJ.push_back(pt.J_A_m2);}
    if(lane.pending->request.value("preview",false)) {merge(feed.v,feed.j,baseV,baseJ,d.previewV,d.previewJ);d.previewCoarse=true;d.status="preview";return;}
    d.liveRunning=true;
    merge(feed.v,feed.j,baseV,baseJ,d.liveV,d.liveJ);d.pvEstimate=feed.pv;d.liveMembers=feed.members;d.liveMemberTotal=feed.memberTotal;d.liveBatch=feed.batch;
}
// One pass over the lanes: running jobs are checked with a cheap status read (no snapshot copy); an idle lane starts its next queued request.
// Lanes of different devices run concurrently; project-level requests (a load that cancelled calculations) wait until every lane is idle.
void Workflow::UpdateOperation() {
    UpdateSave();
    bool busy=false;
    for(auto &[id,L]:lanes) {
        if(L.active) {
            busy=true;
            if(L.pending && !dragging && L.pending->request.value("preview",false)) {L.pending->superseded=true;(void)CancelLane(L);} // slider released: drop the preview, the exact run follows
            const auto live=jobs->TryGetStatus(L.active);
            if((!live || *live==JobStatus::Completed) && L.pending->runPlan && (L.pending->actionBatch?L.pending->nextMember<L.pending->actionBatch->rows.size():L.pending->nextAction<L.pending->runPlan->actions.size())) {
                if(!L.pending->paused)L.active=jobs->Submit(L.pending);
                continue;
            }
            if(!live || *live==JobStatus::Queued || *live==JobStatus::Running) {if(L.pending->feed) PullLive(L);continue;}
            FinishOperation(L);
        } else if(!L.queue.empty()) {
            busy=true;
            auto next=std::move(L.queue.front());L.queue.pop_front();
            try {StartOperation(next.request,L.device,next.draft,{},{},{},{},{},next.key=="auto" || next.key=="auto-band");} catch(const std::exception &e) {status=e.what();}
        }
    }
    for(auto it=lanes.begin();it!=lanes.end();) it=(it->second.Idle() && !std::ranges::any_of(project.devices,[&](const auto &d){return d->id==it->first;}))?lanes.erase(it):std::next(it); // lanes of devices that left the project
    if(busy) return;
    if(!projectLane.queue.empty()) {auto next=std::move(projectLane.queue.front());projectLane.queue.clear();StartOperation(next.request,project.active_device);return;}
    {
        auto &d=Current();
        const auto now=std::chrono::steady_clock::now();
        // Live band + carrier profiles at the working point (d.biasVoltage_V): same lane, <=30/s while dragging, latest wins, alternating with the I-V preview.
        const bool autoIv=autoRecalculate || d.runPlan->actions[1].onEdit,autoBand=autoRecalculateBand || d.runPlan->actions[0].onEdit;
        const bool anyAuto=autoIv || std::any_of(d.runPlan->actions.begin()+2,d.runPlan->actions.end(),[](const auto& a){return a.onEdit;});
        const bool bandDue=((autoBand && d.needsBand && (dragging || (!interacting && now-d.changedAt>=std::chrono::milliseconds(250)))) || (d.selectedRun.empty() && d.needsBiasDetails)) && now-lastBandLive>=std::chrono::milliseconds(16);
        const bool previewDue=autoIv && dragging && d.needsRecalculate && d.revision!=d.previewRev && now-lastLive>=std::chrono::milliseconds(16);
        int ac=-1;for(int i=2;i<5;++i)if(d.runPlan->actions[i].onEdit && (!d.acRevisions.contains(ActionId(ActionKind(i))) || d.acRevisions.at(ActionId(ActionKind(i)))!=d.revision)){ac=i;break;}
        const bool acDue=dragging && d.needsRecalculate && ac>=0 && now-lastAcLive>=std::chrono::milliseconds(16);
        if(acDue && (!bandDue || lastAcLive<=lastBandLive) && (!previewDue || lastAcLive<=lastLive)) {
            auto plan=CreateRef<RunPlan>(*d.runPlan);for(auto& a:plan->actions)a.onEdit=int(a.kind)==ac;
            auto& a=plan->actions[ac];auto coarse=PreviewSamples(ActionSamples(a,d.draft->project),d.liveStride);
            a.range=SweepRange{SweepSampling::List,false,0,0,1,int(coarse.size()),std::move(coarse)};
            lastAcLive=now;StartActions(d.id,std::move(plan),true,true);
        } else if(bandDue && (!previewDue || lastBandLive<=lastLive)) {
            d.needsBand=false;d.needsBiasDetails=false;lastBandLive=now;++liveBandsStarted;
            Submit(d.id,{{"operation","band"},{"voltage_V",d.biasVoltage_V},{"live",true},{"preview",dragging},{"rev",d.revision}},"auto-band");
        } else if(previewDue) { // live preview: previous run finished (never queued), <=30/s
            lastLive=now;d.previewRev=d.revision;liveSeen=true;++previewsStarted;
            Submit(d.id,{{"operation","run"},{"preview",true},{"stride",d.liveStride}},"auto");
        } else if(anyAuto && d.needsRecalculate && !interacting && !dragging && now-d.changedAt>=std::chrono::milliseconds(liveSeen?100:250)) {
            auto plan=CreateRef<RunPlan>(*d.runPlan);plan->actions[0].onEdit=false;plan->actions[1].onEdit=autoIv;
            d.needsRecalculate=false;liveSeen=false;StartActions(d.id,std::move(plan),true);
        }
    }
}
void Workflow::FinishOperation(Lane &L) {
    auto &pending=L.pending;const auto snapshot=jobs->GetJob(L.active);
    if(!snapshot) return;
    const auto op=pending->request.at("operation").get<std::string>();
    if(!HasDevice(L.device)) { // the device left the project (undo of "add device") while its job ran: nothing to apply
        L.active=0;(void)jobs->Submit(CreateRef<FreeJob>(std::vector<Ref<const ResultBase>>{},std::vector<Ref<const Json>>{},std::move(pending)));pending.reset();return;
    }
    const bool liveBand=op=="band" && pending->request.value("live",false);
    if(op=="single_shot" && !pending->automatic && snapshot->status==JobStatus::Completed)lastFullMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-L.runStarted).count();
    const bool continuation=dragging && pending->automatic && (liveBand || pending->request.value("preview",false));
    const bool dropped=pending->superseded || (!continuation && ((op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts" || op=="single_shot") && pending->revision!=Device(L.device).revision)); // undo/redo changed the document under this job: its result is for an older document
    auto &d=Device(L.device);
    if(d.liveGesture && !pending->superseded && !d.liveMesh && pending->liveMeshOutput)d.liveMesh=pending->liveMeshOutput;
    if(pending->warmOutput){if(op=="band")d.warmBand=pending->warmOutput;else if(op=="run" || op=="single_shot"){d.warmIv=pending->warmOutput;d.warmBand=pending->warmOutput;}}
    std::vector<Ref<const ResultBase>> held; // every result before the swap below: whoever replaces one, the last release (a multi-MB destructor) happens on a worker
    for(const auto &dev:project.devices) {held.push_back(dev->result.typed());held.push_back(dev->bandResult.typed());held.push_back(dev->batchResult.typed());for(const auto &[k,r]:dev->acResults.all()) held.push_back(r);for(const auto &[id,r]:*dev->runs) held.push_back(r);}
    if(pending->feed) {
        // Retain the last drawn curve even if this job is cancelled or superseded.
        if(!d.liveV.empty()){d.previewV=std::move(d.liveV);d.previewJ=std::move(d.liveJ);}
        d.liveDlcp.clear();d.liveDlts.clear();d.pvEstimate=PvEstimate{};d.liveMembers.clear();d.liveMemberTotal=0;d.liveBatch.reset();d.liveRunning=false;
    }
    const bool outValid=pending->typedOut?pending->outputValid:pending->output.is_object() && pending->output.value("valid",false);
    if(!pending->completedActions.empty() && (!dropped || !pending->automatic)) {
        auto runs=CreateRef<RunMap>(*d.runs);
        for(const auto& [kind,result]:pending->completedActions) {
            if(!dropped && kind==ActionKind::Iv) {d.result.adopt(std::static_pointer_cast<const IvResult>(result));if(!d.BandFresh() && std::none_of(pending->completedActions.begin(),pending->completedActions.end(),[](const auto& a){return a.first==ActionKind::Band;}))d.bandResult.clear();d.resultRevision=d.revision;}
            else if(!dropped && kind==ActionKind::Band) {d.bandResult.adopt(std::static_pointer_cast<const IvResult>(result));d.bandRevision=d.revision;}
            else if(!dropped) {d.acResults.set(ActionId(kind),std::static_pointer_cast<const AcResult>(result));d.acRevisions[ActionId(kind)]=d.revision;}
            if(!pending->automatic) {
                const auto& meta=result->MetaOf().prim;
                const auto id=StoreRun(d.history.mut(),ActionId(kind),meta,meta.at("project_snapshot"),pending->request,project.ui.value("history_retention",5),pending->batchGroup);
                (*runs)[id]=result;d.selectedRun=id;d.selectedAttempt=0;
            }
            Emit(RunCompleted(d.id,pending->automatic?"":d.selectedRun,ActionId(kind),d.revision));
        }
        if(!dropped && !pending->automatic)for(const auto& [kind,result]:pending->completedActions){if(kind==ActionKind::Iv){d.changeIv.reset();d.changeBand.reset();}else if(kind==ActionKind::Band)d.changeBand.reset();else d.changeAc.erase(ActionId(kind));}
        d.runs=std::move(runs);d.SyncRuns();if(!dropped && std::any_of(pending->completedActions.begin(),pending->completedActions.end(),[](const auto& a){return a.first==ActionKind::Iv;})){d.previewV.clear();d.previewJ.clear();}d.status=outValid?"solved":"failed";if(!pending->automatic)projectDirty=true;
        if(!pending->automatic) {Emit(HistoryChanged(d.id));Emit(SelectionChanged(d.id));FocusResults(d);}
    }
            if(!pending->members.empty() && !dropped) { // members were prepared on the worker; here they only enter the history
                auto runs=CreateRef<RunMap>(*d.runs);
                for(auto &m:pending->members) {
                    const auto runId=StoreRun(d.history.mut(),"batch",m.result,m.model,pending->request,project.ui.value("history_retention",5),pending->batchGroup,&m.parameters,&m.hash);
                    (*runs)[runId]=m.typed;
                    if(!d.history.empty()) d.history.mut().back()["colour"]=Json::array({.15+.7*m.t,.4+.3*std::sin(3.14159*m.t),.9-.7*m.t,1.});
                }
                d.runs=std::move(runs);d.SyncRuns();
                d.selectedRun=d.history.empty()?"":d.history.back().at("run_id").get<std::string>();
                LimitProfiles(d);Emit(HistoryChanged(d.id));Emit(SelectionChanged(d.id));
            }
    if(pending->actionBatch && !dropped) {
        auto table=pending->actionBatch;
        if(snapshot->status==JobStatus::Cancelled) {
            table->cancelled=true;
            if(pending->nextMember<table->rows.size() && pending->nextAction>0){auto row=CreateRef<scaps_core::BatchRow>(*table->rows[pending->nextMember]);row->status="Cancelled";row->error_type="cancelled";row->message="Stopped after completed Actions";auto member=CreateRef<MemberActions>(*table->actionMembers[pending->nextMember]);member->summary=row;table->actionMembers[pending->nextMember]=std::move(member);table->rows[pending->nextMember]=std::move(row);}
            if(!table->batchDraft->include_curves)for(auto& source:table->actionMembers) {
                auto member=CreateRef<MemberActions>(*source);for(const auto& [kind,result]:member->results)held.push_back(result);
                if(!table->batchDraft->include_profiles){member->results.clear();member->recorderSamples.clear();}
                else {for(auto& [kind,result]:member->results)if(auto iv=std::dynamic_pointer_cast<const IvResult>(result))std::erase_if(std::const_pointer_cast<IvResult>(iv)->points,[](const auto& p){return p.x_m.empty();});std::erase_if(member->results,[](const auto& r){return !std::dynamic_pointer_cast<const IvResult>(r.second);});}
                source=std::move(member);
            }
        }
        double totalElapsed=0;for(const auto& row:table->rows)totalElapsed+=row->elapsed_s;table->elapsed_s=totalElapsed;
        const auto runId="run-"+pending->batchGroup;
        auto& history=d.history.mut();
        if(op=="record_batch" && !pending->request.value("calculate_missing",false)) {
            auto runs=CreateRef<RunMap>(*d.runs);if(std::dynamic_pointer_cast<const BatchResult>(d.RunResult(d.selectedRun)))(*runs)[d.selectedRun]=table;d.runs=std::move(runs);
        } else {
            const Json provenance={{"device_hash",""},{"solver_version",table->solverVersion},{"time_unix_ms",table->time_unix_ms}};
            history.push_back({{"run_id",runId},{"kind","batch"},{"model_hash",""},{"document",Json::object()},{"diff",Json::array()},{"model_diff",Json::array()},{"label",op=="retry_batch"?"Retry batch member":pending->request.value("calculate_missing",false)?"Calculate missing":"Batch Actions"},{"request",pending->request},{"time_unix_ms",provenance.at("time_unix_ms")},{"elapsed_s",table->elapsed_s},{"provenance",provenance},{"pinned",false},{"name",""},{"visible",true},{"colour",Json::array({.25,.65,.95,1.})},{"group_id",""},{"explicit",true}});
            auto runs=CreateRef<RunMap>(*d.runs);(*runs)[runId]=table;d.runs=std::move(runs);d.selectedRun=runId;d.selectedMember=0;d.selectedAttempt=0;
        }
        d.batchResult.adopt(table);projectDirty=true;Emit(HistoryChanged(d.id));Emit(SelectionChanged(d.id));
        FocusResults(d);
    }
    if(op=="run") { // timing for the live-preview policy; an exact run retires the preview curve
        const bool isPreview=pending->request.value("preview",false);const int stride=std::max(1,pending->request.value("stride",1));
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-L.runStarted).count();
        if(snapshot->status==JobStatus::Completed) lastFullMs=isPreview?ms*stride:ms;
        if(!isPreview) {if(snapshot->status==JobStatus::Completed && outValid && !dropped){d.previewV.clear();d.previewJ.clear();}}
        else { // preview: curve only, no result/history/save/events
            if(snapshot->status==JobStatus::Completed && outValid && !dropped) {
                d.previewV.clear();d.previewJ.clear();d.previewCoarse=true;
                for(const auto &pt:std::static_pointer_cast<const IvResult>(pending->typedOut)->points) {d.previewV.push_back(pt.voltage_V);d.previewJ.push_back(pt.J_A_m2);}
                d.status="preview";status=std::string("Preview (")+(pending->request.value("stride",1)>1?"every "+std::to_string(pending->request.value("stride",1))+". point":"all points")+"); exact run on release";
            } else {d.status=d.result.empty()?"no results":"previous";status="Preview dropped";}
            response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();L.active=0;pending.reset();return;
        }
    }
    if(snapshot->status==JobStatus::Completed && !dropped) {
        if(op=="project_new" || op=="project_load" || op=="load") {
            SetWorkspace(pending->workspace,true,&pending->loaded,&pending->loadedDrafts);for(auto &device:project.devices) device->undo.Clear();projectUndo.Clear();projectDirty=false;preferProjectUndo=false;
            library.state=pending->assetState?pending->assetState:Library::Builtins();library.browser=pending->assetBrowser;library.dirty.clear();library.undo.Clear();library.preview.reset();library.selected.reset();
            library.drafts=pending->assetDrafts?pending->assetDrafts:std::make_shared<const Library::DraftMap>();
            ++epoch;path=op=="project_new"?std::string{}:pending->request.at("path").get<std::string>();
            if(op!="project_new" && std::filesystem::path(path).extension()!=".scapslab") {path=std::filesystem::path(path).replace_extension(".scapslab").string();projectDirty=true;}
            for(auto &device:project.devices) {device->dirty=false;device->metadataDirty=false;device->undo.MarkClean();}
            Emit(ProjectLoaded(path));Emit(DeviceListChanged());
        } else if(op=="device_read") {
            Json add={{"operation","device_add"},{"loaded",true}};
            for(const auto *key:{"name","description"}) if(pending->request.contains(key)) add[key]=pending->request.at(key);
            DeviceOp(add,pending->draft);
        } else {
            Unique<ICommand> editStep; // delta of the document change, pushed after the document is in place (a gesture merge compacts against it)
            if(IsEditOp(op) && d.draft && pending->draft && !CompareDrafts(*d.draft,*pending->draft).Empty()) editStep=Undo::Edit(*this,L.device,*d.draft,*pending->draft,pending->request.value("gesture_id",std::string{}));
            if(IsEditOp(op)){d.RememberChange();d.SetDraft(pending->draft);}
            if(editStep && d.undo.PushExecuted(std::move(editStep))) preferProjectUndo=false;
            const int retention=project.ui.value("history_retention",5);
            if((op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts") && !liveBand && !pending->automatic) {
                {const auto runId=StoreRun(d.history.mut(),op=="run"?"iv":op,pending->output,d.Document(),pending->request,retention);
                    auto runs=CreateRef<RunMap>(*d.runs);(*runs)[runId]=pending->typedOut;d.runs=std::move(runs);d.SyncRuns();}
                d.selectedRun=d.history.empty()?"":d.history.back().at("run_id").get<std::string>();
                Emit(HistoryChanged(d.id));Emit(SelectionChanged(d.id));FocusResults(d);
            }
            if(op=="batch_select") {d.selectedMember=pending->request.at("index");d.selectedAttempt=0;}
            if(op=="batch_settings") {d.SetBatch(pending->batch);d.SetRecorder(pending->recorder);projectDirty=true;}
            if(op=="quick_sweep" || op=="run_batch" || op=="retry_batch" || op=="record_batch") {d.batchResult.adopt(std::static_pointer_cast<const BatchResult>(pending->typedOut));projectDirty=true;FocusResults(d);Emit(RunCompleted(d.id,d.selectedRun,op,d.revision));}
            if(op=="run") {d.result.adopt(std::static_pointer_cast<const IvResult>(pending->typedOut));d.bandResult.clear();d.resultRevision=d.revision;}
            if(op=="band") {if(!pending->detailRun.empty()){d.storedBand=std::static_pointer_cast<const IvResult>(pending->typedOut);d.detailRun=pending->detailRun;}else {d.bandResult.adopt(std::static_pointer_cast<const IvResult>(pending->typedOut));d.bandRevision=pending->revision;}}
            if(op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts") {d.acResults.set(op,std::static_pointer_cast<const AcResult>(pending->typedOut));d.acRevisions[op]=d.revision;}
            if(!pending->automatic){if(op=="run"){d.changeIv.reset();d.changeBand.reset();}else if(op=="band" && pending->detailRun.empty())d.changeBand.reset();else if(op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts")d.changeAc.erase(op);}
            if(op=="run" || (op=="band" && pending->detailRun.empty())) LimitProfiles(d);
            if(liveBand) {
                d.status=d.result.empty()?"solved":"previous";
                Emit(RunCompleted(d.id,pending->detailRun, "band",d.revision));
            }
            else if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts") {if(!pending->automatic)d.actions[op=="run"?"iv":op]=pending->request;d.status=outValid?"solved":"failed";projectDirty=true;
                if(outValid) Emit(RunCompleted(d.id,d.selectedRun,op=="run"?"iv":op,d.revision));
                else {const std::string message="solver returned no valid result";Emit(RunFailed(d.id,op,message,false));notifier->Error("Calculation failed",d.name+": "+message);}
            }
            if(IsEditOp(op)) {
                if(op=="import_def") {d.selectedLayer=0;d.selectedInterface=-1;}
                d.needsRecalculate=true;d.needsBand=true;d.changedAt=std::chrono::steady_clock::now();d.status=d.history.empty()?"no results":"previous";d.dirty=!d.undo.IsClean();
                ++d.revision;Emit(DocumentEdited(d.id,d.revision));ReturnToLive(d);
                if(op=="import_def") Emit(SelectionChanged(d.id));
            }
            if(op=="save" && project.devices.size()==1) {d.dirty=false;d.metadataDirty=false;d.undo.MarkClean();}
        }
        if((op=="load" || op=="project_load" || op=="save" || op=="project_save") && !pending->request.value("autosave",false)) {
            const auto file=pending->request.at("path");if(op=="save" || op=="project_save") Emit(ProjectSaved(file.get<std::string>(),false));TouchRecent(file.get<std::string>());
        }
        const bool quiet=(op=="project_save" && pending->request.value("autosave",false)) || liveBand;const auto keepStatus=status;const auto keepResponse=quiet?response:Json();RefreshDirty();status="Completed: "+op;response=lightResponse?Json{{"ok",true},{"error",nullptr}}:Snapshot();
        if(pending->output.contains("size_bytes")) status="Saved "+pending->output.at("path").get<std::string>()+" ("+std::to_string(pending->output.at("size_bytes").get<std::size_t>())+" bytes)";
        if(op=="project_load" && !recoveryPath.empty() && pending->request.at("path")==recoveryPath) {path.clear();projectDirty=true;RefreshDirty();response=Snapshot();status="Recovered unsaved project; use Save As";}
        if(op=="project_save" && !pending->request.value("autosave",false)) DropRecovery(path);
        if(!lightResponse && !pending->output.empty()) response["operation_result"]=pending->typedOut?pending->typedOut->Dump():pending->output;
        if(quiet) {status=liveBand?(pending->detailRun.empty()?"Live band updated":"Stored bias loaded"):keepStatus;response=keepResponse;}
        if(op=="single_shot" && pending->automatic)status="Live Actions completed";
        if(op=="project_load" || op=="load" || op=="device_read" || op=="import_def") {
            std::string warnings;
            const auto append=[&](const ProjectDraft& draft){for(const auto& warning:draft.importWarnings){if(!warnings.empty())warnings+='\n';warnings+=warning;}};
            if(pending->draft)append(*pending->draft);for(const auto& draft:pending->loadedDrafts)append(*draft);
            if(!warnings.empty()){projectDirty=true;RefreshDirty();status="Imported values clamped: "+warnings;notifier->Warning("Imported parameter bounds",warnings,NotificationCategory::Project);}
        }
    } else {
        const bool wasCancelled=dropped || snapshot->status==JobStatus::Cancelled;
        if(!dropped && (op=="quick_sweep" || op=="run_batch" || op=="retry_batch") && pending->typedOut) {d.batchResult.adopt(std::static_pointer_cast<const BatchResult>(pending->typedOut));projectDirty=true;}
        if(liveBand) d.status=d.result.empty()?"no results":"previous";
        else if(op=="single_shot" && !dropped)d.status=wasCancelled?"stopped":"failed";
        else if(op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts") d.status=wasCancelled?(d.result.empty()?"no results":"solved"):"failed";
        if(!dropped && !liveBand) {status=snapshot->errorMessage;response=Failure(wasCancelled?"cancelled":"operation_failed",status);} // a dropped job leaves the undo's own status and snapshot
        {
            const bool cancelled=wasCancelled,isRun=op=="single_shot" || op=="run" || op=="band" || op=="cv" || op=="cf" || op=="qe" || op=="dlcp" || op=="dlts" || op=="laplace_dlts" || op=="capture_dlts" || op=="optical_dlts" || op=="quick_sweep" || op=="run_batch" || op=="retry_batch";
            if(isRun && !liveBand) Emit(RunFailed(d.id,op,status,cancelled));
            if(!cancelled && !liveBand && !(op=="project_save" && pending->request.value("autosave",false))) {
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
        ++batchCompleted;batchResponse.push_back({{"device_id",L.device},{"ok",response.at("ok")},{"error",response.at("error")}});
        if(batchCompleted>=batchTotal) {const bool ok=std::all_of(batchResponse.begin(),batchResponse.end(),[](const Json &r){return r.at("ok").get<bool>();});response=Snapshot();response["ok"]=ok;response["runs"]=batchResponse;if(!ok)response["error"]={{"code",(dropped || snapshot->status==JobStatus::Cancelled)?"cancelled":"operation_failed"},{"message","One or more device runs failed"}};}
    }
    pending->typedOut.reset();pending->loaded.clear();pending->workspace=Json();
    (void)jobs->Submit(CreateRef<FreeJob>(std::move(held),std::vector<Ref<const Json>>{},std::move(pending))); // the finished Operation (its output Json can be huge) dies on a worker too
    L.active=0;pending.reset();
}
}
