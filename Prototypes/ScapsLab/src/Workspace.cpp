#include "Workflow.hpp"
#include <algorithm>
#include <atomic>
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
void TrimHistory(Json &history,int retention) {
    int automatic=0;
    for(const auto &run:history) if(!run.at("pinned").get<bool>()) ++automatic;
    for(auto it=history.begin();it!=history.end() && automatic>retention;) {
        if(!it->at("pinned").get<bool>()) {it=history.erase(it);--automatic;} else ++it;
    }
}
std::string RunLabel(const Json &run) {
    if(!run.value("name",std::string{}).empty()) return run.at("name");
    std::string label;
    for(const auto &change:run.value("diff",Json::array())) {
        if(!change.is_object() || !change.value("parameter_id",Json{}).is_string() || !change.value("unit",Json{}).is_string()
            || !change.value("before",Json{}).is_number() || !change.value("after",Json{}).is_number()) return run.at("label");
        if(!label.empty())label+=", ";
        label+=ParameterLabel(change.at("parameter_id"),change.at("unit"),change.at("after"),change.at("before"));
    }
    return label.empty()?run.at("label").get<std::string>():label;
}
std::string DiffLabel(const Json &previous,const Json &document) {
    std::string label;
    for(const auto &parameter:BatchParameters(document)) {
        const Json::json_pointer pointer(parameter.at("pointer").get<std::string>());
        if(previous.contains(pointer) && previous.at(pointer)!=parameter.at("value")) {if(!label.empty()) label+=", ";label+=ParameterLabel(parameter.at("path"),parameter.at("unit"),parameter.at("value"),previous.at(pointer));}
    }
    return label;
}
void StoreRun(Json &history,const std::string &kind,const Json &result,const Json &document,const Json &request,int retention,const std::string &group) {
    static std::atomic_uint64_t sequence{0};
    const auto id="run-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
    const auto previous=history.empty()?document:history.back().at("document");
    auto changes=Json::array();std::string label;
    for(const auto &parameter:BatchParameters(document)) {
        const Json::json_pointer pointer(parameter.at("pointer").get<std::string>());
        if(previous.contains(pointer) && previous.at(pointer)!=parameter.at("value")) {
            changes.push_back({{"parameter_id",parameter.at("id")},{"unit",parameter.at("unit")},{"before",previous.at(pointer)},{"after",parameter.at("value")}});
            if(!label.empty()) label+=", ";label+=ParameterLabel(parameter.at("path"),parameter.at("unit"),parameter.at("value"),previous.at(pointer));
        }
    }
    if(label.empty()) label=history.empty()?"Initial run":previous==document?"Same parameters":"Model changed: "+Json::diff(previous,document).front().at("path").get<std::string>();
    const double t=double(sequence%12)/12.;
    Json colour=Json::array({.5+.45*std::cos(6.283*t),.5+.45*std::cos(6.283*(t-.333)),.5+.45*std::cos(6.283*(t+.333)),1.});
    history.push_back({{"run_id",id},{"kind",kind},{"model_hash",DocumentHash(document)},{"document",document},{"diff",changes},{"model_diff",Json::diff(previous,document)},{"label",label},{"result",result},{"request",request},{"time_unix_ms",result.at("provenance").at("time_unix_ms")},{"elapsed_s",result.value("elapsed_s",result.value("compute_time_ms",0.)/1000.)},{"provenance",result.at("provenance")},{"pinned",false},{"name",""},{"visible",true},{"colour",colour},{"group_id",group}});
    TrimHistory(history,retention);
}
void CompactHistory(Json &workspace) {
    // shortcut: profiles over 64 KiB stay in memory only; move them to the HDF5 sidecar in slview1.
    for(auto &device:workspace["devices"]) for(auto &run:device["history"]) {
        auto &result=run["result"];
        if(result.dump().size()<=65536 || !result.contains("points")) continue;
        for(auto &point:result["points"]) for(auto field=point.begin();field!=point.end();) {
            if(field->is_array() || field->is_object()) field=point.erase(field);else ++field;
        }
        result.erase("mesh_x_m");run["profiles_stored"]=false;
    }
}
DeviceState &Workflow::Device(const std::string &id) {
    for(const auto &d:project.devices) if(d->id==id) return *d;
    throw std::invalid_argument("Unknown device_id: "+id);
}
DeviceState &Workflow::Current() {return Device(project.active_device);}
Json Workflow::Workspace() const {
    Json devices=Json::array();
    for(const auto &d:project.devices) devices.push_back({{"id",d->id},{"name",d->name},{"colour",d->colour},{"description",d->description},
        {"history",d->history},{"selected_run",d->selectedRun},{"selection",{{"layer",d->selectedLayer},{"interface",d->selectedInterface},{"bias",d->selectedBias},{"member",d->selectedMember},{"voltage_V",d->biasVoltage_V}}},{"actions",d->actions},{"document",d->document},{"result",d->result},{"band_result",d->bandResult},{"ac_results",d->acResults},{"batch_result",d->batchResult},{"batch_spec",d->batchSpec},{"recorder_columns",d->recorderColumns},{"status",d->status}});
    return {{"schema_version",project.schema_version},{"name",project.name},{"devices",devices},{"active_device",project.active_device},{"compare_set",project.compare_set},{"ui",project.ui}};
}
void Workflow::SetWorkspace(const Json &value) {
    if(value.value("schema_version",0)<0 || value.value("schema_version",0)>3) throw std::invalid_argument("Unsupported ScapsLab schema_version");
    const auto &devices=value.at("devices");if(!devices.is_array() || devices.empty() || devices.size()>1000) throw std::invalid_argument("Expected 1..1000 devices");
    Project checked;checked.name=value.at("name").get<std::string>();if(checked.name.empty()) throw std::invalid_argument("Project name is empty");
    checked.active_device=value.at("active_device").get<std::string>();checked.compare_set=value.at("compare_set").get<std::set<std::string>>();checked.ui=value.value("ui",Json::object());
    if(!checked.ui.is_object()) throw std::invalid_argument("ui must be an object");
    if(checked.ui.contains("history_retention") && !checked.ui.at("history_retention").is_number_integer())throw std::invalid_argument("history_retention must be an integer");
    if(checked.ui.contains("panels")) {if(!checked.ui.at("panels").is_object())throw std::invalid_argument("panels must be an object");for(const auto &visible:checked.ui.at("panels"))if(!visible.is_boolean())throw std::invalid_argument("panel visibility must be boolean");}
    if(checked.ui.contains("plots")) {
        if(!checked.ui.at("plots").is_object())throw std::invalid_argument("plots must be an object");
        for(const auto &limits:checked.ui.at("plots")) {
            if(!limits.is_array() || (limits.size()!=4 && limits.size()!=6))throw std::invalid_argument("plot limits require xmin, xmax, ymin, ymax and optional y2min, y2max");
            for(const auto &v:limits)if(!v.is_number() || !std::isfinite(v.get<double>()))throw std::invalid_argument("plot limits must be finite");
            if(limits[0]>=limits[1] || limits[2]>=limits[3] || (limits.size()==6 && limits[4]>=limits[5]))throw std::invalid_argument("plot limits must be ordered");
        }
    }
    for(const auto *key:{"optical_tab","gr_tab","recorder_plot"})if(checked.ui.contains(key) && (!checked.ui.at(key).is_number_integer() || checked.ui.at(key).get<int>()<0 || checked.ui.at(key).get<int>()>10000))throw std::invalid_argument("Invalid selected tab/plot");
    for(const auto *key:{"gr_follow","iv_absolute_log"})if(checked.ui.contains(key) && !checked.ui.at(key).is_boolean())throw std::invalid_argument("Invalid plot toggle");
    if(checked.ui.contains("gr_hidden")){if(!checked.ui.at("gr_hidden").is_array())throw std::invalid_argument("gr_hidden must be an array");for(const auto &id:checked.ui.at("gr_hidden"))if(!id.is_string())throw std::invalid_argument("Invalid mechanism ID");}
    if(checked.ui.contains("gr_bias") && (!checked.ui.at("gr_bias").is_object() || !checked.ui.at("gr_bias").at("voltage_V").is_number() || !std::isfinite(checked.ui.at("gr_bias").at("voltage_V").get<double>())))throw std::invalid_argument("Invalid G/R voltage_V");
    const auto retention=checked.ui.value("history_retention",5);
    if(retention<1 || retention>10000) throw std::invalid_argument("history_retention must be 1..10000");
    std::set<std::string> ids;
    for(const auto &item:devices) {
        const auto id=item.at("id").get<std::string>();if(id.empty() || !ids.insert(id).second) throw std::invalid_argument("Invalid or duplicate device_id");
        (void)Model(item.at("document"));
        if(item.contains("selection")) {
            const auto &selection=item.at("selection");if(!selection.is_object())throw std::invalid_argument("selection must be an object");
            for(const auto *key:{"layer","interface","bias","member"})if(selection.contains(key) && (!selection.at(key).is_number_integer() || selection.at(key).get<int>()<(std::string(key)=="interface"?-1:0)))throw std::invalid_argument("Invalid selection index");
            if(selection.contains("voltage_V") && (!selection.at("voltage_V").is_number() || !std::isfinite(selection.at("voltage_V").get<double>())))throw std::invalid_argument("Invalid selected voltage_V");
        }
        const auto history=item.value("history",Json::array());
        if(!history.is_array()) throw std::invalid_argument("history must be an array");
        std::set<std::string> runs;
        for(const auto &run:history) {
            if(run.contains("profiles_stored") && !run.at("profiles_stored").is_boolean())throw std::invalid_argument("profiles_stored must be boolean");
            const auto kind=run.at("kind").get<std::string>();
            if(kind!="iv" && kind!="band" && kind!="cv" && kind!="cf" && kind!="qe" && kind!="batch")throw std::invalid_argument("Invalid history kind");
            if(kind!="batch")(void)Model(run.at("document"));
            else if(!run.at("document").is_object() || !run.at("document").at("layers").is_array() || !run.at("document").at("temperature_K").is_number())throw std::invalid_argument("Invalid batch history model structure");
            if(run.at("result").at("provenance")!=run.at("provenance") || run.at("result").at("project_snapshot")!=run.at("document"))throw std::invalid_argument("History result does not match its model/provenance");
            if(!run.at("label").is_string() || !run.at("group_id").is_string() || !run.at("diff").is_array() || !run.at("request").is_object())throw std::invalid_argument("Invalid history content");
            if(!runs.insert(run.at("run_id").get<std::string>()).second || run.at("run_id")=="" || !run.at("pinned").is_boolean() || !run.at("visible").is_boolean() || !run.at("name").is_string()) throw std::invalid_argument("Invalid history metadata");
            if(run.at("model_hash")!=DocumentHash(run.at("document")) || !run.at("result").is_object() || run.at("provenance").at("device_hash")!=run.at("model_hash")) throw std::invalid_argument("Invalid history provenance");
            if(!run.at("colour").is_array() || run.at("colour").size()!=4) throw std::invalid_argument("Invalid history colour");
            for(const auto &c:run.at("colour")) if(!c.is_number() || !std::isfinite(c.get<double>()) || c<0 || c>1) throw std::invalid_argument("Invalid history colour");
        }
        const auto selected=item.value("selected_run",std::string{});
        if(!selected.empty() && !runs.contains(selected)) throw std::invalid_argument("Unknown selected_run");
        if(item.contains("actions")) {
            const auto &actions=item.at("actions");if(!actions.is_object())throw std::invalid_argument("actions must be an object");
            for(auto a=actions.begin();a!=actions.end();++a) {
                if(!a->is_object())throw std::invalid_argument("Each action must be an object");
                const auto expected=a.key()=="iv"?"run":a.key();
                if(a->contains("operation") && a->at("operation")!=expected)throw std::invalid_argument("Action operation does not match its technique");
                for(const auto *key:{"temperature_K","voltage_V","start_V","stop_V","step_V","frequency_Hz","start_Hz","stop_Hz","points","start_nm","stop_nm","probe_photon_flux_m2_s","probe_power_W_m2"})
                    if(a->contains(key) && (!a->at(key).is_number() || !std::isfinite(a->at(key).get<double>())))throw std::invalid_argument("Invalid numeric action setting");
                if(a->contains("probe_mode") && a->at("probe_mode")!="constant_flux" && a->at("probe_mode")!="constant_power")throw std::invalid_argument("Invalid QE probe mode");
            }
        }
        if(item.at("name").get<std::string>().empty()) throw std::invalid_argument("Device name is empty");
        const auto &colour=item.at("colour");if(!colour.is_array() || colour.size()!=4) throw std::invalid_argument("colour must contain four RGBA values");
        for(const auto &c:colour) if(!c.is_number() || !std::isfinite(c.get<double>()) || c.get<double>()<0 || c.get<double>()>1) throw std::invalid_argument("Invalid RGBA colour");
        for(const auto *key:{"result","band_result","ac_results"}) if(item.contains(key) && !item.at(key).is_object()) throw std::invalid_argument("Results must be objects");
        auto validate=[&](const Json &r,bool profiles=true) {
            if(r.empty()) return;
            if(!r.is_object() || !r.contains("provenance") || r.at("provenance").at("device_hash")!=DocumentHash(r.value("project_snapshot",item.at("document")))) throw std::invalid_argument("Result device hash does not match document");
            if(!r.at("provenance").at("solver_version").is_string() || !r.at("provenance").at("time_unix_ms").is_number_integer()) throw std::invalid_argument("Invalid result provenance");
            if(!r.at("points").is_array() || r.at("points").empty()) throw std::invalid_argument("Invalid result points");
            const bool ac=r.contains("kind");
            if((profiles && !r.at("mesh_x_m").is_array()) || (!ac && (!r.at("contact_phi_eV").is_array() || r.at("contact_phi_eV").size()!=2))) throw std::invalid_argument("Invalid result mesh/contacts");
            for(const auto &point:r.at("points")) {
                if(!point.is_object() || !point.at("status").is_string() || !point.at("valid").is_boolean() || !point.at("voltage_V").is_number()) throw std::invalid_argument("Invalid result point");
                for(const auto *key:{"terminal_voltage_V","internal_voltage_V"}) if(point.contains(key) && !point.at(key).is_number() && !point.at(key).is_null()) throw std::invalid_argument("Invalid result voltage");
                if(r.value("kind",std::string{})=="qe") {for(const auto *key:{"wavelength_nm","EQE","incident_photon_flux_m2_s","probe_power_W_m2"}) if(!point.at(key).is_number() && !point.at(key).is_null()) throw std::invalid_argument("Invalid QE result value");}
                else if(ac) {for(const auto *key:{"frequency_Hz","temperature_K","capacitance_F","conductance_S","G_over_omega_F","inverse_C2_F_minus2","conservation_relative"}) if(!point.at(key).is_number() && !point.at(key).is_null()) throw std::invalid_argument("Invalid AC result value");}
                else {if(!point.at("J_A_m2").is_number() && !point.at("J_A_m2").is_null()) throw std::invalid_argument("Invalid current value");if(profiles || point.contains("x_m"))for(const auto *key:{"x_m","Ec_eV","Ev_eV","EFn_eV","EFp_eV","n_m3","p_m3"}) if(!point.at(key).is_array()) throw std::invalid_argument("Invalid profile array");}
            }
        };
        for(const auto &run:history) {
            const auto &result=run.at("result");
            if(run.at("kind")!="batch") {validate(result,run.value("profiles_stored",true));continue;}
            if(!result.at("provenance").at("solver_version").is_string() || !result.at("provenance").at("time_unix_ms").is_number_integer() || !result.at("rows").is_array() || result.at("rows").size()!=1 || !result.at("columns").is_array() || !result.at("spec").is_object())throw std::invalid_argument("Invalid batch history result");
            const auto &row=result.at("rows").front();
            if(!row.at("id").is_string() || !row.at("status").is_string() || !row.at("elapsed_s").is_number() || !row.at("parameters").is_object() || !row.at("values").is_object() || !row.at("statuses").is_object())throw std::invalid_argument("Invalid batch history member");
            for(const auto &column:result.at("columns")) {
                const auto id=column.at("id").get<std::string>();const auto &cell=row.at("values").at(id);
                if(!column.at("unit").is_string() || (!cell.is_number() && !cell.is_null()) || !row.at("statuses").at(id).is_string())throw std::invalid_argument("Invalid history recorder cell");
            }
            if(row.contains("curves")) {
                if(!row.at("curves").at("iv").is_array() || !row.at("curves").at("ac").is_array())throw std::invalid_argument("Invalid history curves");
                for(const auto &point:row.at("curves").at("iv")) if(!point.at("status").is_string() || !point.at("voltage_V").is_number() || (!point.at("J_Am2").is_number() && !point.at("J_Am2").is_null()))throw std::invalid_argument("Invalid history I-V point");
            }
        }
        if(item.contains("batch_result") && !item.at("batch_result").empty()) {
            const auto &table=item.at("batch_result");
            if(!table.at("provenance").value("solver_version",Json()).is_string() || !table.at("provenance").value("time_unix_ms",Json()).is_number_integer()) throw std::invalid_argument("Invalid batch provenance");
            if(table.at("provenance").at("device_hash")!=DocumentHash(table.value("project_snapshot",item.at("document")))) throw std::invalid_argument("Batch device hash does not match document");
            const auto plan=BatchPreview(table.value("project_snapshot",item.at("document")),table.at("spec"));
            if(!table.at("columns").is_array() || !table.at("rows").is_array() || table.at("rows").size()!=plan.at("member_count").get<std::size_t>()) throw std::invalid_argument("Invalid batch worksheet");
            std::set<std::string> members;
            for(std::size_t i=0;i<table.at("rows").size();++i) {
                const auto &row=table.at("rows")[i];const auto member=row.at("id").get<std::string>();
                if(member.empty() || !members.insert(member).second || !row.at("status").is_string() || !row.at("elapsed_s").is_number() || row.at("parameters")!=plan.at("combinations")[i]) throw std::invalid_argument("Invalid batch member");
                for(const auto &column:table.at("columns")) {
                    const auto id=column.at("id").get<std::string>();const auto &value=row.at("values").at(id);
                    if(!column.at("unit").is_string() || (!value.is_number() && !value.is_null()) || !row.at("statuses").at(id).is_string()) throw std::invalid_argument("Invalid recorder cell");
                }
                if(row.contains("curves") && (!row.at("curves").at("iv").is_array() || !row.at("curves").at("ac").is_array())) throw std::invalid_argument("Invalid batch curves");
            }
        }
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
        state->history=item.value("history",Json::array());TrimHistory(state->history,retention);
        state->selectedRun=item.value("selected_run",std::string{});
        if(std::none_of(state->history.begin(),state->history.end(),[&](const Json &r){return r.at("run_id")==state->selectedRun;}))state->selectedRun.clear();
        const auto selection=item.value("selection",Json::object());
        state->selectedLayer=selection.value("layer",0);state->selectedInterface=selection.value("interface",-1);state->selectedBias=selection.value("bias",0);state->selectedMember=selection.value("member",0);state->biasVoltage_V=selection.value("voltage_V",0.);
        state->actions=item.value("actions",Json::object());
        state->id=id;state->name=item.at("name");state->colour=item.at("colour");state->description=item.value("description",std::string{});
        state->batchResult=item.value("batch_result",Json::object());state->batchSpec=item.value("batch_spec",BatchDefaults());state->recorderColumns=item.value("recorder_columns",RecorderDefaults());
        state->document=NormalizeDocument(item.at("document"));state->result=item.value("result",Json::object());state->bandResult=item.value("band_result",Json::object());state->acResults=item.value("ac_results",Json::object());state->status=item.value("status",std::string("no results"));
        checked.devices.push_back(state);
    }
    project=std::move(checked);
}
void Workflow::RefreshDirty() {for(auto &d:project.devices) d->dirty=d->metadataDirty || !d->undo.IsClean();dirty=projectDirty || std::any_of(project.devices.begin(),project.devices.end(),[](const auto &d){return d->dirty;});}
Json Workflow::Snapshot() const {
    const auto &d=*std::find_if(project.devices.begin(),project.devices.end(),[&](const auto &d){return d->id==project.active_device;});
    Json list=Json::array();for(const auto &item:project.devices) list.push_back({{"device_id",item->id},{"name",item->name},{"description",item->description},{"colour",item->colour},{"status",item->status},{"dirty",item->dirty},{"compare",project.compare_set.contains(item->id)}});
    return {{"ok",true},{"history",d->history},{"selected_run",d->selectedRun},{"selection",{{"layer",d->selectedLayer},{"interface",d->selectedInterface},{"bias",d->selectedBias},{"member",d->selectedMember},{"voltage_V",d->biasVoltage_V}}},{"actions",d->actions},{"document",d->document},{"result",d->result},{"band_result",d->bandResult},{"ac_results",d->acResults},{"batch_result",d->batchResult},{"batch_spec",d->batchSpec},{"recorder_columns",d->recorderColumns},
        {"devices",list},{"active_device",project.active_device},{"project",Workspace()},{"runs",Runs()},{"dirty",dirty},{"auto_recalculate",autoRecalculate},{"auto_recalculate_band",autoRecalculateBand},{"error",nullptr}};
}
}
