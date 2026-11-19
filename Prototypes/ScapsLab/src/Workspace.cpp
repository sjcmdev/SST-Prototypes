#include "Workflow.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace SST::ScapsLab {
std::filesystem::path ExamplesDirectory() {
#ifdef _WIN32
    wchar_t path[32768];const auto length=GetModuleFileNameW(nullptr,path,32768);
    if(length>0 && length<32768) {const auto local=std::filesystem::path(path).parent_path()/"examples";if(std::filesystem::exists(local)) return local;}
#endif
    return SCAPSLAB_EXAMPLES;
}
std::string DocumentHash(const Json &document) {
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:document.dump()) {hash^=c;hash*=1099511628211ull;}
    std::ostringstream text;text<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;return text.str();
}
Json PackResults(const Json &value) {
    if(!value.is_object()) return value;
    auto out=value;
    for(auto it=out.begin();it!=out.end();++it) {
        if(it.key()=="document" || it.key()=="project_snapshot" || it.key()=="ui") continue;
        if(it.key()=="points" && it->is_array() && !it->empty()) {
            const auto &first=it->front();bool rectangular=first.is_object();
            for(const auto &point:*it) {rectangular &= point.is_object() && point.size()==first.size();for(auto field=first.begin();field!=first.end();++field) rectangular &= point.contains(field.key());}
            if(!rectangular) continue;
            Json columns=Json::object();
            for(const auto &point:*it) for(auto field=point.begin();field!=point.end();++field) columns[field.key()].push_back(field.value());
            *it={{"columns",columns},{"count",it->size()}};
        } else if(it->is_object()) *it=PackResults(*it);
        else if(it->is_array()) for(auto &item:*it) item=PackResults(item);
    }
    return out;
}
Json UnpackResults(const Json &value) {
    if(!value.is_object()) return value;
    auto out=value;
    for(auto it=out.begin();it!=out.end();++it) {
        if(it.key()=="document" || it.key()=="project_snapshot" || it.key()=="ui") continue;
        if(it.key()=="points" && it->is_object() && it->contains("columns")) {
            const auto count=it->at("count").get<std::size_t>();if(count>1000000) throw std::invalid_argument("Too many result points");
            auto points=Json::array();for(std::size_t i=0;i<count;++i) points.push_back(Json::object());
            for(auto field=it->at("columns").begin();field!=it->at("columns").end();++field) {
                if(!field->is_array() || field->size()!=count) throw std::invalid_argument("Invalid result column length");
                for(std::size_t i=0;i<count;++i) points[i][field.key()]=(*field)[i];
            }
            *it=std::move(points);
        } else if(it->is_object()) *it=UnpackResults(*it);
        else if(it->is_array()) for(auto &item:*it) item=UnpackResults(item);
    }
    return out;
}
namespace {
bool Compact(const Json &v) {
    if(v.is_object()) return false;
    if(v.is_array()) for(const auto &child:v) if(!Compact(child)) return false;
    return true;
}
std::string Pretty(const Json &v,int indent) {
    if(v.is_primitive() || v.empty() || Compact(v)) return v.dump();
    const bool object=v.is_object();std::string out=object?"{\n":"[\n";bool first=true;
    for(auto it=v.begin();it!=v.end();++it) {
        if(!first) out+=",\n";first=false;out+=std::string(indent+2,' ');
        if(object) out+=Json(it.key()).dump()+": ";out+=Pretty(it.value(),indent+2);
    }
    return out+"\n"+std::string(indent,' ')+(object?"}":"]");
}
}
std::string WorkspaceText(const Json &workspace) {return Pretty(workspace,0)+"\n";}
DeviceState &Workflow::Device(const std::string &id) {
    for(const auto &d:project.devices) if(d->id==id) return *d;
    throw std::invalid_argument("Unknown device_id: "+id);
}
DeviceState &Workflow::Current() {return Device(project.active_device);}
Json Workflow::Workspace() const {
    Json devices=Json::array();
    for(const auto &d:project.devices) devices.push_back({{"id",d->id},{"name",d->name},{"colour",d->colour},{"description",d->description},
        {"document",d->document},{"result",d->result},{"band_result",d->bandResult},{"ac_results",d->acResults},{"status",d->status}});
    return {{"schema_version",project.schema_version},{"name",project.name},{"devices",devices},{"active_device",project.active_device},{"compare_set",project.compare_set},{"ui",project.ui}};
}
void Workflow::SetWorkspace(const Json &value) {
    if(value.at("schema_version")!=1) throw std::invalid_argument("Unsupported ScapsLab schema_version");
    const auto &devices=value.at("devices");if(!devices.is_array() || devices.empty() || devices.size()>1000) throw std::invalid_argument("Expected 1..1000 devices");
    Project checked;checked.name=value.at("name").get<std::string>();if(checked.name.empty()) throw std::invalid_argument("Project name is empty");
    checked.active_device=value.at("active_device").get<std::string>();checked.compare_set=value.at("compare_set").get<std::set<std::string>>();checked.ui=value.at("ui");
    if(!checked.ui.is_object()) throw std::invalid_argument("ui must be an object");
    std::set<std::string> ids;
    for(const auto &item:devices) {
        const auto id=item.at("id").get<std::string>();if(id.empty() || !ids.insert(id).second) throw std::invalid_argument("Invalid or duplicate device_id");
        (void)Model(item.at("document"));
        if(item.at("name").get<std::string>().empty()) throw std::invalid_argument("Device name is empty");
        const auto &colour=item.at("colour");if(!colour.is_array() || colour.size()!=4) throw std::invalid_argument("colour must contain four RGBA values");
        for(const auto &c:colour) if(!c.is_number() || !std::isfinite(c.get<double>()) || c.get<double>()<0 || c.get<double>()>1) throw std::invalid_argument("Invalid RGBA colour");
        for(const auto *key:{"result","band_result","ac_results"}) if(item.contains(key) && !item.at(key).is_object()) throw std::invalid_argument("Results must be objects");
        auto validate=[&](const Json &r) {
            if(r.empty()) return;
            if(!r.is_object() || !r.contains("provenance") || r.at("provenance").at("device_hash")!=DocumentHash(item.at("document"))) throw std::invalid_argument("Result device hash does not match document");
            if(!r.at("provenance").at("solver_version").is_string() || !r.at("provenance").at("time_unix_ms").is_number_integer()) throw std::invalid_argument("Invalid result provenance");
            if(!r.at("points").is_array() || r.at("points").empty()) throw std::invalid_argument("Invalid result points");
            const bool ac=r.contains("kind");
            if(!r.at("mesh_x_m").is_array() || (!ac && (!r.at("contact_phi_eV").is_array() || r.at("contact_phi_eV").size()!=2))) throw std::invalid_argument("Invalid result mesh/contacts");
            for(const auto &point:r.at("points")) {
                if(!point.is_object() || !point.at("status").is_string() || !point.at("valid").is_boolean() || !point.at("voltage_V").is_number()) throw std::invalid_argument("Invalid result point");
                if(ac) {for(const auto *key:{"frequency_Hz","temperature_K","capacitance_F","conductance_S","G_over_omega_F","inverse_C2_F_minus2","conservation_relative"}) if(!point.at(key).is_number() && !point.at(key).is_null()) throw std::invalid_argument("Invalid AC result value");}
                else {if(!point.at("J_A_m2").is_number() && !point.at("J_A_m2").is_null()) throw std::invalid_argument("Invalid current value");for(const auto *key:{"x_m","Ec_eV","Ev_eV","EFn_eV","EFp_eV","n_m3","p_m3"}) if(!point.at(key).is_array()) throw std::invalid_argument("Invalid profile array");}
            }
        };
        validate(item.value("result",Json::object()));validate(item.value("band_result",Json::object()));
        for(const auto &r:item.value("ac_results",Json::object())) validate(r);
    }
    if(!ids.contains(checked.active_device)) throw std::invalid_argument("Unknown active_device");
    for(const auto &id:checked.compare_set) if(!ids.contains(id)) throw std::invalid_argument("Unknown compare device_id");
    // Validation above is complete before touching live devices or their independent history.
    for(const auto &item:devices) {
        const auto id=item.at("id").get<std::string>();Ref<DeviceState> state;
        for(const auto &existing:project.devices) if(existing->id==id) state=existing;
        if(!state) state=CreateRef<DeviceState>();
        state->id=id;state->name=item.at("name");state->colour=item.at("colour");state->description=item.value("description",std::string{});
        state->document=item.at("document");state->result=item.value("result",Json::object());state->bandResult=item.value("band_result",Json::object());state->acResults=item.value("ac_results",Json::object());state->status=item.value("status",std::string("no results"));
        checked.devices.push_back(state);
    }
    project=std::move(checked);
}
void Workflow::RefreshDirty() {for(auto &d:project.devices) d->dirty=d->metadataDirty || !d->undo.IsClean();dirty=projectDirty || std::any_of(project.devices.begin(),project.devices.end(),[](const auto &d){return d->dirty;});}
Json Workflow::Snapshot() const {
    const auto &d=*std::find_if(project.devices.begin(),project.devices.end(),[&](const auto &d){return d->id==project.active_device;});
    Json list=Json::array();for(const auto &item:project.devices) list.push_back({{"device_id",item->id},{"name",item->name},{"description",item->description},{"colour",item->colour},{"status",item->status},{"dirty",item->dirty},{"compare",project.compare_set.contains(item->id)}});
    return {{"ok",true},{"document",d->document},{"result",d->result},{"band_result",d->bandResult},{"ac_results",d->acResults},
        {"devices",list},{"active_device",project.active_device},{"project",Workspace()},{"dirty",dirty},{"auto_recalculate",autoRecalculate},{"auto_recalculate_band",autoRecalculateBand},{"error",nullptr}};
}
}
