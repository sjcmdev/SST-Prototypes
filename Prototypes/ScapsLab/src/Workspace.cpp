#include "Workflow.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib,"psapi.lib")
#endif
namespace SST::ScapsLab {
RunAction ReadRunAction(ActionKind kind,const Json& value) {
    auto a=RunPlan{}.actions[std::size_t(kind)];
    a.include=value.value("include",a.include);a.onEdit=value.value("on_edit",false);
    a.voltage_V=value.value("voltage_V",a.voltage_V);a.frequency_Hz=value.value("frequency_Hz",a.frequency_Hz);a.temperature_K=value.value("temperature_K",0.);
    a.constantFlux=value.value("probe_mode",std::string("constant_flux"))=="constant_flux";
    a.photon_flux_m2_s=value.value("probe_photon_flux_m2_s",a.photon_flux_m2_s);a.probe_power_W_m2=value.value("probe_power_W_m2",a.probe_power_W_m2);
    if(value.contains("range")) {
        const auto& r=value.at("range");SweepRange range;
        if(r.contains("points") && (!r.at("points").is_number_integer()))throw std::invalid_argument("Action points must be an integer");
        const auto sampling=r.at("sampling").get<std::string>();
        if(sampling!="points" && sampling!="step" && sampling!="list")throw std::invalid_argument("Unknown Action sampling");
        range.sampling=sampling=="points"?SweepSampling::Points:sampling=="step"?SweepSampling::Step:SweepSampling::List;
        range.logarithmic=r.value("logarithmic",false);range.minimum=r.value("minimum",0.);range.maximum=r.value("maximum",0.);range.step=r.value("step",1.);range.points=r.value("points",2);range.custom=r.value("values",std::vector<double>{});a.range=std::move(range);
    } else if(kind==ActionKind::Cv) {
        if(value.contains("start_V") || value.contains("stop_V") || value.contains("step_V")) {
            auto r=*a.range;r.sampling=SweepSampling::Step;r.minimum=value.value("start_V",r.minimum);r.maximum=value.value("stop_V",r.maximum);r.step=value.value("step_V",r.step);a.range=std::move(r);
        }
    } else if(kind==ActionKind::Cf) {a.range->minimum=value.value("start_Hz",a.range->minimum);a.range->maximum=value.value("stop_Hz",a.range->maximum);a.range->points=value.value("points",a.range->points);}
    else if(kind==ActionKind::Qe) {a.range->minimum=value.value("start_nm",a.range->minimum);a.range->maximum=value.value("stop_nm",a.range->maximum);a.range->points=value.value("points",a.range->points);}
    if(a.temperature_K!=0)RequireParameter("temperature_K",a.temperature_K);
    RequireParameter("voltage_V",a.voltage_V);RequireParameter("frequency_Hz",a.frequency_Hz);
    RequireParameter("probe_photon_flux_m2_s",a.photon_flux_m2_s);RequireParameter("probe_power_W_m2",a.probe_power_W_m2);
    if(a.range)for(double x:SweepValues(*a.range))RequireParameter(kind==ActionKind::Cf?"frequency_Hz":kind==ActionKind::Qe?"wavelength_nm":"voltage_V",x);
    return a;
}
RunPlan ReadRunPlan(const Json& value) {
    ValidateActions(value);
    RunPlan plan;
    for(auto& a:plan.actions)if(value.contains(ActionId(a.kind)))a=ReadRunAction(a.kind,value.at(ActionId(a.kind)));
    // The execution field is kept inside a technique object for compatibility with legacy actions objects.
    if(value.contains("iv")){plan.pauseAfterAction=value.at("iv").value("pause_after_action",false);plan.pauseAfterMember=value.at("iv").value("pause_after_member",false);}
    return plan;
}
Json RunPlanJson(const RunPlan& plan,const Json& original) {
    Json out=original;
    for(const auto& a:plan.actions) {
        auto& v=out[ActionId(a.kind)];if(!v.is_object())v=Json::object();
        v["operation"]=ActionOperation(a.kind);v["include"]=a.include;v["on_edit"]=a.onEdit;v["voltage_V"]=a.voltage_V;v["frequency_Hz"]=a.frequency_Hz;v["temperature_K"]=a.temperature_K;
        v["probe_mode"]=a.constantFlux?"constant_flux":"constant_power";v["probe_photon_flux_m2_s"]=a.photon_flux_m2_s;v["probe_power_W_m2"]=a.probe_power_W_m2;
        if(a.range) {const auto& r=*a.range;auto& fields=v["range"];if(!fields.is_object())fields=Json::object();fields.update({{"sampling",r.sampling==SweepSampling::Points?"points":r.sampling==SweepSampling::Step?"step":"list"},{"logarithmic",r.logarithmic},{"minimum",r.minimum},{"maximum",r.maximum},{"step",r.step},{"points",r.points},{"values",r.custom}});}
        else v.erase("range");
    }
    out["iv"]["pause_after_action"]=plan.pauseAfterAction;if(plan.pauseAfterMember || out["iv"].contains("pause_after_member"))out["iv"]["pause_after_member"]=plan.pauseAfterMember;return out;
}
std::size_t PrivateBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX m{};if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m),sizeof(m)))return m.PrivateUsage;
#endif
    return 0;
}
std::size_t JsonValues(const Json &v) {std::size_t n=1;if(v.is_structured())for(const auto &c:v)n+=JsonValues(c);return n;}
namespace {
// Approximate heap bytes of typed results; interned vectors are counted once per call (`seen`).
std::size_t PBytes(const PJson &p,std::set<const void*> &seen) {
    std::size_t n=sizeof(PJson)+JsonValues(p.prim)*16;
    if(p.kind==PJson::Kind::Floats && p.f.id() && seen.insert(p.f.id()).second) n+=p.f.size()*8;
    for(const auto &k:p.kids) n+=PBytes(k.second,seen);
    return n;
}
std::size_t VBytes(const Column &v,std::set<const void*> &seen) {return v.id() && seen.insert(v.id()).second?v.size()*8:0;}
std::size_t TypedBytes(const ResultBase *r,std::set<const void*> &seen) {
    if(!r || !seen.insert(r).second) return 0;
    std::size_t n=0;
    if(auto iv=dynamic_cast<const IvResult*>(r)) {
        n=PBytes(iv->meta,seen)+VBytes(iv->mesh_x_m,seen)+iv->curve.size()*sizeof(scaps_core::IVSample);
        for(const auto &p:iv->points) {
            n+=sizeof(IvPoint)+PBytes(p.rest,seen);for(const auto *v:{&p.x_m,&p.Ec_eV,&p.Ev_eV,&p.EFn_eV,&p.EFp_eV,&p.n_m3,&p.p_m3})n+=VBytes(*v,seen);
            if(p.details && seen.insert(p.details.get()).second) {
                const auto& g=p.details->generation_recombination;n+=sizeof(IvDetails)+VBytes(g.x_m,seen)+VBytes(g.generation_m3_s,seen);
                for(const auto& c:g.channels)n+=sizeof(RecombinationChannel)+VBytes(c.x_m,seen)+VBytes(c.R_m3_s,seen)+VBytes(c.R_sheet_m2_s,seen)+c.by_layer.size()*sizeof(LayerCurrent);
                for(const auto& d:p.details->defect_previews)n+=sizeof(DefectPreview)+8*(d.energy_eV.size()+d.density.size()+d.occupation_energy_eV.size()+d.occupation.size()+d.band_edges_eV.size());
                for(const auto& v:p.details->trap_occupation)n+=v.size()*8;n+=p.details->interface_occupation.size()*8;
            }
        }
        if(iv->optics) {n+=VBytes(iv->optics->x_m,seen)+VBytes(iv->optics->generation_m3_s,seen);for(const auto& c:iv->optics->absorption)n+=VBytes(c.wavelength_nm,seen)+VBytes(c.alpha_m1,seen)+VBytes(c.without_tail_m1,seen);}
    } else if(auto ac=dynamic_cast<const AcResult*>(r)) {
        n=PBytes(ac->meta,seen)+VBytes(ac->mesh_x_m,seen);for(const auto &p:ac->points)n+=sizeof(AcPoint)+PBytes(p.rest,seen);
    } else if(auto b=dynamic_cast<const BatchResult*>(r)) {
        n=PBytes(b->body,seen);
        for(const auto& row:b->rows)if(seen.insert(row.get()).second) {
            n+=sizeof(scaps_core::BatchRow)+row->iv.size()*sizeof(scaps_core::IVSample)+row->ac.size()*sizeof(scaps_core::ACSample);
            for(const auto& p:row->iv)if(p.profiles && seen.insert(p.profiles.get()).second){n+=p.profiles->x_m.size()*8;for(const auto& [field,v]:p.profiles->fields)n+=v.size()*8;}
        }
    }
    return n;
}
}
std::string Workflow::MemoryReport() const {
    std::string out="private "+std::to_string(PrivateBytes()>>20)+" MB;";
    for(const auto &d:project.devices) {
        std::set<const void*> seen;std::size_t bytes=TypedBytes(d->result.typed().get(),seen)+TypedBytes(d->bandResult.typed().get(),seen)+TypedBytes(d->batchResult.typed().get(),seen);
        for(const auto &[k,r]:d->acResults.all())bytes+=TypedBytes(r.get(),seen);
        for(const auto &[id,r]:*d->runs)bytes+=TypedBytes(r.get(),seen);
        out+=" ["+d->name+": history "+std::to_string(d->history.size())+" runs, typed results "+std::to_string(bytes>>20)+" MB (shared/interned counted once), json meta "+std::to_string(JsonValues(*d->history)>>10)+" kvalues]";
    }
    return out;
}
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
    for(const auto &run:history) if(!run.at("pinned").get<bool>() && !run.value("explicit",false) && !run.value("legacy",false)) ++automatic;
    for(auto it=history.begin();it!=history.end() && automatic>retention;) {
        if(!it->at("pinned").get<bool>() && !it->value("explicit",false) && !it->value("legacy",false)) {it=history.erase(it);--automatic;} else ++it;
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
std::string StoreRun(Json &history,const std::string &kind,const Json &result,const Json &document,const Json &request,int retention,const std::string &group,const Json *parameters,const std::string *modelHash) {
    static std::atomic_uint64_t sequence{0};
    const auto id="run-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++);
    const Json &previous=history.empty()?document:history.back().at("document"); // a reference: nothing below touches `history` before the push_back
    auto changes=Json::array();std::string label;
    Json computed;if(!parameters) computed=BatchParameters(document); // batch members hand over theirs, computed on the worker
    for(const auto &parameter:parameters?*parameters:computed) {
        const Json::json_pointer pointer(parameter.at("pointer").get<std::string>());
        if(previous.contains(pointer) && previous.at(pointer)!=parameter.at("value")) {
            changes.push_back({{"parameter_id",parameter.at("id")},{"unit",parameter.at("unit")},{"before",previous.at(pointer)},{"after",parameter.at("value")}});
            if(!label.empty()) label+=", ";label+=ParameterLabel(parameter.at("path"),parameter.at("unit"),parameter.at("value"),previous.at(pointer));
        }
    }
    const auto modelDiff=Json::diff(previous,document);
    if(label.empty()) label=history.empty()?"Initial run":previous==document?"Same parameters":"Model changed: "+modelDiff.front().at("path").get<std::string>();
    if(request.value("operation",std::string{})=="single_shot") {
        const std::time_t seconds=result.at("provenance").at("time_unix_ms").get<std::int64_t>()/1000;std::tm local{};localtime_s(&local,&seconds);char time[32];std::strftime(time,sizeof(time),"%H:%M:%S",&local);label=std::string("Single shot ")+time;
    }
    const double t=double(sequence%12)/12.;
    Json colour=Json::array({.5+.45*std::cos(6.283*t),.5+.45*std::cos(6.283*(t-.333)),.5+.45*std::cos(6.283*(t+.333)),1.});
    history.push_back({{"run_id",id},{"kind",kind},{"model_hash",modelHash?*modelHash:DocumentHash(document)},{"document",document},{"diff",changes},{"model_diff",modelDiff},{"label",label},{"request",request},{"time_unix_ms",result.at("provenance").at("time_unix_ms")},{"elapsed_s",result.value("elapsed_s",result.value("compute_time_ms",0.)/1000.)},{"provenance",result.at("provenance")},{"pinned",false},{"name",""},{"visible",true},{"colour",colour},{"group_id",group}});
    if(request.value("operation",std::string{})=="single_shot" || request.value("operation",std::string{})=="capture_live")history.back()["explicit"]=true;
    TrimHistory(history,retention);
    return id;
}
// Drop the spatial profiles of one run (keeps the document, I-V curve and scalar metrics); the UI then offers "Recalculate to view profiles".
void DropProfiles(Json &run) {
    auto &result=run["result"];
    if(!result.contains("points") || !result.contains("mesh_x_m")) return;
    for(auto &point:result["points"]) for(auto field=point.begin();field!=point.end();) {
        if(field->is_array() || field->is_object()) field=point.erase(field);else ++field;
    }
    result.erase("mesh_x_m");run["profiles_stored"]=false;
}
void CompactHistory(Json &workspace) {
    // shortcut: profiles over 64 KiB stay in memory only; move them to the HDF5 sidecar in slview1.
    for(auto &device:workspace["devices"]) for(auto &run:device["history"]) {
        if(run.value("explicit",false) || run.value("legacy",false) || run["result"].dump().size()<=65536) continue;
        DropProfiles(run);
    }
}
// Memory bound: only pinned runs, the selected run, the live result and the newest `keep` runs keep spatial profiles.
void LimitProfiles(DeviceState &d,int keep) {
    const auto &h=*d.history;std::vector<std::string> drop;int kept=0;
    for(std::size_t i=h.size();i-->0;) {
        const auto &run=h[i];
        if(run.value("explicit",false) || run.value("legacy",false) || !run.value("profiles_stored",true)) continue;
        const auto id=run.at("run_id").get<std::string>();const auto typed=d.RunResult(id);
        const auto iv=dynamic_cast<const IvResult*>(typed.get());const auto ac=dynamic_cast<const AcResult*>(typed.get());
        const bool has=iv?HasProfiles(*iv):ac&&(ac->has>>31&1u)&&((ac->has&1u)||(ac->meta.prim.is_object()&&ac->meta.prim.contains("mesh_x_m")));
        if(!has) continue;
        bool live=false;for(const auto *r:{d.result.typed().get(),d.bandResult.typed().get()}) live|=r && !r->Provenance().is_null() && run.at("provenance")==r->Provenance();
        if(live) ++kept; // the live run counts towards the LRU
        if(live || run.at("pinned")==true || id==d.selectedRun || kept++<keep) continue;
        drop.push_back(id);
    }
    if(drop.empty()) return;
    auto runs=CreateRef<RunMap>(*d.runs);auto &m=d.history.mut();
    for(const auto &id:drop) {
        auto typed=d.RunResult(id);
        if(auto iv=std::dynamic_pointer_cast<const IvResult>(typed)) (*runs)[id]=DropProfiles(*iv);
        else if(auto ac=std::dynamic_pointer_cast<const AcResult>(typed)) {auto out=CreateRef<AcResult>(*ac);out->mesh_x_m=Column();out->has&=~1u;if(out->meta.prim.is_object())out->meta.prim.erase("mesh_x_m");for(auto &p:out->points)if(!p.verbatim)DropStructured(p.rest);(*runs)[id]=out;}
        for(auto &run:m) if(run.at("run_id")==id) run["profiles_stored"]=false;
    }
    d.runs=std::move(runs);
}
DeviceResults DeviceState::Results() const {DeviceResults r;r.result=result.typed();r.band=bandResult.typed();r.ac=acResults.all();r.batch=batchResult.typed();r.runs=runs;return r;}
void DeviceState::SetResults(const DeviceResults &r) {result.adopt(r.result);bandResult.adopt(r.band);acResults.clear();for(const auto &[k,v]:r.ac)acResults.set(k,v);batchResult.adopt(r.batch);runs=r.runs?r.runs:CreateRef<const RunMap>();}
void DeviceState::SyncRuns() {
    std::set<std::string> ids;for(const auto &run:*history) ids.insert(run.at("run_id").get<std::string>());
    if(std::all_of(runs->begin(),runs->end(),[&](const auto &e){return ids.contains(e.first);})) return;
    auto m=CreateRef<RunMap>();for(const auto &e:*runs) if(ids.contains(e.first)) m->insert(e);runs=std::move(m);
}
namespace {Json JsonOf(const ResultBase &r,bool cached) {return cached?r.View():r.Dump();}}
Json HistoryJson(const Json &history,const RunMap &runs,bool cached) {
    Json out=Json::array();
    for(const auto &run:history) {
        Json copy=run;auto it=runs.find(run.at("run_id").get<std::string>());
        copy["result"]=it==runs.end()?Json::object():JsonOf(*it->second,cached);
        if(it!=runs.end())if(auto batch=std::dynamic_pointer_cast<const BatchResult>(it->second);batch && batch->runPlan) {
            copy["document"]=copy.at("result").at("project_snapshot");copy["provenance"]=copy.at("result").at("provenance");copy["model_hash"]=copy.at("provenance").at("device_hash");
        }
        out.push_back(std::move(copy));
    }
    return out;
}
void AttachResults(Json &device,const Json &history,const DeviceResults &r,bool cached,bool compact) {
    if(compact) {
        Json pool=Json::object();std::map<const ResultBase*,std::string> ids;
        const auto store=[&](const Ref<const ResultBase>& result)->Json {
            if(!result || result->liveOnly)return Json::object();
            if(auto found=ids.find(result.get());found!=ids.end())return {{"$result_ref",found->second}};
            auto value=JsonOf(*result,cached);std::string id;
            for(const auto& entry:pool.items())if(entry.value()==value){id=entry.key();break;}
            if(id.empty()){id="result-"+std::to_string(pool.size());pool[id]=std::move(value);}
            ids[result.get()]=id;return {{"$result_ref",id}};
        };
        device["history"]=Json::array();
        for(const auto& run:history) {
            auto copy=run;const auto found=r.runs->find(run.at("run_id").get<std::string>());
            copy["result"]=found==r.runs->end()?Json::object():store(found->second);
            if(found!=r.runs->end())if(auto batch=std::dynamic_pointer_cast<const BatchResult>(found->second);batch && batch->runPlan) {
                const auto& saved=pool.at(copy.at("result").at("$result_ref").get<std::string>());
                copy["document"]=saved.at("project_snapshot");copy["provenance"]=saved.at("provenance");copy["model_hash"]=copy.at("provenance").at("device_hash");
            }
            device["history"].push_back(std::move(copy));
        }
        device["result"]=store(r.result);device["band_result"]=store(r.band);device["batch_result"]=store(r.batch);
        device["ac_results"]=Json::object();for(const auto& [kind,result]:r.ac)if(!result->liveOnly)device["ac_results"][kind]=store(result);
        if(!pool.empty())device["stored_results"]=std::move(pool);return;
    }
    device["history"]=HistoryJson(history,*r.runs,cached);
    device["result"]=r.result && !r.result->liveOnly?JsonOf(*r.result,cached):Json::object();device["band_result"]=r.band && !r.band->liveOnly?JsonOf(*r.band,cached):Json::object();
    Json ac=Json::object();for(const auto &[k,v]:r.ac)if(!v->liveOnly)ac[k]=JsonOf(*v,cached);device["ac_results"]=std::move(ac);
    device["batch_result"]=r.batch?JsonOf(*r.batch,cached):Json::object();
}
std::vector<DeviceResults> ExtractResults(Json &workspace) {
    std::vector<DeviceResults> out;
    if(!workspace.is_object() || !workspace.contains("devices")) return out;
    for(auto &item:workspace["devices"]) {
        DeviceResults r;auto runs=CreateRef<RunMap>();std::map<std::string,Ref<const ResultBase>> decoded;
        const auto read=[&](Json value,const std::string& kind)->Ref<const ResultBase> {
            std::string id;
            if(value.size()==1 && value.contains("$result_ref") && item.contains("stored_results")){id=value.at("$result_ref").get<std::string>();if(auto found=decoded.find(id);found!=decoded.end())return found->second;value=item.at("stored_results").at(id);}
            Ref<const ResultBase> result;
            if(kind=="iv" || kind=="band")result=CreateRef<const IvResult>(IvFromJson(std::move(value)));
            else if(kind=="batch")result=CreateRef<const BatchResult>(BatchFromJson(value));
            else result=CreateRef<const AcResult>(AcFromJson(std::move(value)));
            if(!id.empty())decoded[id]=result;return result;
        };
        const auto take=[&](const char *key) {Json v=Json::object();if(item.contains(key)) {v=std::move(item[key]);item[key]=Json::object();}return v;};
        if(auto j=take("result");!j.empty()) r.result=std::dynamic_pointer_cast<const IvResult>(read(std::move(j),"iv"));
        if(auto j=take("band_result");!j.empty()) r.band=std::dynamic_pointer_cast<const IvResult>(read(std::move(j),"band"));
        if(auto j=take("ac_results");j.is_object()) for(auto it=j.begin();it!=j.end();++it) if(!it.value().empty()) r.ac[it.key()]=std::dynamic_pointer_cast<const AcResult>(read(it.value(),it.key()));
        if(auto j=take("batch_result");!j.empty()) r.batch=std::dynamic_pointer_cast<const BatchResult>(read(std::move(j),"batch"));
        if(item.contains("history") && item["history"].is_array()) for(auto &run:item["history"]) {
            if(!run.contains("result")) continue;
            Json result=std::move(run["result"]);run.erase("result");
            const auto id=run.at("run_id").get<std::string>(),kind=run.at("kind").get<std::string>();
            if(kind=="iv" || kind=="band") (*runs)[id]=read(std::move(result),kind);
            else if(kind=="batch") {auto batch=std::dynamic_pointer_cast<const BatchResult>(read(std::move(result),kind));if(batch->runPlan){run["document"]=Json::object();run["model_hash"]="";run["provenance"]["device_hash"]="";}(*runs)[id]=std::move(batch);}
            else (*runs)[id]=read(std::move(result),kind);
        }
        item.erase("stored_results");r.runs=std::move(runs);out.push_back(std::move(r));
    }
    return out;
}
DeviceState &Workflow::Device(const std::string &id) {
    for(const auto &d:project.devices) if(d->id==id) return *d;
    throw std::invalid_argument("Unknown device_id: "+id);
}
DeviceState &Workflow::Current() {return Device(project.active_device);}
Json Workflow::Workspace(bool withResults) const {
    // withResults=false (autosave): skip history/results so the UI thread does not convert tens of MB.
    static const Json none=Json::object(),noHistory=Json::array();
    Json devices=Json::array();
    for(const auto &d:project.devices) {
        Json device={{"id",d->id},{"name",d->name},{"colour",d->colour},{"description",d->description},
            {"history",noHistory},{"selected_run",withResults?d->selectedRun:std::string{}},{"selection",{{"layer",d->selectedLayer},{"interface",d->selectedInterface},{"bias",d->selectedBias},{"member",d->selectedMember},{"voltage_V",d->biasVoltage_V}}},{"actions",d->runPlanEdited?RunPlanJson(*d->runPlan,d->actions):d->actions},{"document",d->Document()},{"result",none},{"band_result",none},{"ac_results",none},{"batch_result",none},{"batch_spec",d->BatchSpec()},{"recorder_columns",d->RecorderSpec()},{"status",withResults?d->status:std::string("no results")}};
        auto selection=d->fileExtensions.value("selection",Json::object());selection.update(device.at("selection"));if(d->selectedAttempt || selection.contains("attempt"))selection["attempt"]=d->selectedAttempt;device["selection"]=std::move(selection);
        auto extensions=d->fileExtensions;extensions.update(device);device=std::move(extensions);
        if(d->runPlanEdited || device.contains("use_run_actions"))device["use_run_actions"]=d->runPlanEdited;
        if(withResults) AttachResults(device,*d->history,d->Results(),!lightResponse);
        devices.push_back(std::move(device));
    }
    auto out=project.fileExtensions;out.update(Json{{"schema_version",project.schema_version},{"name",project.name},{"devices",devices},{"active_device",project.active_device},{"compare_set",project.compare_set},{"ui",project.ui}});return out;
}
// ui object checks shared by workspace validation and project_edit; returns history_retention
int ValidateUi(const Json &ui,bool strictPlotRanges) {
    if(!ui.is_object()) throw std::invalid_argument("ui must be an object");
    if(ui.contains("history_retention") && !ui.at("history_retention").is_number_integer())throw std::invalid_argument("history_retention must be an integer");
    if(ui.contains("panels")) {if(!ui.at("panels").is_object())throw std::invalid_argument("panels must be an object");for(const auto &visible:ui.at("panels"))if(!visible.is_boolean())throw std::invalid_argument("panel visibility must be boolean");}
    if(strictPlotRanges && ui.contains("plots")) {
        if(!ui.at("plots").is_object())throw std::invalid_argument("plots must be an object");
        for(const auto &limits:ui.at("plots")) {
            if(!limits.is_array() || (limits.size()!=4 && limits.size()!=6))throw std::invalid_argument("plot limits require xmin, xmax, ymin, ymax and optional y2min, y2max");
            for(const auto &v:limits)if(!v.is_number() || !std::isfinite(v.get<double>()))throw std::invalid_argument("plot limits must be finite");
            if(limits[0]>=limits[1] || limits[2]>=limits[3] || (limits.size()==6 && limits[4]>=limits[5]))throw std::invalid_argument("plot limits must be ordered");
        }
    }
    for(const auto *key:{"optical_tab","gr_tab","recorder_plot"})if(ui.contains(key) && (!ui.at(key).is_number_integer() || ui.at(key).get<int>()<0 || ui.at(key).get<int>()>10000))throw std::invalid_argument("Invalid selected tab/plot");
    for(const auto *key:{"gr_follow","iv_absolute_log","contact_annotations","batch_absolute"})if(ui.contains(key) && !ui.at(key).is_boolean())throw std::invalid_argument("Invalid plot toggle");
    if(ui.contains("results_action")){bool valid=false;for(int i=0;i<5;++i)valid|=ui.at("results_action")==ActionId(ActionKind(i));if(!valid)throw std::invalid_argument("Unknown Results Action");}
    if(ui.contains("gr_hidden")){if(!ui.at("gr_hidden").is_array())throw std::invalid_argument("gr_hidden must be an array");for(const auto &id:ui.at("gr_hidden"))if(!id.is_string())throw std::invalid_argument("Invalid mechanism ID");}
    if(ui.contains("gr_bias") && (!ui.at("gr_bias").is_object() || !ui.at("gr_bias").at("voltage_V").is_number() || !std::isfinite(ui.at("gr_bias").at("voltage_V").get<double>())))throw std::invalid_argument("Invalid G/R voltage_V");
    const auto retention=ui.value("history_retention",5);
    if(retention<1 || retention>10000) throw std::invalid_argument("history_retention must be 1..10000");
    return retention;
}
void ValidateActions(const Json &actions) {
    if(!actions.is_object())throw std::invalid_argument("actions must be an object");
    for(auto a=actions.begin();a!=actions.end();++a) {
        if(!a->is_object())throw std::invalid_argument("Each action must be an object");
        const auto expected=a.key()=="iv"?"run":a.key();
        if(a->contains("operation") && a->at("operation")!=expected)throw std::invalid_argument("Action operation does not match its technique");
        for(const auto *key:{"temperature_K","voltage_V","start_V","stop_V","step_V","frequency_Hz","start_Hz","stop_Hz","points","start_nm","stop_nm","probe_photon_flux_m2_s","probe_power_W_m2"})
            if(a->contains(key) && (!a->at(key).is_number() || !std::isfinite(a->at(key).get<double>())))throw std::invalid_argument("Invalid numeric action setting");
        if(a->contains("probe_mode") && a->at("probe_mode")!="constant_flux" && a->at("probe_mode")!="constant_power")throw std::invalid_argument("Invalid QE probe mode");
        if(a->contains("range"))for(int i=0;i<5;++i)if(a.key()==ActionId(ActionKind(i))) {
            const auto typed=ReadRunAction(ActionKind(i),*a);(void)SweepValues(*typed.range);
        }
    }
}
// Pure check of a whole workspace document (no live state touched, no JobSystem).
void ValidateWorkspace(const Json &value) {
    if(value.contains("devices") && value.at("devices").is_array() && std::any_of(value.at("devices").begin(),value.at("devices").end(),[](const auto& d){return d.contains("stored_results");})) {
        if(value.value("schema_version",0)!=4)throw std::invalid_argument("Result references require ScapsLab v4");
        auto expanded=value;
        for(auto& d:expanded["devices"])if(d.contains("stored_results")) {
            const auto pool=std::move(d["stored_results"]);d.erase("stored_results");if(!pool.is_object())throw std::invalid_argument("Invalid stored_results");
            const auto expand=[&](Json& result){if(result.contains("$result_ref")){if(result.size()!=1)throw std::invalid_argument("Invalid result reference");const auto id=result.at("$result_ref").template get<std::string>();if(!pool.contains(id) || pool.at(id).contains("$result_ref"))throw std::invalid_argument("Unknown result reference");result=pool.at(id);}};
            for(const auto* key:{"result","band_result","batch_result"})if(d.contains(key))expand(d[key]);
            for(auto& result:d["ac_results"])expand(result);for(auto& run:d["history"])expand(run["result"]);
        }
        ValidateWorkspace(expanded);return;
    }
    if(value.value("schema_version",0)<0 || value.value("schema_version",0)>4) throw std::invalid_argument("Unsupported ScapsLab schema_version");
    const auto &devices=value.at("devices");
if(!devices.is_array() || devices.empty() || devices.size()>1000) throw std::invalid_argument("Expected 1..1000 devices");
    Project checked;checked.name=value.at("name").get<std::string>();if(checked.name.empty()) throw std::invalid_argument("Project name is empty");
    checked.active_device=value.at("active_device").get<std::string>();checked.compare_set=value.at("compare_set").get<std::set<std::string>>();checked.ui=value.value("ui",Json::object());
    const auto retention=ValidateUi(checked.ui,false); // optional legacy view ranges are validated/ignored by the plot reader
    std::set<std::string> ids;
    for(const auto &item:devices) {
        const auto id=item.at("id").get<std::string>();if(id.empty() || !ids.insert(id).second) throw std::invalid_argument("Invalid or duplicate device_id");
        (void)Model(item.at("document"));
        if(item.contains("selection")) {
            const auto &selection=item.at("selection");if(!selection.is_object())throw std::invalid_argument("selection must be an object");
            for(const auto *key:{"layer","interface","bias","member","attempt"})if(selection.contains(key) && (!selection.at(key).is_number_integer() || selection.at(key).get<int>()<(std::string(key)=="interface"?-1:0)))throw std::invalid_argument("Invalid selection index");
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
        if(item.contains("use_run_actions") && !item.at("use_run_actions").is_boolean())throw std::invalid_argument("use_run_actions must be boolean");
        if(item.contains("actions")) (void)ReadRunPlan(item.at("actions"));
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
            if(result.contains("run_plan") && result.contains("action_members")) {
                const auto typed=BatchFromJson(result);ValidateRunPlan(*typed.runPlan,*typed.projectSnapshot);
                const auto plan=PrepareBatch(*typed.projectSnapshot,*typed.batchDraft,*typed.recorderDraft);
                if(typed.rows.size()!=plan.combinations.size())throw std::invalid_argument("Stored Action batch has an invalid member count");
                for(const auto& row:typed.rows)for(const auto& column:typed.recorderDraft->columns)if(!row->values.contains(column.id))throw std::invalid_argument("Missing stored Recorder cell");
                continue;
            }
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
}
Json NormalizeWorkspace(Json workspace) {
    ValidateWorkspace(workspace);
    for(auto &device:workspace["devices"]) {
        device["document"]=NormalizeDocument(std::move(device["document"]));
        if(workspace.value("schema_version",0)>=4)continue;
        if(!device.contains("history"))device["history"]=Json::array();auto& history=device["history"];
        for(auto& run:history)if(!run.value("explicit",false))run["legacy"]=true;
        const auto retain=[&](const Json& result,const std::string& kind) {
            if(result.empty() || std::any_of(history.begin(),history.end(),[&](const auto& run){return run.at("result")==result;}))return;
            auto id="legacy-current-"+kind;while(std::any_of(history.begin(),history.end(),[&](const auto& run){return run.at("run_id")==id;}))id+="-copy";
            const auto document=result.value("project_snapshot",device.at("document"));
            history.push_back({{"run_id",id},{"kind",kind},{"document",document},{"model_hash",DocumentHash(document)},{"diff",Json::array()},{"model_diff",Json::array()},{"label","Legacy current result / "+kind},{"request",Json::object()},{"provenance",result.at("provenance")},{"time_unix_ms",result.at("provenance").at("time_unix_ms")},{"elapsed_s",result.value("elapsed_s",0.)},{"pinned",false},{"name",""},{"visible",true},{"colour",device.at("colour")},{"group_id",""},{"legacy",true},{"result",result}});
        };
        retain(device.value("result",Json::object()),"iv");retain(device.value("band_result",Json::object()),"band");retain(device.value("batch_result",Json::object()),"batch");
        const auto ac=device.value("ac_results",Json::object());for(const auto& [kind,result]:ac.items())retain(result,kind);
    }
    return workspace;
}
Json SingleDeviceWorkspace(Json document,std::string projectName) { // what Workspace() yields for one default device; replaces the throwaway Workflow
    const auto name=document.value("case_id",std::string("Device"));
    Json device={{"id","device-1"},{"name",name},{"colour",Json::array({0.25,0.65,0.95,1.0})},{"description",""},{"history",Json::array()},{"selected_run",""},{"selection",{{"layer",0},{"interface",-1},{"bias",0},{"member",0},{"voltage_V",0.}}},{"actions",Json::object()},{"document",std::move(document)},{"result",Json::object()},{"band_result",Json::object()},{"ac_results",Json::object()},{"batch_result",Json::object()},{"batch_spec",BatchDefaults()},{"recorder_columns",RecorderDefaults()},{"status","no results"}};
    return NormalizeWorkspace({{"schema_version",4},{"name",std::move(projectName)},{"devices",Json::array({std::move(device)})},{"active_device","device-1"},{"compare_set",Json::array({"device-1"})},{"ui",Json::object()}});
}
void Workflow::SetWorkspace(const Json &value,bool validated,const std::vector<DeviceResults> *typed,const std::vector<Ref<const ProjectDraft>>* drafts) { // validated: value came from NormalizeWorkspace (already checked and normalized)
    if(!validated){auto normalized=NormalizeWorkspace(value);SetWorkspace(normalized,true);return;}
    ++plotGen;
    if(!validated) ValidateWorkspace(value);
    std::vector<DeviceResults> own;
    if(!typed) {Json copy=value;own=ExtractResults(copy);typed=&own;} // Python/tests hand over Json with results: convert it here
    const auto &devices=value.at("devices");
    std::vector<Ref<const ProjectDraft>> imported;
    if(!drafts){for(const auto& device:devices)imported.push_back(DraftFromDocument(NormalizeDocument(device.at("document"))));drafts=&imported;}
    Project checked;checked.schema_version=4;checked.fileExtensions=value;for(const auto* key:{"schema_version","name","devices","active_device","compare_set","ui"})checked.fileExtensions.erase(key);checked.name=value.at("name").get<std::string>();checked.active_device=value.at("active_device").get<std::string>();checked.compare_set=value.at("compare_set").get<std::set<std::string>>();checked.ui=value.value("ui",Json::object());
    const auto retention=checked.ui.value("history_retention",5);
    std::size_t index=0;
    for(const auto &item:devices) {
        const auto id=item.at("id").get<std::string>();Ref<DeviceState> state;
        for(const auto &existing:project.devices) if(existing->id==id) state=existing;
        if(!state) state=CreateRef<DeviceState>();
        {Json h=Json::array();for(const auto &run:item.value("history",Json::array())) {Json copy=run;copy.erase("result");h.push_back(std::move(copy));}state->history=std::move(h);}

        state->selectedRun=item.value("selected_run",std::string{});state->selectedAttempt=item.value("selection",Json::object()).value("attempt",0);
        if(std::none_of(state->history.begin(),state->history.end(),[&](const Json &r){return r.at("run_id")==state->selectedRun;}))state->selectedRun.clear();
        const auto selection=item.value("selection",Json::object());
        state->selectedLayer=selection.value("layer",0);state->selectedInterface=selection.value("interface",-1);state->selectedBias=selection.value("bias",0);state->selectedMember=selection.value("member",0);state->biasVoltage_V=selection.value("voltage_V",0.);
        state->fileExtensions=item;for(const auto* key:{"id","name","colour","description","history","selected_run","actions","document","result","band_result","ac_results","batch_result","stored_results","batch_spec","recorder_columns","status"})state->fileExtensions.erase(key);
        state->actions=item.value("actions",Json::object());state->runPlan=CreateRef<const RunPlan>(ReadRunPlan(state->actions));state->runPlanEdited=item.value("use_run_actions",false);
        state->id=id;state->name=item.at("name");state->colour=item.at("colour");state->description=item.value("description",std::string{});
        state->SetBatchSpec(item.value("batch_spec",BatchDefaults()));state->SetRecorderSpec(item.value("recorder_columns",RecorderDefaults()));
        state->SetDraft(drafts->at(index));state->SetResults(index<typed->size()?(*typed)[index]:DeviceResults{});state->SyncRuns();state->status=item.value("status",std::string("no results"));
        state->needsRecalculate=state->needsBand=true;state->changedAt=std::chrono::steady_clock::now();state->pinStoredView=false;state->needsBiasDetails=false;state->storedBand.reset();state->detailRun.clear();
        checked.devices.push_back(state);++index;
    }
    project=std::move(checked);
}
void Workflow::RefreshDirty() {const bool was=dirty;for(auto &d:project.devices) d->dirty=d->metadataDirty || !d->undo.IsClean();dirty=projectDirty || std::any_of(project.devices.begin(),project.devices.end(),[](const auto &d){return d->dirty;});if(dirty!=was)Emit(ProjectDirtyChanged(dirty));}
Json Workflow::Snapshot() const {
    const auto &d=*std::find_if(project.devices.begin(),project.devices.end(),[&](const auto &d){return d->id==project.active_device;});
    Json list=Json::array();for(const auto &item:project.devices) list.push_back({{"device_id",item->id},{"name",item->name},{"description",item->description},{"colour",item->colour},{"status",item->status},{"dirty",item->dirty},{"compare",project.compare_set.contains(item->id)}});
    return {{"ok",true},{"history",HistoryJson(*d->history,*d->runs,true)},{"selected_run",d->selectedRun},{"selection",{{"layer",d->selectedLayer},{"interface",d->selectedInterface},{"bias",d->selectedBias},{"member",d->selectedMember},{"attempt",d->selectedAttempt},{"voltage_V",d->biasVoltage_V}}},{"actions",d->runPlanEdited?RunPlanJson(*d->runPlan,d->actions):d->actions},{"document",d->Document()},{"result",d->result.json()},{"band_result",d->bandResult.json()},{"ac_results",d->acResults.json(true)},{"batch_result",d->batchResult.json()},{"batch_spec",d->BatchSpec()},{"recorder_columns",d->RecorderSpec()},
        {"devices",list},{"active_device",project.active_device},{"project",Workspace()},{"runs",Runs()},{"dirty",dirty},{"auto_recalculate",autoRecalculate},{"auto_recalculate_band",autoRecalculateBand},{"error",nullptr}};
}
}
