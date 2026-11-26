#include "Library/Library.hpp"
#include "Domain/ParameterBounds.hpp"
#include <scaps_core/illumination.hpp>
#include <type_traits>

namespace SST::ScapsLab::Library {
using namespace scaps_core;
const char* Kind(const Payload& p) {static constexpr const char* names[]={"material","defect","profile","interface","contact","illumination","optical","layer","device"};return names[p.index()];}
Catalog Empty(std::string id,std::string name,Assets::Scope scope) {Catalog out{ValidatePayload};Assets::Require(Assets::ValidId(id),"Invalid library ID");out.id=std::move(id);out.name=std::move(name);out.scope=scope;return out;}
namespace {
void Put(ProjectLayer& l,const Material& m) {
    l.material=m.values;l.Nd_m3=m.Nd_m3;l.Na_m3=m.Na_m3;l.radiative_cm3_s=m.radiative_cm3_s;l.auger_n_cm6_s=m.auger_n_cm6_s;l.auger_p_cm6_s=m.auger_p_cm6_s;l.absorption=m.absorption;
}
std::string Prefix(const Payload& p) {
    if(std::holds_alternative<Illumination>(p))return "illumination.";
    if(std::holds_alternative<Contact>(p))return "left_contact.";
    if(std::holds_alternative<Interface>(p))return "interface_defects[";
    if(std::holds_alternative<Defect>(p))return "layers[0].defects[0].";
    if(std::holds_alternative<Optical>(p))return "layers[0].absorption.";
    if(const auto* profile=std::get_if<Profile>(&p))return "layers[0].grading."+profile->target+".";
    if(std::holds_alternative<Device>(p))return {};
    return "layers[0].";
}
void RequireTable(const OpticalTable& values,const std::string& label,bool positiveX=true) {
    double previous=-INFINITY;for(const auto& [x,y]:values){Assets::Require(std::isfinite(x) && std::isfinite(y) && (!positiveX || x>0) && x>previous && y>=0,label+": expected sorted finite nonnegative data with unique coordinates");previous=x;}
}
void RequireComposition(const ProjectLayer& layer,const Assets::Validity& validity) {
    if((!validity.compositionMin || *validity.compositionMin<=0) && (!validity.compositionMax || *validity.compositionMax>=1))return;
    Assets::Require(layer.grading && layer.grading->composition && layer.grading->composition->law==GradingLaw::Uniform,"This composition-specific snapshot requires an explicit uniform composition; use a layer/grading template for a varying composition");
    const auto value=layer.grading->composition->left;
    Assets::Require((!validity.compositionMin || value>=*validity.compositionMin) && (!validity.compositionMax || value<=*validity.compositionMax),"Layer composition is outside asset validity [1]");
}
}
DeviceProject Carrier(const Payload& payload) {
    auto p=make_default_device_project();p.layers.resize(2);p.interface_defects.clear();p.metastable_transitions.clear();p.scaps_native={};
    std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>;
        if constexpr(std::is_same_v<T,Material>)Put(p.layers[0],v);
        else if constexpr(std::is_same_v<T,Defect>)p.layers[0].defects={v.value};
        else if constexpr(std::is_same_v<T,Profile>){p.layers[0].grading.emplace();GradingProfile(*p.layers[0].grading,v.target)=v.value;}
        else if constexpr(std::is_same_v<T,Interface>){p.interface_defects=v.defects;for(auto& d:p.interface_defects)d.left_layer_index=0;}
        else if constexpr(std::is_same_v<T,Contact>){p.left_contact=v.value;if(v.reflectance)p.illumination.front_internal_reflection=*v.reflectance;}
        else if constexpr(std::is_same_v<T,Illumination>)p.illumination=v.value;
        else if constexpr(std::is_same_v<T,Optical>)p.layers[0].absorption=v.absorption;
        else if constexpr(std::is_same_v<T,Layer>)p.layers[0]=v.value;
        else if constexpr(std::is_same_v<T,Device>)p=v.value;
    },payload);return p;
}
Payload FromCarrier(const std::string& kind,const DeviceProject& p,const Payload* extras,const std::string& target) {
    Assets::Require(!p.layers.empty(),"Asset carrier needs a layer");const auto& l=p.layers.front();
    if(kind=="material") {Material m;if(extras)if(const auto* old=std::get_if<Material>(extras))m=*old;m.values=l.material;m.Nd_m3=l.Nd_m3;m.Na_m3=l.Na_m3;m.radiative_cm3_s=l.radiative_cm3_s;m.auger_n_cm6_s=l.auger_n_cm6_s;m.auger_p_cm6_s=l.auger_p_cm6_s;m.absorption=l.absorption;return m;}
    if(kind=="defect"){Assets::Require(l.defects.size()==1,"Defect item needs exactly one bulk defect");return Defect{l.defects.front()};}
    if(kind=="profile"){Assets::Require(bool(l.grading),"Profile item needs grading");const auto& g=GradingProfile(*l.grading,target);Assets::Require(bool(g),"Profile target is missing: "+target);return Profile{target,*g};}
    if(kind=="interface"){Assets::Require(!p.interface_defects.empty(),"Interface item needs defects");Interface value{p.interface_defects};if(extras)if(const auto* old=std::get_if<Interface>(extras))value.referenceReviewRequired=old->referenceReviewRequired;return value;}
    if(kind=="contact"){Contact c{p.left_contact};if(extras)if(const auto* old=std::get_if<Contact>(extras))c.reflectance=old->reflectance;return c;}
    if(kind=="illumination")return Illumination{p.illumination};
    if(kind=="optical"){Optical o{l.absorption};if(extras)if(const auto* old=std::get_if<Optical>(extras))o.refractiveIndex=old->refractiveIndex;return o;}
    if(kind=="layer")return Layer{l};if(kind=="device")return Device{p};
    throw Assets::Error(Assets::ErrorCode::Unsupported,"Unknown asset kind: "+kind);
}
void ValidatePayload(const Payload& v) {
    // Existing typed bounds and physics validation are the source of truth.
    const auto draft=DraftFromProject(Carrier(v));const auto& p=draft->project;
    for(const auto& l:p.layers) {
        RequireTable(l.absorption.table_nm_m1,"Absorption");
        Assets::Require(l.absorption.model!="file" || !l.absorption.table_nm_m1.empty(),"Absorption file must be embedded as a table");
        if(l.grading)for(const auto* key:{"composition","Eg_eV","chi_eV","eps_r","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","Nd_m3","Na_m3","vth_n_ref_m_s","vth_p_ref_m_s"})if(const auto& g=GradingProfile(*l.grading,key);g)validate_profile(*g);
    }
    const auto& light=p.illumination;RequireTable(light.spectrum_nm_W_m2,"Spectrum");RequireTable(light.generation_m_m3s,"Generation",false);
    Assets::Require(light.mode!="spectrum" || !light.enabled || !light.spectrum_nm_W_m2.empty(),"Spectrum must contain embedded samples");
    Assets::Require(light.mode!="generation_file" || !light.enabled || !light.generation_m_m3s.empty(),"Generation must contain embedded samples");
    if(const auto* m=std::get_if<Material>(&v)) {
        if(m->bandgapTemperature){const auto& t=*m->bandgapTemperature;RequireParameter("Eg_eV",t.Eg0_eV);RequireParameter("varshni_alpha_eV_K",t.alpha_eV_K);RequireParameter("temperature_K",t.beta_K);Assets::Require(std::isfinite(t.Eg0_eV) && t.Eg0_eV>0 && std::isfinite(t.alpha_eV_K) && t.alpha_eV_K>=0 && std::isfinite(t.beta_K) && t.beta_K>0,"Invalid Varshni law [eV, eV/K, K]");}
        if(m->effectiveMass){RequireParameter("effective_mass_ratio",m->effectiveMass->electronRatio);RequireParameter("effective_mass_ratio",m->effectiveMass->holeRatio);Assets::Require(std::isfinite(m->effectiveMass->electronRatio) && std::isfinite(m->effectiveMass->holeRatio) && m->effectiveMass->electronRatio>0 && m->effectiveMass->holeRatio>0,"Effective mass ratios must be positive");}
    }
    if(const auto* c=std::get_if<Contact>(&v))if(c->reflectance)Assets::Require(std::isfinite(*c->reflectance) && *c->reflectance>=0 && *c->reflectance<=1,"Contact reflectance must be in [0,1]");
    if(const auto* o=std::get_if<Optical>(&v)){double previous=0;for(const auto& row:o->refractiveIndex){Assets::Require(std::isfinite(row[0]) && row[0]>previous && std::isfinite(row[1]) && row[1]>0 && std::isfinite(row[2]) && row[2]>=0,"n,k table needs increasing wavelength [nm], n>0, k>=0");previous=row[0];}}
}
void ValidateRevision(const Revision& r) {
    Assets::Validate(r.meta);Assets::Require(bool(r.payload) && r.version>0,"Missing payload/version");ValidatePayload(*r.payload);
    if(r.meta.parameters.empty())return;
    std::optional<DeviceProject> project;
    const auto parameter=[&](const std::string& path){if(const auto* m=std::get_if<Material>(r.payload.get())){if(m->bandgapTemperature){const auto& t=*m->bandgapTemperature;if(path=="bandgap_temperature.Eg0_eV")return t.Eg0_eV;if(path=="bandgap_temperature.alpha_eV_K")return t.alpha_eV_K;if(path=="bandgap_temperature.beta_K")return t.beta_K;}if(m->effectiveMass){if(path=="effective_mass.electron_ratio")return m->effectiveMass->electronRatio;if(path=="effective_mass.hole_ratio")return m->effectiveMass->holeRatio;}}if(!project)project=Carrier(*r.payload);return ProjectParameter(*project,path);};
    const auto sameUnit=[](const std::string& unit,const std::string& expected){if(unit==expected)return true;for(const auto& [a,b]:std::initializer_list<std::pair<const char*,const char*>>{{"m^2/(V s)","m^2 V^-1 s^-1"},{"m/s","m s^-1"},{"cm^3/s","cm^3 s^-1"}})if((unit==a && expected==b) || (unit==b && expected==a))return true;return false;};
    for(const auto& range:r.meta.parameters){const auto value=parameter(range.path);Assets::Require(sameUnit(range.unit,NativeUnit(range.path)),range.path+": range unit must be "+NativeUnit(range.path));Assets::Require((!range.lower || value>=*range.lower) && (!range.upper || value<=*range.upper),range.path+": value outside the cited range ["+range.unit+"]");}
}
std::shared_ptr<const ProjectDraft> Apply(const ProjectDraft& original,const Revision& r,const std::string& libraryId,const Target& target,Assets::UsePolicy policy) {
    ValidateRevision(r);Assets::Require(Assets::ValidId(libraryId),"Invalid library reference");
    const double temperature=std::holds_alternative<Device>(*r.payload)?std::get<Device>(*r.payload).value.temperature_K:original.project.temperature_K;const auto& validity=r.meta.validity;
    Assets::Require((!validity.temperatureMin_K || temperature>=*validity.temperatureMin_K) && (!validity.temperatureMax_K || temperature<=*validity.temperatureMax_K),"Device temperature is outside asset validity [K]");
    auto out=std::make_shared<ProjectDraft>(original);auto& p=out->project;std::string destination;
    auto layer=std::find_if(out->layers.begin(),out->layers.end(),[&](const auto& v){return v.id==target.layerId;});
    const bool needsLayer=std::holds_alternative<Material>(*r.payload) || std::holds_alternative<Defect>(*r.payload) || std::holds_alternative<Profile>(*r.payload) || std::holds_alternative<Optical>(*r.payload) || std::holds_alternative<Layer>(*r.payload);
    if(needsLayer && !target.appendLayer)Assets::Require(layer!=out->layers.end(),"Choose a destination layer");
    std::size_t index=std::size_t(layer-out->layers.begin());
    if(target.appendLayer){Assets::Require(std::holds_alternative<Layer>(*r.payload),"Only a layer template can be appended");EditLayer(*out,"add");index=out->layers.size()-1;}
    if(needsLayer)destination="layer:"+out->layers[index].id+":"+Kind(*r.payload);
    std::visit([&](const auto& value){using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,Material>){auto& l=p.layers[index];if(l.grading)for(const auto* key:{"Eg_eV","chi_eV","eps_r","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","Nd_m3","Na_m3","vth_n_ref_m_s","vth_p_ref_m_s"})Assets::Require(!GradingProfile(*l.grading,key),"Layer has parameter grading; remove it explicitly before replacing its material");Put(l,value);if(value.bandgapTemperature){const auto& t=*value.bandgapTemperature;l.material.Eg_eV=t.Eg0_eV-t.alpha_eV_K*temperature*temperature/(temperature+t.beta_K);}}
        else if constexpr(std::is_same_v<T,Defect>){int n=-1;if(!target.defectId.empty()){const auto& ids=out->layers[index].defects;const auto found=std::find_if(ids.begin(),ids.end(),[&](const auto& id){return id.id==target.defectId;});Assets::Require(found!=ids.end(),"Asset defect destination was removed");n=int(found-ids.begin());}else n=AddDraftDefect(*out,int(index),false);p.layers[index].defects[n]=value.value;out->layers[index].defects[n].name=value.value.label;destination="defect:"+out->layers[index].defects[n].id;}
        else if constexpr(std::is_same_v<T,Profile>){auto& l=p.layers[index];if(!l.grading)l.grading.emplace();auto& g=GradingProfile(*l.grading,value.target);g=value.value;ResetProfileFields(*out,*g);destination+="/"+value.target;}
        else if constexpr(std::is_same_v<T,Interface>){Assets::Require(!value.referenceReviewRequired,"Derive this interface and explicitly review its SCAPS energy-reference convention before applying it");Assets::Require(target.interfaceAfter>=0 && target.interfaceAfter+1<int(p.layers.size()),"Choose an internal layer boundary");const auto layerId=out->layers[target.interfaceAfter].id;const auto prefix="asset:"+libraryId+":"+r.meta.id+":"+layerId+":";std::erase_if(p.interface_defects,[&](const auto& d){return d.left_layer_index==target.interfaceAfter && d.id.starts_with(prefix);});unsigned n=0;for(auto d:value.defects){d.left_layer_index=target.interfaceAfter;d.id=prefix+std::to_string(++n);p.interface_defects.push_back(std::move(d));}destination="interface:"+layerId+":"+libraryId+"/"+r.meta.id;}
        else if constexpr(std::is_same_v<T,Contact>){(target.rightContact?p.right_contact:p.left_contact)=value.value;if(value.reflectance)(target.rightContact?p.illumination.back_reflection:p.illumination.front_internal_reflection)=*value.reflectance;destination=target.rightContact?"contact:right":"contact:left";}
        else if constexpr(std::is_same_v<T,Illumination>){p.illumination=value.value;destination="illumination";}
        else if constexpr(std::is_same_v<T,Optical>){Assets::Require(value.absorption.model!="none" || value.refractiveIndex.empty(),"n,k data is retained for inspection; supply alpha(lambda) to apply it to this solver");p.layers[index].absorption=value.absorption;}
        else if constexpr(std::is_same_v<T,Layer>){p.layers[index]=value.value;out->layers[index].name=value.value.name;out->layers[index].defects.clear();for(std::size_t k=0;k<value.value.defects.size();++k)out->layers[index].defects.push_back({out->layers[index].id+"-asset-defect-"+std::to_string(k+1),value.value.defects[k].label});if(p.layers[index].grading)for(const auto* key:{"composition","Eg_eV","chi_eV","eps_r","Nc_ref_m3","Nv_ref_m3","mu_n_m2_V_s","mu_p_m2_V_s","Nd_m3","Na_m3","vth_n_ref_m_s","vth_p_ref_m_s"})if(const auto& g=GradingProfile(*p.layers[index].grading,key);g)ResetProfileFields(*out,*g);}
        else if constexpr(std::is_same_v<T,Device>){out=std::make_shared<ProjectDraft>(*DraftFromProject(value.value));destination="device";}
    },*r.payload);
    if(needsLayer && target.composition){Assets::Require(std::isfinite(*target.composition) && *target.composition>=0 && *target.composition<=1,"Composition must be in [0,1]");auto& layer=p.layers[index];if(!layer.grading)layer.grading.emplace();scaps_core::Profile composition;composition.left=*target.composition;layer.grading->composition=composition;ResetProfileFields(*out,*layer.grading->composition);}
    if(needsLayer)RequireComposition(p.layers[index],validity);
    if(needsLayer && !target.appendLayer && std::holds_alternative<Layer>(*r.payload)) {
        const auto prefix="layer:"+out->layers[index].id+":";
        const auto& oldDefects=original.layers[std::min(index,original.layers.size()-1)].defects;
        std::erase_if(out->assetUses,[&](const auto& use){return use.target.starts_with(prefix) || std::any_of(oldDefects.begin(),oldDefects.end(),[&](const auto& id){return use.target=="defect:"+id.id;});});
    }
    Assets::Use use{{libraryId,r.meta.id,r.version},policy,destination};
    std::erase_if(out->assetUses,[&](const auto& old){return old.target==destination;});out->assetUses.push_back(std::move(use));
    ValidateDraft(*out);return out;
}
std::vector<Assets::Usage> Usages(const std::string& project,const std::vector<std::pair<std::string,std::shared_ptr<const ProjectDraft>>>& devices) {
    std::vector<Assets::Usage> out;for(const auto& [id,d]:devices)if(d)for(const auto& use:d->assetUses)out.push_back({project,id,d->project.case_id,use});return out;
}
bool UpdateAvailable(const Assets::Use& use,const Catalog& catalog) {if(use.source.libraryId!=catalog.id)return false;try{return catalog.Find(use.source.itemId)->version>use.source.version;}catch(const Assets::Error&){return false;}}
std::vector<Difference> Compare(const Payload& a,const Payload& b) {
    Assets::Require(a.index()==b.index(),"Compare items of the same kind");const auto before=ProjectNumbers(Carrier(a),false),after=ProjectNumbers(Carrier(b),false);const auto prefix=Prefix(a);std::vector<Difference> out;
    for(const auto& [path,value]:after)if(path.starts_with(prefix)){const auto old=before.find(path);if(old!=before.end() && old->second!=value)out.push_back({path,NativeUnit(path),old->second,value});}
    const auto text=[&](const std::string& path,const std::string& x,const std::string& y){if(x!=y)out.push_back({path,"1",0,0,x,y});};
    const auto number=[&](const char* path,const char* unit,double x,double y){if(x!=y)out.push_back({path,unit,x,y});};
    if(const auto* x=std::get_if<Profile>(&a)){const auto& y=std::get<Profile>(b);text("profile.target",x->target,y.target);text("profile.law",grading_law_name(x->value.law),grading_law_name(y.value.law));if(x->value.coordinate!=y.value.coordinate || x->value.value!=y.value.value)text("profile.table","reference samples","changed samples");}
    if(const auto* x=std::get_if<Defect>(&a)){const auto& y=std::get<Defect>(b);text("defect.distribution",x->value.energy_distribution,y.value.energy_distribution);text("defect.charge_type",to_string(x->value.charge_type),to_string(y.value.charge_type));text("defect.energy_reference",to_string(x->value.energy_reference),to_string(y.value.energy_reference));}
    if(const auto* x=std::get_if<Optical>(&a)){const auto& y=std::get<Optical>(b);text("absorption.model",x->absorption.model,y.absorption.model);if(x->absorption.table_nm_m1!=y.absorption.table_nm_m1)text("absorption.table_nm_m1","reference samples","changed samples");if(x->refractiveIndex!=y.refractiveIndex)text("refractive_index","reference n,k","changed n,k");}
    if(const auto* x=std::get_if<Material>(&a)){const auto& y=std::get<Material>(b);text("bandgap_temperature",x->bandgapTemperature?"Varshni":"constant",y.bandgapTemperature?"Varshni":"constant");if(x->bandgapTemperature && y.bandgapTemperature){number("bandgap_temperature.Eg0_eV","eV",x->bandgapTemperature->Eg0_eV,y.bandgapTemperature->Eg0_eV);number("bandgap_temperature.alpha_eV_K","eV K^-1",x->bandgapTemperature->alpha_eV_K,y.bandgapTemperature->alpha_eV_K);number("bandgap_temperature.beta_K","K",x->bandgapTemperature->beta_K,y.bandgapTemperature->beta_K);}text("effective_mass",x->effectiveMass?"specified":"not specified",y.effectiveMass?"specified":"not specified");if(x->effectiveMass && y.effectiveMass){number("effective_mass.electron_ratio","1",x->effectiveMass->electronRatio,y.effectiveMass->electronRatio);number("effective_mass.hole_ratio","1",x->effectiveMass->holeRatio,y.effectiveMass->holeRatio);}}
    if(const auto* x=std::get_if<Illumination>(&a)){const auto& y=std::get<Illumination>(b);text("illumination.mode",x->value.mode,y.value.mode);text("illumination.enabled",x->value.enabled?"enabled":"disabled",y.value.enabled?"enabled":"disabled");if(x->value.spectrum_nm_W_m2!=y.value.spectrum_nm_W_m2)text("illumination.spectrum","reference samples","changed samples");}
    if(const auto* x=std::get_if<Contact>(&a)){const auto& y=std::get<Contact>(b);if(x->reflectance && y.reflectance)number("reflectance","1",*x->reflectance,*y.reflectance);else text("reflectance",x->reflectance?"specified":"not specified",y.reflectance?"specified":"not specified");}
    return out;
}
std::map<std::string,double> EditableNumbers(const Payload& payload,const DeviceProject* carrier) {
    auto numbers=carrier?ProjectNumbers(*carrier,false):ProjectNumbers(Carrier(payload),false);const auto prefix=Prefix(payload);
    std::erase_if(numbers,[&](const auto& entry){const auto& path=entry.first;if(!path.starts_with(prefix))return true;if(const auto* profile=std::get_if<Profile>(&payload))return !ProfileFieldActive(profile->value,path.substr(prefix.size()));
        if(const auto* material=std::get_if<Material>(&payload)){if(path.find(".defects[")!=path.npos || path.find(".grading.")!=path.npos || path=="layers[0].thickness_m")return true;if(material->bandgapTemperature && path=="layers[0].material.Eg_eV")return true;}
        if(path.find(".absorption.")!=path.npos){const auto* material=std::get_if<Material>(&payload);const auto* optical=std::get_if<Optical>(&payload);const auto* model=material?&material->absorption:optical?&optical->absorption:nullptr;if(model && (model->model=="none" || model->model=="file"))return true;if(model && model->model=="sqrt")return !path.ends_with("sqrt_alpha_m1") && !path.ends_with("sqrt_beta_m1");}
        return false;});return numbers;
}
std::string NativeUnit(const std::string& path) {
    const auto key=ParameterBoundsKey(path);
    if(key=="peak_density_m2_eV")return "m^-2 eV^-1";
    if(key=="A_m1_eV_half")return "m^-1 eV^-1/2";
    if(key=="B_m1_eV_minus_half")return "m^-1 eV^1/2";
    if(key.ends_with("_eV"))return "eV";
    if(key.ends_with("_eV_K"))return "eV K^-1";
    if(key=="beta_K")return "K";
    if(key.ends_with("_nm"))return "nm";
    if(key=="radiative_cm3_s")return "cm^3 s^-1";
    if(key=="auger_n_cm6_s" || key=="auger_p_cm6_s")return "cm^6 s^-1";
    if(key=="spectral_irradiance_W_m2_nm")return "W m^-2 nm^-1";
    const auto bounds=BoundsTable();const auto found=bounds->find(key);return found==bounds->end()?"1":found->second.unit;
}
void ValidateLinks(const ProjectDraft& draft,const State& state) {
    for(const auto& use:draft.assetUses)if(use.policy==Assets::UsePolicy::Linked) {
        const auto library=state.libraries.find(use.source.libraryId);
        if(library==state.libraries.end())throw Assets::Error(Assets::ErrorCode::Missing,"Linked library is unavailable: "+use.source.libraryId);
        const auto source=library->second.catalog.Find(use.source.itemId);
        if(source->version!=use.source.version)throw Assets::Error(Assets::ErrorCode::Conflict,"Linked source changed; refresh asset updates before calculating");
        const auto& validity=source->meta.validity;const auto temperature=draft.project.temperature_K;
        Assets::Require((!validity.temperatureMin_K || temperature>=*validity.temperatureMin_K) && (!validity.temperatureMax_K || temperature<=*validity.temperatureMax_K),"Linked asset temperature validity exceeded [K]");
        if(use.target.starts_with("layer:")){const auto end=use.target.find(':',6);const auto id=use.target.substr(6,end-6);const auto layer=std::find_if(draft.layers.begin(),draft.layers.end(),[&](const auto& l){return l.id==id;});Assets::Require(layer!=draft.layers.end(),"Linked layer was removed");RequireComposition(draft.project.layers[layer-draft.layers.begin()],validity);}
    }
}
namespace {
template<class Y> std::vector<std::size_t> PreviewIndices(std::size_t count,Y value) {
    std::vector<std::size_t> out;if(!count)return out;
    if(count<=4096){out.resize(count);for(std::size_t i=0;i<count;++i)out[i]=i;return out;}
    out.push_back(0);const auto bucket=(count+2045)/2046;
    for(std::size_t first=1;first+1<count;first+=bucket){const auto end=std::min(count-1,first+bucket);auto low=first,high=first;for(auto i=first+1;i<end;++i){if(value(i)<value(low))low=i;if(value(i)>value(high))high=i;}out.push_back(std::min(low,high));if(low!=high)out.push_back(std::max(low,high));}
    out.push_back(count-1);return out;
}
void TablePreview(const scaps_core::OpticalTable& table,Preview& out,double scale=1) {
    const auto indices=PreviewIndices(table.size(),[&](std::size_t i){return table[i].second;});out.x.reserve(indices.size());out.y.reserve(indices.size());
    for(auto i:indices){out.x.push_back(table[i].first);out.y.push_back(table[i].second*scale);}
}
}
Preview MakePreview(const Payload& payload) {
    Preview out;auto p=Carrier(payload);if(const auto* material=std::get_if<Material>(&payload);material && material->bandgapTemperature){const auto& t=*material->bandgapTemperature;const auto temperature=material->values.T_ref_K;p.layers.front().material.Eg_eV=t.Eg0_eV-t.alpha_eV_K*temperature*temperature/(temperature+t.beta_K);}const auto& l=p.layers.front();
    if(const auto* profile=std::get_if<Profile>(&payload)){
        out.title="Profile";out.xLabel="Relative position [1]";out.yLabel=profile->target+" ["+NativeUnit("layers[0].grading."+profile->target+".left")+"]";
        if(profile->value.law==scaps_core::GradingLaw::Table){const auto& table=profile->value;Assets::Require(table.coordinate.size()==table.value.size(),"Profile coordinate/value length mismatch");for(auto i:PreviewIndices(table.coordinate.size(),[&](std::size_t j){return table.value[j];})){out.x.push_back(table.coordinate[i]);out.y.push_back(table.value[i]);}}
        else for(int i=0;i<=128;++i){const double t=i/128.;out.x.push_back(t);out.y.push_back(evaluate_position_profile(profile->value,t*l.thickness_m,l.thickness_m));}
    }else if(const auto* illumination=std::get_if<Illumination>(&payload)){
        const auto& light=illumination->value;out.title=light.enabled?"Illumination":"Illumination (disabled)";
        if(light.mode=="spectrum"){out.xLabel="Wavelength [nm]";out.yLabel="Spectral irradiance [W m^-2 nm^-1]";TablePreview(light.spectrum_nm_W_m2,out,light.intensity_multiplier);}
        else if(light.mode=="monochromatic"){out.xLabel="Wavelength [nm]";out.yLabel="Irradiance [W m^-2]";out.x={light.wavelength_nm};out.y={light.monochromatic_power_W_m2*light.intensity_multiplier};}
        else if(light.mode=="generation_file"){out.xLabel="Position [m]";out.yLabel="Generation [m^-3 s^-1]";TablePreview(light.generation_m_m3s,out,light.intensity_multiplier);}
        else {out.xLabel="Relative position [1]";out.yLabel="Generation [m^-3 s^-1]";out.x={0,1};out.y={light.uniform_generation_m3s,light.uniform_generation_m3s};}
    }else if(const auto* optical=std::get_if<Optical>(&payload)){
        out.title="Absorption";out.xLabel="Wavelength [nm]";out.yLabel="Absorption [m^-1]";
        if(!optical->absorption.table_nm_m1.empty())TablePreview(optical->absorption.table_nm_m1,out);
        else if(optical->absorption.model!="none"){
            scaps_core::Layer layer;layer.material=l.material;layer.absorption=optical->absorption;
            for(int i=0;i<=256;++i){const double wavelength=200+i*1800./256;out.x.push_back(wavelength);out.y.push_back(absorption_m1(layer,wavelength,standard_physical_constants()));}
        }else if(!optical->refractiveIndex.empty()){
            out.title="n";out.secondLabel="k";out.yLabel="Refractive index [1]";
            const auto& table=optical->refractiveIndex;auto indices=PreviewIndices(table.size(),[&](std::size_t i){return table[i][1];});auto k=PreviewIndices(table.size(),[&](std::size_t i){return table[i][2];});indices.insert(indices.end(),k.begin(),k.end());std::sort(indices.begin(),indices.end());indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
            for(auto i:indices){out.x.push_back(table[i][0]);out.y.push_back(table[i][1]);out.y2.push_back(table[i][2]);}
        }
    }else if(std::holds_alternative<Defect>(payload)){
        out.title="Bulk defect levels";out.xLabel="Energy offset [eV]";out.yLabel="Density per solver level [m^-3]";
        const auto model=make_device_from_project(p);for(const auto& level:model.layers.front().defects){out.x.push_back(level.energy_offset_eV);out.y.push_back(level.Nt_m3);}
    }else if(std::holds_alternative<Interface>(payload)){
        out.title="Interface defect levels";out.xLabel="Energy offset [eV]";out.yLabel="Density per solver level [m^-2]";
        const auto model=make_device_from_project(p);for(const auto& level:model.interface_defects){out.x.push_back(level.energy_offset_eV);out.y.push_back(level.Nt_m2);}
    }else if(const auto* contact=std::get_if<Contact>(&payload)){
        out.title="Metal level";out.xLabel="Relative position [1]";out.yLabel="Energy relative to vacuum [eV]";out.x={0,1};out.y={-contact->value.phi_m_eV,-contact->value.phi_m_eV};
    }else {
        out.title="Ec";out.secondLabel="Ev";out.xLabel="Position [um]";out.yLabel="Energy relative to vacuum [eV]";
        double x=0;const auto count=std::holds_alternative<Device>(payload)?p.layers.size():1;
        for(std::size_t i=0;i<count;++i){const auto& layer=p.layers[i];for(double position:{x,x+layer.thickness_m*1e6}){out.x.push_back(position);out.y.push_back(-layer.material.chi_eV);out.y2.push_back(-layer.material.chi_eV-layer.material.Eg_eV);}x+=layer.thickness_m*1e6;}
    }
    if(out.x.empty()){out.x={0,1};out.y={0,0};}return out;
}
}
