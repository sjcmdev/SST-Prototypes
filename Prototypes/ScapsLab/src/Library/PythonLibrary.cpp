#include "Library/PythonLibrary.hpp"

namespace SST::ScapsLab::Library::Python {
using Json=nlohmann::json;
namespace {
Assets::Metadata ReadMeta(const Json& j) {
    Assets::Metadata m;m.id=j.at("id").get<std::string>();m.name=j.at("name").get<std::string>();m.description=j.value("description",std::string{});m.collection=j.value("collection",std::string{});m.changeNote=j.value("change_note",std::string{});m.tags=j.value("tags",std::vector<std::string>{});m.locked=j.value("locked",false);
    const auto p=j.value("provenance",Json::object());m.provenance={p.value("source",std::string{}),p.value("citation",std::string{}),p.value("doi",std::string{}),p.value("locator",std::string{}),p.value("author",std::string{}),p.value("date",std::string{}),p.value("run_id",std::string{}),p.value("source_path",std::string{})};
    const auto v=j.value("validity",Json::object());const auto optional=[](const Json& j,const char* key)->std::optional<double>{if(j.contains(key) && !j.at(key).is_null())return j.at(key).get<double>();return {};};
    m.validity={optional(v,"temperature_min_K"),optional(v,"temperature_max_K"),optional(v,"composition_min"),optional(v,"composition_max")};
    for(const auto& p:j.value("parameters",Json::array()))m.parameters.push_back({p.at("path").get<std::string>(),p.at("unit").get<std::string>(),p.value("citation",std::string{}),optional(p,"lower"),optional(p,"upper"),optional(p,"uncertainty")});
    if(j.contains("based_on")){const auto& r=j.at("based_on");m.basedOn=Assets::Reference{r.at("library_id").get<std::string>(),r.at("item_id").get<std::string>(),r.at("version").get<std::uint32_t>()};}Assets::Validate(m);return m;
}
Json Meta(const Assets::Metadata& m) {
    const auto& p=m.provenance;Json out={{"id",m.id},{"name",m.name},{"description",m.description},{"collection",m.collection},{"tags",m.tags},{"change_note",m.changeNote},{"locked",m.locked},{"provenance",{{"source",p.source},{"citation",p.citation},{"doi",p.doi},{"locator",p.locator},{"author",p.author},{"date",p.date},{"run_id",p.runId},{"source_path",p.sourcePath}}},{"validity",Json::object()},{"parameters",Json::array()}};
    auto& v=out["validity"];if(m.validity.temperatureMin_K)v["temperature_min_K"]=*m.validity.temperatureMin_K;if(m.validity.temperatureMax_K)v["temperature_max_K"]=*m.validity.temperatureMax_K;if(m.validity.compositionMin)v["composition_min"]=*m.validity.compositionMin;if(m.validity.compositionMax)v["composition_max"]=*m.validity.compositionMax;
    for(const auto& p:m.parameters){Json r={{"path",p.path},{"unit",p.unit},{"citation",p.citation}};if(p.lower)r["lower"]=*p.lower;if(p.upper)r["upper"]=*p.upper;if(p.uncertainty)r["uncertainty"]=*p.uncertainty;out["parameters"].push_back(std::move(r));}
    if(m.basedOn)out["based_on"]={{"library_id",m.basedOn->libraryId},{"item_id",m.basedOn->itemId},{"version",m.basedOn->version}};return out;
}
Json Item(const Revision& r,bool data) {
    auto j=Meta(r.meta);j["version"]=r.version;j["kind"]=Kind(*r.payload);
    if(data){j["data"]=DraftDocument(*DraftFromProject(Carrier(*r.payload)));if(const auto* p=std::get_if<Profile>(r.payload.get()))j["profile_target"]=p->target;
        if(const auto* p=std::get_if<Interface>(r.payload.get()))if(p->referenceReviewRequired)j["interface_reference_review_required"]=true;
        if(const auto* m=std::get_if<Material>(r.payload.get())){if(m->bandgapTemperature){const auto& t=*m->bandgapTemperature;j["bandgap_temperature"]={{"model","varshni"},{"Eg0_eV",t.Eg0_eV},{"alpha_eV_K",t.alpha_eV_K},{"beta_K",t.beta_K}};}if(m->effectiveMass)j["effective_mass"]={{"electron_ratio",m->effectiveMass->electronRatio},{"hole_ratio",m->effectiveMass->holeRatio}};}
        if(const auto* c=std::get_if<Contact>(r.payload.get()))if(c->reflectance)j["reflectance"]=*c->reflectance;
        if(const auto* o=std::get_if<Optical>(r.payload.get()))j["refractive_index"]=o->refractiveIndex;
    }return j;
}
Payload ReadPayload(const Json& j) {
    const auto kind=j.at("kind").get<std::string>();const auto draft=DraftFromDocument(j.at("data"));auto p=FromCarrier(kind,draft->project,nullptr,j.value("profile_target",std::string("Na_m3")));
    if(auto* m=std::get_if<Material>(&p)){if(j.contains("bandgap_temperature")){const auto& t=j.at("bandgap_temperature");Assets::Require(t.at("model")=="varshni","Unsupported temperature law");m->bandgapTemperature=BandgapTemperature{t.at("Eg0_eV").get<double>(),t.at("alpha_eV_K").get<double>(),t.at("beta_K").get<double>()};}if(j.contains("effective_mass")){const auto& v=j.at("effective_mass");m->effectiveMass=EffectiveMass{v.at("electron_ratio").get<double>(),v.at("hole_ratio").get<double>()};}}
    if(auto* c=std::get_if<Contact>(&p))if(j.contains("reflectance"))c->reflectance=j.at("reflectance").get<double>();
    if(auto* interface=std::get_if<Interface>(&p))interface->referenceReviewRequired=j.value("interface_reference_review_required",false);
    if(auto* o=std::get_if<Optical>(&p))if(j.contains("refractive_index"))o->refractiveIndex=j.at("refractive_index").get<std::vector<std::array<double,3>>>();ValidatePayload(p);return p;
}
}
Request ReadRequest(const Json& j) {
    Request r;const auto action=j.value("action",std::string("inspect"));bool found=false;
    for(const auto& [name,value]:std::initializer_list<std::pair<const char*,Action>>{{"inspect",Action::Inspect},{"create_library",Action::CreateLibrary},{"mount",Action::Mount},{"preview_import",Action::PreviewImport},{"commit_import",Action::CommitImport},{"create",Action::Create},{"revise",Action::Revise},{"derive",Action::Derive},{"lock",Action::SetLocked},{"save",Action::Save},{"export",Action::Export},{"apply",Action::Apply},{"refresh",Action::Refresh},{"where_used",Action::WhereUsed},{"undo",Action::Undo},{"redo",Action::Redo}})if(action==name){r.action=value;found=true;}
    Assets::Require(found,"Unknown library action: "+action);r.libraryId=j.value("library_id",std::string("user"));r.itemId=j.value("item_id",std::string{});r.sourceLibraryId=j.value("source_library_id",std::string{});r.newId=j.value("new_id",std::string{});r.path=j.value("path",std::string{});r.kind=j.value("kind",std::string("material"));r.profileTarget=j.value("profile_target",std::string("Na_m3"));r.version=j.value("version",0u);r.expectedVersion=j.value("expected_version",0u);r.locked=j.value("locked",false);r.readOnly=j.value("read_only",false);
    const auto policy=j.value("policy",std::string("copy"));Assets::Require(policy=="copy" || policy=="linked","Use policy must be copy or linked");r.policy=policy=="linked"?Assets::UsePolicy::Linked:Assets::UsePolicy::Copy;
    const auto scope=j.value("scope",std::string("user"));Assets::Require(scope=="user" || scope=="project" || scope=="shared","Use library scope user, project or shared");r.scope=scope=="project"?Assets::Scope::Project:scope=="shared"?Assets::Scope::Shared:Assets::Scope::User;
    if(j.contains("metadata"))r.metadata=ReadMeta(j.at("metadata"));else if(r.action==Action::CreateLibrary)r.metadata.name=j.at("name").get<std::string>();
    if(r.action==Action::Create || r.action==Action::Revise)r.payload=std::make_shared<const Payload>(ReadPayload(j));
    if(j.contains("target")){const auto& t=j.at("target");r.target={t.value("layer_id",std::string{}),t.value("interface_after",-1),t.value("right_contact",false),t.value("append_layer",false),t.value("device_id",std::string{}),t.value("defect_id",std::string{})};if(t.contains("composition"))r.target.composition=t.at("composition").get<double>();}return r;
}
Json Reply(const Session& session,bool includeData) {
    Json out={{"ok",session.errorCode.empty()},{"error",nullptr},{"message",session.message},{"libraries",Json::array()},{"where_used",Json::array()},{"warnings",session.state->warnings}};
    if(!session.errorCode.empty())out["error"]={{"code",session.errorCode},{"message",session.message}};
    for(const auto& [id,m]:session.state->libraries){Json c={{"id",id},{"name",m.catalog.name},{"read_only",m.catalog.ReadOnly()},{"generation",m.catalog.generation},{"dirty",session.dirty.contains(id)},{"items",Json::array()}};for(const auto& [key,history]:m.catalog.Items())c["items"].push_back(Item(*history.back(),false));out["libraries"].push_back(std::move(c));}
    if(session.selected){out["selected"]=Item(*session.selected,includeData);out["history"]=Json::array();for(const auto& [id,m]:session.state->libraries)if(const auto it=m.catalog.Items().find(session.selected->meta.id);it!=m.catalog.Items().end() && std::any_of(it->second.begin(),it->second.end(),[&](const auto& r){return r==session.selected;}))for(const auto& r:it->second)out["history"].push_back(Item(*r,false));}
    if(session.preview){out["preview"]=Json::array();for(const auto& [id,history]:session.preview->Items())out["preview"].push_back(Item(*history.back(),includeData));}
    for(const auto& u:session.whereUsed)out["where_used"].push_back({{"project",u.project},{"device_id",u.device},{"name",u.name},{"target",u.use.target},{"version",u.use.source.version},{"policy",u.use.policy==Assets::UsePolicy::Linked?"linked":"copy"}});return out;
}
Json StateDocument(const State& state) {
    Json out=Json::array();for(const auto& [id,m]:state.libraries){if(m.catalog.scope==Assets::Scope::Builtin)continue;Json c={{"id",id},{"name",m.catalog.name},{"scope",int(m.catalog.scope)},{"read_only",m.catalog.readOnly},{"generation",m.catalog.generation},{"items",Json::array()}};if(!m.source.path.empty()){c["source_path"]=m.source.path.generic_string();c["source_fingerprint"]=m.source.fingerprint;}for(const auto& [key,history]:m.catalog.Items()){Json item=Item(*history.back(),true);item["history"]=Json::array();for(std::size_t i=0;i+1<history.size();++i)item["history"].push_back(Item(*history[i],true));c["items"].push_back(std::move(item));}out.push_back(std::move(c));}return out;
}
std::shared_ptr<const State> ReadState(const Json& j) {
    auto out=std::make_shared<State>(*Builtins());std::set<std::string> ids;
    for(const auto& c:j){const auto id=c.at("id").get<std::string>();Assets::Require(Assets::ValidId(id) && ids.insert(id).second && id!="sst-examples","Invalid or duplicate library ID");const auto scope=c.at("scope").get<int>();Assets::Require(scope>=int(Assets::Scope::User) && scope<=int(Assets::Scope::Shared),"Invalid stored library scope");auto catalog=Empty(id,c.at("name").get<std::string>(),Assets::Scope(scope));catalog.readOnly=c.value("read_only",false);catalog.generation=c.value("generation",0ull);
        for(const auto& item:c.at("items")){Catalog::History history;const auto read=[&](const Json& r){auto payload=std::make_shared<const Payload>(ReadPayload(r));auto rev=std::make_shared<const Revision>(Revision{ReadMeta(r),r.at("version").get<std::uint32_t>(),std::move(payload)});ValidateRevision(*rev);history.push_back(std::move(rev));};for(const auto& r:item.value("history",Json::array()))read(r);read(item);catalog.Restore(std::move(history));}out->libraries.insert_or_assign(id,Mounted{std::move(catalog),{c.value("source_path",std::string{}),c.value("source_fingerprint",std::uint64_t(0))}});
    }return out;
}
Json BrowserDocument(const Browser& b){return {{"library",b.library},{"item",b.item},{"search",b.search},{"collection",b.collection},{"tag",b.tag},{"compare_item",b.compareItem},{"favorites",b.favorites},{"grid",b.grid},{"favorites_only",b.favoritesOnly}};}
Browser ReadBrowser(const Json& j){Browser b;b.library=j.value("library",b.library);b.item=j.value("item",b.item);b.search=j.value("search",b.search);b.collection=j.value("collection",b.collection);b.tag=j.value("tag",b.tag);b.compareItem=j.value("compare_item",b.compareItem);b.favorites=j.value("favorites",b.favorites);b.grid=j.value("grid",b.grid);b.favoritesOnly=j.value("favorites_only",b.favoritesOnly);return b;}
Json DraftsDocument(const DraftMap& drafts) {
    Json out=Json::object();for(const auto& [key,draft]:drafts){Revision r{draft.metadata,draft.sourceVersion,draft.payload};ValidateRevision(r);out[key]=Item(r,true);}return out;
}
std::shared_ptr<const DraftMap> ReadDrafts(const Json& j,const State& state) {
    Assets::Require(j.is_object(),"Expected asset editor drafts object");auto drafts=std::make_shared<DraftMap>();
    for(const auto& [key,value]:j.items()){const auto separator=key.find('/');Assets::Require(separator!=key.npos && Assets::ValidId(key.substr(0,separator)) && Assets::ValidId(key.substr(separator+1)),"Invalid asset editor draft identity");auto meta=ReadMeta(value);Assets::Require(meta.id==key.substr(separator+1),"Asset draft ID mismatch");auto payload=std::make_shared<const Payload>(ReadPayload(value));const auto version=value.at("version").get<std::uint32_t>();ValidateRevision({meta,version,payload});const auto library=state.libraries.find(key.substr(0,separator));Assets::Require(library!=state.libraries.end(),"Asset draft source library is missing");const auto source=library->second.catalog.Find(meta.id,version);Assets::Require(source->payload->index()==payload->index(),"Asset draft kind differs from its source");drafts->emplace(key,ItemDraft{std::move(meta),std::move(payload),version});}return drafts;
}
}
