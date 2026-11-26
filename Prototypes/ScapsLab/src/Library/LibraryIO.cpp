#include "Library/Library.hpp"
#include "Library/PythonLibrary.hpp"
#include "Io.hpp"
#include "Core/Assets/AssetManager.hpp"
#include "Core/Platform/ProcessRunner.hpp"
#include <scaps_core/device_project_yaml.hpp>
#include <scaps_core/device_project_def.hpp>
#include <scaps_core/scaps_material.hpp>
#include <yaml-cpp/yaml.h>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace SST::ScapsLab::Library {
namespace {
using Node=YAML::Node;
template<class T> T Get(const Node& n,const char* key,T fallback={}) {return n[key]?n[key].as<T>():fallback;}
std::uint64_t Fingerprint(const std::string& text) {std::uint64_t h=14695981039346656037ull;for(unsigned char c:text){h^=c;h*=1099511628211ull;}return h;}
std::string Read(const std::filesystem::path& path) {
    std::error_code ec;const auto size=std::filesystem::file_size(path,ec);if(ec)throw Assets::Error(Assets::ErrorCode::Io,"Cannot read library: "+ec.message());
    Assets::Require(size<=50u*1024*1024,"Library file exceeds 50 MiB; split it into smaller libraries");
    std::string text,error;if(!Io::Load(path,text,error))throw Assets::Error(Assets::ErrorCode::Io,error);return text;
}
const char* ScopeName(Assets::Scope s) {switch(s){case Assets::Scope::Builtin:return "builtin";case Assets::Scope::Project:return "project";case Assets::Scope::Shared:return "shared";default:return "user";}}
Assets::Scope ReadScope(const std::string& s) {if(s=="builtin")return Assets::Scope::Builtin;if(s=="user")return Assets::Scope::User;if(s=="project")return Assets::Scope::Project;if(s=="shared")return Assets::Scope::Shared;throw Assets::Error(Assets::ErrorCode::Invalid,"Unknown library scope: "+s);}
Node ReferenceNode(const Assets::Reference& r) {Node n;n["library_id"]=r.libraryId;n["item_id"]=r.itemId;n["version"]=r.version;return n;}
Assets::Reference ReadReference(const Node& n) {return {n["library_id"].as<std::string>(),n["item_id"].as<std::string>(),n["version"].as<std::uint32_t>()};}
Node MetaNode(const Assets::Metadata& m) {
    Node n;n["id"]=m.id;n["name"]=m.name;n["description"]=m.description;n["collection"]=m.collection;n["tags"]=m.tags;n["change_note"]=m.changeNote;n["locked"]=m.locked;
    const auto& p=m.provenance;auto a=n["provenance"];a["source"]=p.source;a["citation"]=p.citation;a["doi"]=p.doi;a["locator"]=p.locator;a["author"]=p.author;a["date"]=p.date;a["run_id"]=p.runId;a["source_path"]=p.sourcePath;
    const auto& v=m.validity;auto b=n["validity"];if(v.temperatureMin_K)b["temperature_min_K"]=*v.temperatureMin_K;if(v.temperatureMax_K)b["temperature_max_K"]=*v.temperatureMax_K;if(v.compositionMin)b["composition_min"]=*v.compositionMin;if(v.compositionMax)b["composition_max"]=*v.compositionMax;
    n["parameters"]=Node(YAML::NodeType::Sequence);for(const auto& p:m.parameters){Node r;r["path"]=p.path;r["unit"]=p.unit;r["citation"]=p.citation;if(p.lower)r["lower"]=*p.lower;if(p.upper)r["upper"]=*p.upper;if(p.uncertainty)r["uncertainty"]=*p.uncertainty;n["parameters"].push_back(r);}
    if(m.basedOn)n["based_on"]=ReferenceNode(*m.basedOn);return n;
}
Assets::Metadata ReadMeta(const Node& n) {
    Assets::Metadata m;m.id=n["id"].as<std::string>();m.name=n["name"].as<std::string>();m.description=Get<std::string>(n,"description");m.collection=Get<std::string>(n,"collection");m.changeNote=Get<std::string>(n,"change_note");m.locked=Get<bool>(n,"locked");if(n["tags"])m.tags=n["tags"].as<std::vector<std::string>>();
    if(const auto a=n["provenance"]){auto& p=m.provenance;p.source=Get<std::string>(a,"source");p.citation=Get<std::string>(a,"citation");p.doi=Get<std::string>(a,"doi");p.locator=Get<std::string>(a,"locator");p.author=Get<std::string>(a,"author");p.date=Get<std::string>(a,"date");p.runId=Get<std::string>(a,"run_id");p.sourcePath=Get<std::string>(a,"source_path");}
    if(const auto v=n["validity"]){if(v["temperature_min_K"])m.validity.temperatureMin_K=v["temperature_min_K"].as<double>();if(v["temperature_max_K"])m.validity.temperatureMax_K=v["temperature_max_K"].as<double>();if(v["composition_min"])m.validity.compositionMin=v["composition_min"].as<double>();if(v["composition_max"])m.validity.compositionMax=v["composition_max"].as<double>();}
    if(n["parameters"])for(const auto& p:n["parameters"]){Assets::ParameterRange r;r.path=p["path"].as<std::string>();r.unit=p["unit"].as<std::string>();r.citation=Get<std::string>(p,"citation");if(p["lower"])r.lower=p["lower"].as<double>();if(p["upper"])r.upper=p["upper"].as<double>();if(p["uncertainty"])r.uncertainty=p["uncertainty"].as<double>();m.parameters.push_back(std::move(r));}
    if(n["based_on"])m.basedOn=ReadReference(n["based_on"]);Assets::Validate(m);return m;
}
Node RevisionNode(const Revision& revision) {
    auto n=MetaNode(revision.meta);n["version"]=revision.version;const auto& payload=*revision.payload;n["kind"]=Kind(payload);n["data"]=scaps_core::device_project_to_node(Carrier(payload));
    if(const auto* p=std::get_if<Profile>(&payload))n["profile_target"]=p->target;
    if(const auto* p=std::get_if<Interface>(&payload))if(p->referenceReviewRequired)n["interface_reference_review_required"]=true;
    if(const auto* m=std::get_if<Material>(&payload)) {
        if(m->bandgapTemperature){const auto& t=*m->bandgapTemperature;auto a=n["bandgap_temperature"];a["model"]="varshni";a["Eg0_eV"]=t.Eg0_eV;a["alpha_eV_K"]=t.alpha_eV_K;a["beta_K"]=t.beta_K;}
        if(m->effectiveMass){auto a=n["effective_mass"];a["electron_ratio"]=m->effectiveMass->electronRatio;a["hole_ratio"]=m->effectiveMass->holeRatio;}
    }
    if(const auto* c=std::get_if<Contact>(&payload))if(c->reflectance)n["reflectance"]=*c->reflectance;
    if(const auto* o=std::get_if<Optical>(&payload)){n["refractive_index"]=Node(YAML::NodeType::Sequence);for(const auto& row:o->refractiveIndex){Node a;for(double value:row)a.push_back(value);n["refractive_index"].push_back(a);}}
    return n;
}
RevisionRef ReadRevision(const Node& n,int schema,const std::filesystem::path& dir) {
    Revision r;r.meta=ReadMeta(n);r.version=Get<std::uint32_t>(n,schema==0?"revision":"version",1);const auto kind=n["kind"].as<std::string>();
    auto data=YAML::Clone(n["data"]);bool review=Get<bool>(n,"interface_reference_review_required");
    if(kind=="interface" && data["interface_defects"])for(auto d:data["interface_defects"]){const auto reference=d["energy_reference"];if(reference){const auto text=reference.as<std::string>();if(text=="relative_to_Ec" || text=="relative_to_Ev"){d["energy_reference"]=text=="relative_to_Ec"?4:3;review=true;r.meta.description+=" Interface reference uses SCAPS code "+std::string(text=="relative_to_Ec"?"4 (lower conduction edge)":"3 (higher valence edge)")+"; derive and confirm this convention before use.";}}if(Get<std::string>(d,"density_convention")=="total")d["density_convention"]="integrated_sheet";}
    auto p=scaps_core::device_project_from_node(data,dir);
    auto payload=FromCarrier(kind,p,nullptr,Get<std::string>(n,"profile_target","Na_m3"));
    if(auto* interface=std::get_if<Interface>(&payload))interface->referenceReviewRequired=review;
    if(auto* m=std::get_if<Material>(&payload)){
        if(const auto a=n["bandgap_temperature"]){Assets::Require(Get<std::string>(a,"model")=="varshni","Unsupported temperature law");m->bandgapTemperature=BandgapTemperature{a["Eg0_eV"].as<double>(),a["alpha_eV_K"].as<double>(),a["beta_K"].as<double>()};}
        if(const auto a=n["effective_mass"])m->effectiveMass=EffectiveMass{a["electron_ratio"].as<double>(),a["hole_ratio"].as<double>()};
    }
    if(auto* c=std::get_if<Contact>(&payload))if(n["reflectance"])c->reflectance=n["reflectance"].as<double>();
    if(auto* o=std::get_if<Optical>(&payload))if(n["refractive_index"])for(const auto& a:n["refractive_index"]){Assets::Require(a.IsSequence() && a.size()==3,"n,k rows require wavelength_nm,n,k");o->refractiveIndex.push_back({a[0].as<double>(),a[1].as<double>(),a[2].as<double>()});}
    r.payload=std::make_shared<const Payload>(std::move(payload));ValidateRevision(r);return std::make_shared<const Revision>(std::move(r));
}
}
Mounted Load(const std::filesystem::path& path,bool readOnly) try {
    const auto text=Read(path);const auto root=YAML::Load(text);Assets::Require(root.IsMap(),"Expected a library object");const int schema=Get<int>(root,"schema_version",-1);
    if(schema!=0 && schema!=1)throw Assets::Error(Assets::ErrorCode::Unsupported,"Unsupported library schema_version: "+std::to_string(schema));
    const auto m=root["library"];auto out=Empty(m["id"].as<std::string>(),m["name"].as<std::string>(),ReadScope(Get<std::string>(m,"scope","user")));out.readOnly=readOnly || Get<bool>(m,"read_only");out.generation=Get<std::uint64_t>(m,"generation");
    const auto entries=root[schema==0?"assets":"items"];Assets::Require(entries.IsSequence(),"Library needs an items list");Assets::Require(entries.size()<=10000,"Library exceeds 10000 items");
    for(const auto& entry:entries){Catalog::History history;if(entry["history"])for(const auto& old:entry["history"]){Assets::Require(!old["history"],"Nested asset histories are invalid");history.push_back(ReadRevision(old,schema,path.parent_path()));}history.push_back(ReadRevision(entry,schema,path.parent_path()));out.Restore(std::move(history));}
    return {std::move(out),{path,Fingerprint(text)}};
}catch(const YAML::Exception& e){throw Assets::Error(Assets::ErrorCode::Invalid,"Invalid library YAML: "+std::string(e.what()));}
Mounted LoadShippedLiterature() {
    const auto executable=Platform::CurrentExecutablePath();
    if(executable){AssetManager resources{Path{executable.Value().Native().parent_path()/"library"}};resources.RegisterAsset({"cigs-literature.sstlibrary.yaml",AssetType::Data});const auto resolved=resources.ResolvePath("cigs-literature.sstlibrary.yaml");if(resolved)return Load(resolved.Value().resolvedPath.Native(),true);}
    return Load(std::filesystem::path(SCAPSLAB_LIBRARY_DATA)/"cigs-literature.sstlibrary.yaml",true);
}
void Save(const Catalog& catalog,const std::filesystem::path& path,std::optional<std::uint64_t> expectedFingerprint) {
    if(catalog.ReadOnly())throw Assets::Error(Assets::ErrorCode::ReadOnly,"Cannot save a read-only library; derive into a writable library");
    Assets::Require(!path.empty(),"Choose a library file");Io::Note("library-save",path.string());
    std::error_code ec;if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());auto lock=path;lock+=".lock";
    if(!std::filesystem::create_directory(lock,ec))throw Assets::Error(Assets::ErrorCode::Conflict,"Library is being written by another process: "+path.filename().string());
    struct Unlock {std::filesystem::path path;~Unlock(){std::error_code ec;std::filesystem::remove(path,ec);}} unlock{lock};
    const bool exists=std::filesystem::exists(path);
    if(expectedFingerprint){if(!exists || Fingerprint(Read(path))!=*expectedFingerprint)throw Assets::Error(Assets::ErrorCode::Conflict,"Library changed on disk; reload before saving");}
    else if(exists)throw Assets::Error(Assets::ErrorCode::Conflict,"Destination already exists; load it before replacing it");
    for(const auto& [key,history]:catalog.Items())for(const auto& r:history)ValidateRevision(*r);
    std::string text;
    if(Assets::Fold(path.extension().string())==".json") {
        State state;state.libraries.emplace(catalog.id,Mounted{catalog,{}});const auto data=Python::StateDocument(state);
        nlohmann::json document={{"schema_version",1},{"library",{{"id",catalog.id},{"name",catalog.name},{"scope",ScopeName(catalog.scope)},{"read_only",catalog.readOnly},{"generation",catalog.generation}}},{"items",data.front().at("items")}};
        text=document.dump(2)+"\n";
    }else {
        Node root;root["schema_version"]=1;auto m=root["library"];m["id"]=catalog.id;m["name"]=catalog.name;m["scope"]=ScopeName(catalog.scope);m["read_only"]=catalog.readOnly;m["generation"]=catalog.generation;root["items"]=Node(YAML::NodeType::Sequence);
        for(const auto& [key,history]:catalog.Items()){auto item=RevisionNode(*history.back());if(history.size()>1){item["history"]=Node(YAML::NodeType::Sequence);for(std::size_t i=0;i+1<history.size();++i)item["history"].push_back(RevisionNode(*history[i]));}root["items"].push_back(item);}
        YAML::Emitter emitter;emitter.SetDoublePrecision(17);emitter<<root;Assets::Require(emitter.good(),"Failed to encode library YAML");text=std::string(emitter.c_str())+"\n";
    }
    Assets::Require(text.size()<=50u*1024*1024,"Library output exceeds 50 MiB; split the library");std::string error;if(!Io::Save(path,text,error))throw Assets::Error(Assets::ErrorCode::Io,error);
}
Catalog ImportScaps(const std::filesystem::path& path) {
    Io::Note("import-scaps-asset",path.string());auto out=Empty("imported","SCAPS import");auto p=scaps_core::make_default_device_project();std::string kind;
    if(Assets::Fold(path.extension().string())==".def"){p=scaps_core::import_def(path);kind="device";}
    else if(Assets::Fold(path.extension().string())==".material"){scaps_core::apply_scaps_material(scaps_core::read_scaps_material(path),p.layers.front());kind="material";}
    else throw Assets::Error(Assets::ErrorCode::Unsupported,"Select .def or .material");
    Assets::Metadata m;m.id="imported-asset";m.name=path.stem().string();m.changeNote="Imported from SCAPS";m.provenance.source="example";m.provenance.sourcePath=path.filename().string();m.provenance.citation="User-selected SCAPS file; verify physical provenance";
    auto r=out.Create(std::move(m),std::make_shared<const Payload>(FromCarrier(kind,p)));ValidateRevision(*r);return out;
}
RevisionRef ImportCsv(const std::filesystem::path& path,const std::string& kind,const std::string& target) {
    const auto text=Read(path);std::istringstream lines(text);std::string line;Assets::Require(bool(std::getline(lines,line)),"CSV is empty");if(!line.empty() && line.back()=='\r')line.pop_back();
    const char separator=line.find('\t')!=line.npos?'\t':',';const auto split=line.find(separator);Assets::Require(split!=line.npos,"CSV requires two columns with unit headers");
    const auto xHeader=line.substr(0,split),yHeader=line.substr(split+1);std::string expectedX,expectedY;
    if(kind=="optical"){expectedX="wavelength_nm";expectedY="alpha_m1";}else if(kind=="illumination"){expectedX="wavelength_nm";expectedY="spectral_irradiance_W_m2_nm";}else if(kind=="profile"){expectedX="relative_position";expectedY=target;}else throw Assets::Error(Assets::ErrorCode::Unsupported,"CSV target must be optical, illumination or profile");
    Assets::Require(xHeader==expectedX && yHeader==expectedY,"Expected CSV header: "+expectedX+separator+expectedY);
    const auto number=[](std::string_view value){while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))value.remove_prefix(1);while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))value.remove_suffix(1);double out=0;const auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),out);Assets::Require(error==std::errc{} && end==value.data()+value.size() && std::isfinite(out),"Invalid finite CSV number");return out;};
    scaps_core::OpticalTable table;while(std::getline(lines,line)){if(line.empty() || line=="\r")continue;const auto at=line.find(separator);Assets::Require(at!=line.npos && line.find(separator,at+1)==line.npos,"CSV requires exactly two columns");table.emplace_back(number(std::string_view(line).substr(0,at)),number(std::string_view(line).substr(at+1)));Assets::Require(table.size()<=1000000,"CSV has too many samples");}
    Assets::Require(table.size()>=2,"CSV needs at least two samples");Payload payload;
    if(kind=="optical"){Optical v;v.absorption.model="file";v.absorption.table_nm_m1=std::move(table);payload=std::move(v);}
    else if(kind=="illumination"){Illumination v;v.value.enabled=true;v.value.mode="spectrum";v.value.spectrum_nm_W_m2=std::move(table);payload=std::move(v);}
    else {Profile v;v.target=target;v.value.law=scaps_core::GradingLaw::Table;for(const auto& [x,y]:table){Assets::Require(x>=0 && x<=1,"Relative profile coordinate must be in [0,1]");v.value.coordinate.push_back(x);v.value.value.push_back(y);}payload=std::move(v);}
    Assets::Metadata m;m.id="csv-asset";m.name=path.stem().string();m.changeNote="CSV import";m.provenance.source="measurement";m.provenance.sourcePath=path.filename().string();m.provenance.citation="User-selected table; verify measurement provenance";auto out=std::make_shared<const Revision>(Revision{std::move(m),1,std::make_shared<const Payload>(std::move(payload))});ValidateRevision(*out);return out;
}
void ExportCsv(const Payload& payload,const std::filesystem::path& path) {
    ValidatePayload(payload);
    std::ostringstream stream;stream<<std::setprecision(17);
    if(const auto* o=std::get_if<Optical>(&payload)){stream<<"wavelength_nm,alpha_m1\n";for(const auto& [x,y]:o->absorption.table_nm_m1)stream<<x<<','<<y<<'\n';}
    else if(const auto* l=std::get_if<Illumination>(&payload)){stream<<"wavelength_nm,spectral_irradiance_W_m2_nm\n";for(const auto& [x,y]:l->value.spectrum_nm_W_m2)stream<<x<<','<<y<<'\n';}
    else if(const auto* p=std::get_if<Profile>(&payload)){stream<<"relative_position,"<<p->target<<'\n';if(p->value.law==scaps_core::GradingLaw::Table){for(std::size_t i=0;i<p->value.coordinate.size();++i)stream<<p->value.coordinate[i]<<','<<p->value.value[i]<<'\n';}else{const auto preview=MakePreview(payload);for(std::size_t i=0;i<preview.x.size();++i)stream<<preview.x[i]<<','<<preview.y[i]<<'\n';}}
    else throw Assets::Error(Assets::ErrorCode::Unsupported,"CSV export supports optical, spectrum and profile tables");
    std::string error;if(!Io::Save(path,stream.str(),error))throw Assets::Error(Assets::ErrorCode::Io,error);
}
void ExportScaps(const Payload& payload,const std::filesystem::path& path) {
    ValidatePayload(payload);Io::Note("export-scaps-asset",path.string());const auto p=Carrier(payload);
    if(Assets::Fold(path.extension().string())==".def" && std::holds_alternative<Device>(payload))scaps_core::export_def(p,path);
    else if(Assets::Fold(path.extension().string())==".material" && (std::holds_alternative<Material>(payload) || std::holds_alternative<Layer>(payload)))scaps_core::write_scaps_material(scaps_core::make_scaps_material(p.layers.front()),path);
    else throw Assets::Error(Assets::ErrorCode::Unsupported,"Export a device as .def or a material/layer as .material");
}
std::vector<std::filesystem::path> LiteratureFiles(const std::filesystem::path& dir) {
    Io::Note("literature-scan",dir.string());std::vector<std::filesystem::path> out;std::error_code ec;if(!std::filesystem::exists(dir,ec))return out;
    for(const auto& entry:std::filesystem::directory_iterator(dir)){const auto ext=Assets::Fold(entry.path().extension().string());if(entry.is_regular_file() && (ext==".yaml" || ext==".yml" || ext==".json"))out.push_back(entry.path());}std::sort(out.begin(),out.end());return out;
}
}
