#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Io.hpp"
#include <scaps_core/batch.hpp>
#include <scaps_core/api.hpp>
#include <scaps_core/device_project_def.hpp>
#include <sst/plotlab/Engine.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <atomic>
#include <thread>
#include "Core/JobSystem/ParallelFor.hpp"
#include <mutex>
#include <exception>
#include <iomanip>
namespace SST::ScapsLab {
Json BatchDefaults() {return {{"mode","nested"},{"axes",Json::array({{{"parameter_id","temperature_K"},{"start",270.},{"stop",330.},{"count",3},{"spacing","linear"}}})},{"include_curves",true}};}
Json RecorderDefaults() {return Json::array({{{"id","J_at_0V"},{"quantity","J"},{"voltage_V",0.}}});}
namespace {
std::string Unit(const std::string &key,std::string *name=nullptr) {
    if(name)*name=key;
    if(key.ends_with("Nt_m2")){if(name)name->erase(key.size()-3);return "m^-2";}
    for(const auto &[suffix,unit]:std::vector<std::pair<std::string,std::string>>{{"_m3_s","m^-3/s"},{"_cm6_s","cm^6/s"},{"_cm3_s","cm^3/s"},{"_W_m2","W/m^2"},{"_m1","m^-1"},{"_nm","nm"},{"_m2_V_s","m^2/V/s"},{"_ohm_m2","Ohm m^2"},{"_m2_eV","m^-2/eV"},{"_m_s","m/s"},{"_m3","m^-3"},{"_m2","m^2"},{"_eV","eV"},{"_Hz","Hz"},{"_K","K"},{"_V","V"},{"_m","m"}}) if(key.ends_with(suffix)) {if(name)name->erase(key.size()-suffix.size());return unit;}
    return "1";
}
std::vector<scaps_core::RecorderColumn> RecorderColumns(const Json& input) {
    if(!input.is_array())throw std::invalid_argument("columns must be a list");
    std::vector<scaps_core::RecorderColumn> columns;
    for(const auto& c:input) {
        scaps_core::RecorderColumn column;column.id=c.at("id");column.quantity=c.at("quantity");column.field=c.value("field",std::string{});
        column.voltage_V=c.value("voltage_V",double(NAN));column.frequency_Hz=c.value("frequency_Hz",double(NAN));column.x_m=c.value("x_m",double(NAN));
        column.voltage_min_V=c.value("voltage_min_V",-INFINITY);column.voltage_max_V=c.value("voltage_max_V",INFINITY);
        if((c.contains("voltage_min_V") && !std::isfinite(column.voltage_min_V)) || (c.contains("voltage_max_V") && !std::isfinite(column.voltage_max_V)))throw std::invalid_argument("Vbi window bounds must be finite");
        columns.push_back(std::move(column));
    }
    scaps_core::validate_recorder_columns(columns);return columns;
}
Json NormalizedColumns(const Json& input) {
    auto result=input;const auto columns=RecorderColumns(input);for(std::size_t i=0;i<columns.size();++i)result[i]["unit"]=columns[i].unit;return result;
}
void Save(const std::string &path,const std::string &text) {std::string error;if(!Io::Save(path,text,error)) throw std::runtime_error(error);}
double ParameterValue(const scaps_core::BatchRow& row,const std::string& key) {
    return std::visit([](const auto& value)->double {using T=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<T,double> || std::is_same_v<T,std::int64_t>)return double(value);else throw std::invalid_argument("Workbook axes must be numeric");},row.parameters.at(key));
}
void Workbook(const BatchResult& result,const std::vector<Ref<const scaps_core::BatchRow>>& rows,const std::string& path) {
    namespace pl=sst::plotlab;const auto table=result.runPlan?result.FileMetadata():result.Schema();
    pl::Project project("ScapsLab batch");const auto book=project.reserveId(),sheet=project.reserveId();
    pl::Worksheet worksheet;worksheet.source["provenance"]=table.at("provenance").dump();worksheet.source["batch_spec"]=table.at("spec").dump();
    for(const auto& r:rows)worksheet.rows.push_back(project.reserveId());
    std::vector<pl::Object> objects={{book,project.root(),"Batch workbook",0,pl::Workbook{}},{sheet,book,"Recorder",0,std::move(worksheet)}};
    auto column=[&](const std::string& name,const std::string& unit,pl::Values values,pl::Role role=pl::Role::None){pl::Column c;c.units=unit;c.values=std::move(values);c.role=role;objects.push_back({project.reserveId(),sheet,name,0,std::move(c)});};
    for(const auto member:{&scaps_core::BatchRow::id,&scaps_core::BatchRow::status,&scaps_core::BatchRow::error_type,&scaps_core::BatchRow::message}) {
        std::vector<std::string> values;for(const auto& r:rows)values.push_back((*r).*member);
        column(member==&scaps_core::BatchRow::id?"id":member==&scaps_core::BatchRow::status?"status":member==&scaps_core::BatchRow::error_type?"error_type":"message","",std::move(values));
    }
    for(const auto& a:table.at("spec").at("axes")){const std::string key=a.at("path");std::vector<double> values;for(const auto& r:rows)values.push_back(ParameterValue(*r,key));column(key,a.value("unit",Unit(key)),std::move(values),pl::Role::X);}
    for(const auto& c:table.at("columns")){const std::string id=c.at("id");std::vector<double> values;std::vector<std::string> statuses;for(const auto& r:rows){values.push_back(r->values.at(id).value);statuses.push_back(r->values.at(id).status);}column(id,c.at("unit"),std::move(values),pl::Role::Y);column(id+" status","",std::move(statuses));}
    auto change=project.change("ScapsLab recorder workbook",std::move(objects));change.apply(project);Save(path,project.toJson());
}

}
std::string ParameterLabel(const std::string &path,const std::string &unit,const Json &value,const Json &before) {
    auto name=path;
    if(unit!="1") (void)Unit(path,&name);
    if(name=="temperature") name="T";
    std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(6)<<name<<' ';
    if(before.is_number()) text<<before.get<double>()<<" \u2192 ";
    text<<value.get<double>();if(unit!="1")text<<' '<<unit;
    return text.str();
}
Json BatchParameters(const Json &document) {
    Json result=Json::array();
    std::function<void(const Json &,std::string,std::string,std::string,std::string)> visit;
    visit=[&](const Json &node,std::string path,std::string stable,std::string pointer,std::string label) {
        if(node.is_object()) {for(auto it=node.begin();it!=node.end();++it) {if(it.key()=="scaps_native" || it.key()=="schema_version" || it.key()=="future_extension")continue;visit(it.value(),path.empty()?it.key():path+"."+it.key(),stable.empty()?it.key():stable+"."+it.key(),pointer+"/"+it.key(),label.empty()?it.key():label+" / "+it.key());}}
        else if(node.is_array()) for(std::size_t i=0;i<node.size();++i) {const auto &child=node[i];const auto id=child.is_object()?child.value("scapslab_id",child.value("id",std::to_string(i))):std::to_string(i);const auto name=child.is_object()?child.value("name",child.value("label",id)):id;visit(child,path+"["+std::to_string(i)+"]",stable+"["+id+"]",pointer+"/"+std::to_string(i),label+" / "+name);}
        else if(node.is_number()) result.push_back({{"id",stable},{"path",path},{"pointer",pointer},{"label",label},{"unit",Unit(path)},{"value",node}});
    };
    visit(document,"","","","");return result;
}
std::vector<NumericParameter> ProjectParameters(const ProjectDraft& draft) {
    std::vector<NumericParameter> result;
    for(const auto& [path,value]:ProjectNumbers(draft.project)) {
        std::string id=path,pointer="/",label;
        for(char c:path){if(c=='.' || c=='[')pointer+='/';else if(c!=']')pointer+=c;}
        if(path.starts_with("layers["))for(std::size_t i=0;i<draft.layers.size();++i) {
            const auto prefix="layers["+std::to_string(i)+"]";if(!path.starts_with(prefix+"."))continue;
            id="layers["+draft.layers[i].id+"]"+path.substr(prefix.size());
            for(std::size_t k=0;k<draft.layers[i].defects.size();++k) {const auto marker=".defects["+std::to_string(k)+"]";if(const auto at=id.find(marker);at!=id.npos)id.replace(at,marker.size(),".defects["+draft.layers[i].defects[k].id+"]");}
            label="layers / "+draft.project.layers[i].name+" / "+path.substr(prefix.size()+1);break;
        }
        if(path.starts_with("interface_defects["))for(std::size_t i=0;i<draft.project.interface_defects.size();++i) {const auto prefix="interface_defects["+std::to_string(i)+"]";if(path.starts_with(prefix+".")){id="interface_defects["+draft.project.interface_defects[i].id+"]"+path.substr(prefix.size());break;}}
        if(label.empty())label=path;
        result.push_back({id,path,pointer,label,Unit(path),value});
    }
    return result;
}
Json BatchParameters(const ProjectDraft& draft) {Json out=Json::array();for(const auto& p:ProjectParameters(draft))out.push_back({{"id",p.id},{"path",p.path},{"pointer",p.pointer},{"label",p.label},{"unit",p.unit},{"value",p.value}});return out;}
Json BatchPreview(const Json& document,const Json& input) {return BatchPreview(*DraftFromDocument(document),input);}
Json QuickSweep(const Json& document,const Json& input) {return QuickSweep(*DraftFromDocument(document),input);}
Json BatchPreview(const ProjectDraft& document,const Json& input) {
    const auto draft=ReadBatchDraft(input);const auto columns=input.contains("columns")?ReadRecorderDraft(input.at("columns")):CreateRef<const RecorderDraft>();return BatchPlanJson(PrepareBatch(document,*draft,*columns));
}
Json QuickSweep(const ProjectDraft& document,const Json& request) {
    auto draft=PrepareQuickSweep(document,request.at("parameter_id"),request.at("delta_native_units"),request.value("points",5),request.value("spacing",std::string("linear")));
    if(request.contains("voltages_V"))draft.voltages_V=request.at("voltages_V").get<std::vector<double>>();if(request.contains("frequencies_Hz"))draft.frequencies_Hz=request.at("frequencies_Hz").get<std::vector<double>>();return BatchPlanJson(PrepareBatch(document,draft,RecorderDraft{})).at("spec");
}
bool BatchOperation(Operation& o,JobContext& ctx) {
    JobExecutor executor{ctx};
    const auto op=o.request.at("operation").get<std::string>();
    if(o.runPlan && (op=="run_batch" || op=="quick_sweep" || op=="retry_batch")) {
        const bool retry=op=="retry_batch";
        if(!o.actionBatch) {
            if(retry && (!o.batchInput || !o.batchInput->projectSnapshot))throw std::invalid_argument("No original batch snapshot");
            if(retry){o.draft=o.batchInput->projectSnapshot;o.recorder=o.batchInput->recorderDraft;}
            auto config=retry?o.batchInput->batchDraft:o.batch;
            if(!config)throw std::invalid_argument("Batch settings are missing");
            if(op=="quick_sweep" && o.request.contains("parameter_id"))config=CreateRef<const BatchDraft>(PrepareQuickSweep(*o.draft,o.request.at("parameter_id"),o.request.at("delta_native_units"),o.request.value("points",5),o.request.value("spacing",std::string("linear"))));
            o.preparedBatch=CreateRef<const BatchPlan>(PrepareBatch(*o.draft,*config,*o.recorder));
            auto table=CreateRef<BatchResult>();table->native=true;table->runPlan=o.runPlan;table->projectSnapshot=o.draft;table->batchDraft=CreateRef<const BatchDraft>(o.preparedBatch->spec);table->recorderDraft=CreateRef<const RecorderDraft>(o.preparedBatch->recorder);
            table->parentRun=o.request.value("source_run",std::string{});
            const auto time=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            table->time_unix_ms=time;table->solverVersion=SCAPSLAB_SOLVER_VERSION;
            o.batchGroup="batch-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            const auto& points=o.preparedBatch->combinations;
            if(retry){table->rows=o.batchInput->rows;table->actionMembers=o.batchInput->actionMembers;}
            else for(std::size_t i=0;i<points.size();++i) {
                auto row=CreateRef<scaps_core::BatchRow>();row->id="point_"+std::to_string(i);row->status="Not started";
                for(const auto& [path,value]:points[i])row->parameters[path]=value;
                for(const auto& c:table->recorderDraft->columns)row->values[c.id]={NAN,"not sampled"};
                table->rows.push_back(std::move(row));table->actionMembers.push_back(CreateRef<const MemberActions>());
            }
            if(retry && std::none_of(table->rows.begin(),table->rows.end(),[&](const auto& row){return row->id==o.request.at("member_id").get<std::string>();}))throw std::invalid_argument("Unknown member_id");
            o.actionBatch=std::move(table);o.typedOut=o.actionBatch;
            if(o.feed){std::lock_guard guard(o.feed->lock);o.feed->memberTotal=points.size();o.feed->batch=o.actionBatch->batchDraft;}
        }
        auto& table=*o.actionBatch;
        const bool parallel=!o.runPlan->pauseAfterAction && !o.runPlan->pauseAfterMember;
        const auto executeMember=[&](std::size_t i,std::size_t& nextAction) {
            // Keep per-point token reference-count traffic local to the serial member solve.
            std::stop_source memberStop;
            std::stop_callback cancel(o.cancellation.get_token(),[&]{memberStop.request_stop();});
            if(retry && table.rows[i]->id!=o.request.at("member_id").get<std::string>())return;
            ctx.ThrowIfCancellationRequested();if(o.cancellation.stop_requested())throw JobCancelledException();
            auto member=CreateRef<MemberActions>(*table.actionMembers[i]);
            auto row=CreateRef<scaps_core::BatchRow>(*table.rows[i]);
            if(retry && nextAction==0){member->previous=table.actionMembers[i];member->results.clear();member->errors.clear();member->recorderSamples.clear();row->attempt++;}
            row->status="Running";table.rows[i]=row;table.actionMembers[i]=member;
            auto snapshot=CreateRef<ProjectDraft>(*table.projectSnapshot);
            for(const auto& [path,value]:o.preparedBatch->combinations[i])SetProjectParameter(snapshot->project,path,value);
            for(const auto& [path,value]:o.preparedBatch->combinations[i])if(path.starts_with("illumination.") && path!="illumination.enabled")snapshot->project.illumination.enabled=true;
            member->illuminated=snapshot->project.illumination.enabled;
            const auto recorderSnapshot=[&](ActionKind kind)->Ref<const ProjectDraft> {const auto& a=o.runPlan->actions[int(kind)];if(a.temperature_K<=0)return snapshot;auto overridden=CreateRef<ProjectDraft>(*snapshot);overridden->project.temperature_K=a.temperature_K;return overridden;};
            for(;nextAction<o.runPlan->actions.size();++nextAction) {
                const auto& a=o.runPlan->actions[nextAction];if(!a.include)continue;
                const auto started=std::chrono::steady_clock::now();
                try {
                    Ref<const ProjectDraft> actionDraft=snapshot;if(a.temperature_K>0){auto overrideDraft=CreateRef<ProjectDraft>(*snapshot);overrideDraft->project.temperature_K=a.temperature_K;actionDraft=std::move(overrideDraft);}
                    ValidateDraft(*actionDraft);
                    Operation child(actionDraft,{{"operation",ActionOperation(a.kind)}});child.action=a;child.deferMetadata=true;child.jsonResponse=false;child.retainProfiles=table.batchDraft->include_profiles;child.cancellation=memberStop;child.parallelMember=parallel;
                    child.refinePv=std::any_of(table.recorderDraft->columns.begin(),table.recorderDraft->columns.end(),[](const auto& c){return c.quantity=="Voc" || c.quantity=="Jsc" || c.quantity=="FF" || c.quantity=="eta" || c.quantity=="Vmp" || c.quantity=="Pmax";});
                    child.measurement=PrepareBatchMember(actionDraft->project,*table.batchDraft,o.preparedBatch->engine);
                    child.Execute(ctx);member->results.emplace_back(a.kind,std::move(child.typedOut));
                } catch(const JobCancelledException&) {table.actionMembers[i]=member;throw;}
                catch(const scaps_core::ExecutionCancelled&) {table.actionMembers[i]=member;throw JobCancelledException();}
                catch(const std::exception& e) {member->errors[a.kind]=e.what();}
                row->elapsed_s+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
                auto recorded=ExtractActions(*member,*table.recorderDraft);row->values=std::move(recorded.values);
                row->photovoltaic=std::move(recorded.photovoltaic);row->photovoltaic_requested=recorded.photovoltaic_requested;
                row->status=!member->errors.empty()?"Failed":recorded.status;
                row->message=member->errors.empty()?"":member->errors.begin()->second;
                row->error_type=member->errors.empty()?"":"action_failed";
                member->summary=row;table.rows[i]=row;table.actionMembers[i]=member;
                if(o.runPlan->pauseAfterAction && std::any_of(o.runPlan->actions.begin()+nextAction+1,o.runPlan->actions.end(),[](const auto& next){return next.include;})) {
                    ++nextAction;o.paused=true;ctx.SetStage(std::string("Paused after ")+ActionId(a.kind)+" / "+row->id);return;
                }
            }
            try { // Recorder settings are explicit sampling requests, including when curves are discarded.
                auto recorded=ExtractActions(*member,*table.recorderDraft);
                for(const auto& column:table.recorderDraft->columns) {
                    const auto& existing=recorded.values.at(column.id);
                    if(existing.status=="ok")continue;
                    RunAction extra;extra.include=true;
                    if(column.quantity=="profile") {
                        if(std::any_of(member->recorderSamples.begin(),member->recorderSamples.end(),[&](const auto& sample){return sample.voltage_V==column.voltage_V;}))continue;
                        const auto source=recorderSnapshot(o.runPlan->actions[0].include?ActionKind::Band:ActionKind::Iv);auto spec=PrepareBatchMember(source->project,*table.batchDraft,scaps_core::BatchSpec{});spec.voltages_V={column.voltage_V};
                        for(const auto& c:table.recorderDraft->columns)if(c.quantity=="profile" && c.voltage_V==column.voltage_V)spec.columns.push_back(c);
                        scaps_core::BatchOptions options;options.include_curves=true;options.execution={std::ref(executor),parallel?1:ctx.GetWorkerCount(),memberStop.get_token()};
                        auto acquired=scaps_core::run_batch_member(source->project,spec,options);
                        if(acquired.status=="Cancelled" || ctx.IsCancellationRequested())throw JobCancelledException();
                        if(acquired.status!="Converged")member->errors[ActionKind::Band]=acquired.message;
                        member->recorderSamples.insert(member->recorderSamples.end(),std::make_move_iterator(acquired.iv.begin()),std::make_move_iterator(acquired.iv.end()));recorded=ExtractActions(*member,*table.recorderDraft);continue;
                    }
                    if(column.quantity=="Voc" || column.quantity=="Jsc" || column.quantity=="FF" || column.quantity=="eta" || column.quantity=="Pmax" || column.quantity=="Vmp") {
                        if(!member->illuminated || row->photovoltaic_requested)continue;
                        const auto source=recorderSnapshot(ActionKind::Iv);auto spec=PrepareBatchMember(source->project,*table.batchDraft,scaps_core::BatchSpec{});spec.voltages_V={0.};spec.columns={column};spec.photovoltaic=true;spec.photovoltaic_voltage_limit_V=table.batchDraft->photovoltaic_voltage_limit_V;spec.incident_power_W_m2=table.batchDraft->incident_power_W_m2;
                        scaps_core::BatchOptions options;options.execution={std::ref(executor),parallel?1:ctx.GetWorkerCount(),memberStop.get_token()};auto acquired=scaps_core::run_batch_member(source->project,spec,options);if(acquired.status=="Cancelled" || ctx.IsCancellationRequested())throw JobCancelledException();row->photovoltaic=std::move(acquired.photovoltaic);row->photovoltaic_requested=acquired.photovoltaic_requested;row->incident_power_W_m2=acquired.incident_power_W_m2;recorded=ExtractActions(*member,*table.recorderDraft);continue;
                    }
                    if(column.quantity=="J"){extra.kind=ActionKind::Band;extra.voltage_V=column.voltage_V;}
                    else if(column.quantity=="C" || column.quantity=="G" || column.quantity=="Vbi") {
                        extra.kind=ActionKind::Cv;extra.frequency_Hz=column.frequency_Hz;
                        if(column.quantity=="Vbi")extra.range=o.runPlan->actions[2].range;
                        else extra.range=SweepRange{SweepSampling::List,false,0,0,1,1,{column.voltage_V}};
                    } else continue;
                    const auto source=recorderSnapshot(extra.kind==ActionKind::Band?ActionKind::Iv:ActionKind::Cv);Operation sample(source,{{"operation",ActionOperation(extra.kind)}});sample.action=extra;sample.deferMetadata=true;sample.jsonResponse=false;sample.retainProfiles=false;sample.cancellation=memberStop;sample.parallelMember=parallel;
                    sample.measurement=PrepareBatchMember(source->project,*table.batchDraft,o.preparedBatch->engine);sample.Execute(ctx);
                    member->results.emplace_back(extra.kind,std::move(sample.typedOut));
                    recorded=ExtractActions(*member,*table.recorderDraft);
                }
                row->values=std::move(recorded.values);row->status=member->errors.empty()?recorded.status:"Failed";
            } catch(const JobCancelledException&) {throw;}
            catch(const scaps_core::ExecutionCancelled&) {throw JobCancelledException();}
            catch(const std::exception& e) {member->errors[ActionKind::Band]=e.what();row->status="Failed";row->error_type="recorder_failed";row->message=e.what();}
            if(o.feed) {
                LiveMember live;live.index=int(i);
                for(const auto& [kind,result]:member->results)if(kind==ActionKind::Iv){const auto& iv=static_cast<const IvResult&>(*result);std::vector<double> v,j;for(const auto& p:iv.curve){v.push_back(p.voltage_V);j.push_back(ValidIvSample(p)?p.J_Am2:NAN);}for(const auto& p:iv.points){v.push_back(p.voltage_V);j.push_back(p.valid?p.J_A_m2:NAN);}live.v=Column::Intern(std::move(v));live.j=Column::Intern(std::move(j));}
                o.feed->AddMember(std::move(live));
            }
            // Recorder has consumed the transient samples; only explicitly requested arrays survive.
            if(!table.batchDraft->include_profiles)member->recorderSamples.clear();
            if(!table.batchDraft->include_curves && !table.batchDraft->include_profiles)member->results.clear();
            else if(!table.batchDraft->include_curves) {
                for(auto& [kind,result]:member->results)if(auto iv=std::dynamic_pointer_cast<const IvResult>(result)){auto kept=std::const_pointer_cast<IvResult>(iv);std::erase_if(kept->points,[](const auto& p){return p.x_m.empty();});}
                std::erase_if(member->results,[](const auto& r){return !std::dynamic_pointer_cast<const IvResult>(r.second);});
            }
            nextAction=0;
            ctx.SetStage("Members "+std::to_string(i+1)+" / "+std::to_string(table.rows.size()));
            if(o.runPlan->pauseAfterMember && i+1<table.rows.size()){o.paused=true;ctx.SetStage("Paused after Member "+row->id);return;}
        };
        const auto runMember=[&](std::size_t i,std::size_t& nextAction) {
            try {executeMember(i,nextAction);}
            catch(const JobCancelledException&) {
                if(table.rows[i]->status!="Not started") {
                    auto row=CreateRef<scaps_core::BatchRow>(*table.rows[i]);row->status="Cancelled";row->error_type="cancelled";row->message="Stopped after completed Actions";
                    auto member=CreateRef<MemberActions>(*table.actionMembers[i]);member->summary=row;table.rows[i]=std::move(row);table.actionMembers[i]=std::move(member);
                }
                throw;
            }
        };
        if(parallel) {
            ParallelFor(ctx,table.rows.size(),[&](std::size_t i){std::size_t cursor=0;runMember(i,cursor);});
            o.nextMember=table.rows.size();
        } else for(;o.nextMember<table.rows.size();++o.nextMember) {
            runMember(o.nextMember,o.nextAction);
            if(o.paused){if(o.nextAction==0)++o.nextMember;return true;}
        }
        double elapsed=0;for(const auto& row:table.rows)elapsed+=row->elapsed_s;
        table.elapsed_s=elapsed;o.outputValid=true;return true;
    }
    if(op=="batch_select"){const int i=o.request.at("index");if(i<0 || !o.batchInput || i>=int(o.batchInput->rows.size()))throw std::invalid_argument("Unknown member index");}
    else if(op=="batch_parameters")o.output=BatchParameters(*o.draft);
    else if(op=="batch_settings") {
        try{o.output=BatchPlanJson(PrepareBatch(*o.draft,*o.batch,*o.recorder));}catch(const std::invalid_argument& e){o.output={{"configuration_error",e.what()}};}
    }
    else if(op=="quick_sweep" || op=="preview_batch" || op=="run_batch" || op=="retry_batch") {
        if(op=="retry_batch" && !o.batchInput)throw std::invalid_argument("No batch to retry");
        auto config=op=="retry_batch"?o.batchInput->batchDraft:o.batch;auto recorder=op=="retry_batch"?o.batchInput->recorderDraft:o.recorder;
        if(!config || !recorder || !o.draft)throw std::invalid_argument("Batch has no typed project/settings snapshot");
        if(op=="quick_sweep" && o.request.contains("parameter_id")) {
            auto quick=PrepareQuickSweep(*o.draft,o.request.at("parameter_id"),o.request.at("delta_native_units"),o.request.value("points",5),o.request.value("spacing",std::string("linear")));
            if(o.request.contains("voltages_V"))quick.voltages_V=o.request.at("voltages_V").get<std::vector<double>>();if(o.request.contains("frequencies_Hz"))quick.frequencies_Hz=o.request.at("frequencies_Hz").get<std::vector<double>>();config=CreateRef<const BatchDraft>(std::move(quick));
        }
        std::optional<BatchPlan> prepared;
        try{prepared=PrepareBatch(*o.draft,*config,*recorder);}catch(const std::invalid_argument& e){if(op!="preview_batch")throw;o.output={{"configuration_error",e.what()}};return true;}
        const auto& plan=*prepared;if(op=="preview_batch"){o.output=BatchPlanJson(plan);return true;}
        const auto spec=BatchPlanJson(plan).at("spec");const auto& points=plan.combinations;
        o.document=DraftDocument(*o.draft); // legacy history/provenance metadata; solver setup stays typed
        auto table=CreateRef<BatchResult>();table->native=true;table->projectSnapshot=o.draft;table->batchDraft=CreateRef<const BatchDraft>(plan.spec);table->recorderDraft=CreateRef<const RecorderDraft>(plan.recorder);
        auto metadata=op=="retry_batch" ? o.batchInput->Schema() : Json{{"schema_version",1},{"spec",spec},{"columns",NormalizedColumns(spec.at("columns"))},{"solver_version",SCAPSLAB_SOLVER_VERSION},{"elapsed_s",0.},{"project_snapshot",o.document},{"provenance",{{"device_hash",DocumentHash(o.document)},{"solver_version",SCAPSLAB_SOLVER_VERSION},{"time_unix_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}}}};
        if(op=="retry_batch")table->rows=o.batchInput->rows;else table->rows.resize(points.size());
        if(op=="retry_batch" && (!o.batchInput->native || o.batchInput->legacyBase)) {
            table->legacyBase=o.batchInput->legacyBase?o.batchInput->legacyBase:o.batchInput;table->replacedRows=o.batchInput->replacedRows;
            table->replacedRows.push_back(o.request.at("member_id").get<std::string>());
        }
        if(op=="retry_batch" && std::none_of(table->rows.begin(),table->rows.end(),[&](const auto& r){return r->id==o.request.at("member_id").get<std::string>();}))throw std::invalid_argument("Unknown member_id");
        const auto parameters=ProjectParameters(*o.draft);auto typedSpec=plan.engine;
        scaps_core::BatchOptions options;options.include_curves=true;options.parallel_member=true;options.execution={std::ref(executor),1,o.cancellation.get_token()};
        options.include_conditioning_profiles=o.jsonResponse;
        if(o.jsonResponse && plan.spec.include_curves)options.profile_biases=typedSpec.voltages_V;
        o.batchGroup="batch-"+std::to_string(metadata.at("provenance").at("time_unix_ms").get<std::int64_t>());
        o.members.resize(points.size());
        std::atomic<std::size_t> finished{0};
        if(o.feed) {std::lock_guard g(o.feed->lock);o.feed->memberTotal=points.size();o.feed->batch=table->batchDraft;}
        const auto wanted=[&](std::size_t i){return op!="retry_batch" || table->rows[i]->id==o.request.at("member_id").get<std::string>();};
        ParallelFor(ctx,points.size(),[&](std::size_t i){
            if(wanted(i)) {
                auto model=o.document;auto row=CreateRef<scaps_core::BatchRow>();
                for(const auto& parameter:parameters) {
                    const auto& path=parameter.path;
                    if(points[i].contains(path) && points[i].at(path)!=parameter.value) {
                        model[Json::json_pointer(parameter.pointer)]=points[i].at(path);
                    }
                }
                try {
                    auto project=o.draft->project;
                    for(const auto& [path,value]:points[i])if(value!=ProjectParameter(project,path))SetProjectParameter(project,path,value);
                    for(const auto& [path,value]:points[i])if(path.starts_with("illumination.") && path!="illumination.enabled")project.illumination.enabled=true;
                    auto memberSpec=PrepareBatchMember(project,plan.spec,typedSpec);
                    std::stop_source memberStop;
                    std::stop_callback cancel(o.cancellation.get_token(),[&]{memberStop.request_stop();});
                    auto memberOptions=options;memberOptions.execution.stop=memberStop.get_token();
                    *row=scaps_core::run_batch_member(project,memberSpec,memberOptions);
                    row->project_hash=scaps_core::batch_project_hash(project);
                }
                catch(const std::exception& e) {row->status="Failed";const auto* typed=dynamic_cast<const scaps_core::DefError*>(&e);row->error_type=typed?typed->code:"invalid_parameter";row->message=e.what();scaps_core::record_batch(*row,typedSpec.columns);}
                row->id="point_"+std::to_string(i);row->attempt=op=="retry_batch"?table->rows[i]->attempt+1:1;
                for(const auto& [key,value]:points[i])row->parameters[key]=value;
                table->rows[i]=row;
                auto& member=o.members[i];member.model=std::move(model);member.hash=DocumentHash(member.model);member.parameters=BatchParameters(member.model);member.t=points.size()<2?0.:double(i)/double(points.size()-1);
                auto memberMeta=metadata;memberMeta["project_snapshot"]=member.model;memberMeta["provenance"]["device_hash"]=member.hash;memberMeta["elapsed_s"]=row->elapsed_s;
                auto memberResult=CreateRef<BatchResult>();memberResult->native=true;memberResult->projectSnapshot=o.draft;memberResult->batchDraft=table->batchDraft;memberResult->recorderDraft=table->recorderDraft;memberResult->body.prim=std::move(memberMeta);memberResult->rows.push_back(row);member.typed=std::move(memberResult);
                member.result={{"provenance",member.typed->Schema().at("provenance")},{"elapsed_s",row->elapsed_s}};
                if(o.feed) {LiveMember m;m.index=int(i);std::vector<double> v,j;for(const auto& p:row->iv){v.push_back(p.voltage_V);j.push_back(p.status==scaps_core::SolverStatus::Converged?p.J_Am2:double(NAN));}m.v=Column::Intern(std::move(v));m.j=Column::Intern(std::move(j));o.feed->AddMember(std::move(m));}
            }
            ctx.SetStage("Members "+std::to_string(++finished)+" / "+std::to_string(points.size()));
        });
        double elapsed=op=="retry_batch"?metadata.value("elapsed_s",0.):0;
        for(std::size_t i=0;i<points.size();++i) {
            if(!table->rows[i]) {
                auto row=CreateRef<scaps_core::BatchRow>();row->id="point_"+std::to_string(i);row->status="Cancelled";row->error_type="cancelled";row->message="Stopped before this member";
                for(const auto& [key,value]:points[i])row->parameters[key]=value;
                for(const auto& c:typedSpec.columns)row->values[c.id]={NAN,"cancelled"};table->rows[i]=std::move(row);
            }
            if(wanted(i))elapsed+=table->rows[i]->elapsed_s;
        }
        std::erase_if(o.members,[](const auto& member){return !member.typed;});
        const bool cancelled=ctx.IsCancellationRequested() || o.cancellation.stop_requested();
        metadata["elapsed_s"]=elapsed;metadata["cancelled"]=cancelled;table->body.prim=std::move(metadata);o.typedOut=table;
        o.output={{"provenance",table->Schema().at("provenance")},{"elapsed_s",elapsed},{"cancelled",cancelled}};
        ctx.SetProgress(float(points.size()),float(points.size()));
        if(cancelled)throw JobCancelledException();
    } else if(op=="record_batch") {
        if(!o.batchInput)throw std::invalid_argument("No current batch results");
        if(o.request.value("calculate_missing",false)) {
            if(!o.batchInput->projectSnapshot || !o.batchInput->batchDraft)throw std::invalid_argument("No original snapshot for Calculate missing");
            Operation child(o.batchInput->projectSnapshot,{{"operation","run_batch"},{"calculate_missing",true},{"source_run",o.request.value("source_run",std::string{})}});
            auto plan=o.batchInput->runPlan?CreateRef<RunPlan>(*o.batchInput->runPlan):CreateRef<RunPlan>();plan->pauseAfterAction=false;plan->pauseAfterMember=false;
            if(!o.batchInput->runPlan && o.batchInput->batchDraft->voltages_V)plan->actions[1].range=SweepRange{SweepSampling::List,false,0,0,1,1,*o.batchInput->batchDraft->voltages_V};
            child.runPlan=std::move(plan);child.batch=o.batchInput->batchDraft;child.recorder=o.recorder;child.cancellation=o.cancellation;
            child.Execute(ctx);o.actionBatch=std::move(child.actionBatch);o.typedOut=o.actionBatch;o.batchGroup=child.batchGroup;o.output=std::move(child.output);return true;
        }
        if(o.batchInput->runPlan) {
            auto table=CreateRef<BatchResult>(*o.batchInput);table->recorderDraft=o.recorder;
            for(std::size_t i=0;i<table->rows.size();++i) {
                ctx.ThrowIfCancellationRequested();auto row=CreateRef<scaps_core::BatchRow>(*table->rows[i]);
                row->values=ExtractActions(*table->actionMembers[i],*o.recorder).values;
                // Curves off retains the original requested metrics even though samples were discarded.
                for(auto& [id,value]:row->values)if(value.status=="not stored")for(const auto& old:o.batchInput->recorderDraft->columns)for(const auto& current:o.recorder->columns)
                    if(id==old.id && id==current.id && old.quantity==current.quantity && old.field==current.field && old.voltage_min_V==current.voltage_min_V && old.voltage_max_V==current.voltage_max_V && (old.voltage_V==current.voltage_V || (!std::isfinite(old.voltage_V)&&!std::isfinite(current.voltage_V))) && (old.frequency_Hz==current.frequency_Hz || (!std::isfinite(old.frequency_Hz)&&!std::isfinite(current.frequency_Hz))) && (old.x_m==current.x_m || (!std::isfinite(old.x_m)&&!std::isfinite(current.x_m))))if(auto found=table->rows[i]->values.find(id);found!=table->rows[i]->values.end())value=found->second;
                auto member=CreateRef<MemberActions>(*table->actionMembers[i]);member->summary=row;table->actionMembers[i]=std::move(member);table->rows[i]=std::move(row);
            }
            o.actionBatch=std::move(table);o.typedOut=o.actionBatch;return true;
        }
        auto table=CreateRef<BatchResult>();table->native=true;auto metadata=o.batchInput->Schema();
        if(!o.batchInput->native || o.batchInput->legacyBase){table->legacyBase=o.batchInput->legacyBase?o.batchInput->legacyBase:o.batchInput;table->replacedRows=o.batchInput->replacedRows;}
        const auto& columns=o.recorder->columns;metadata["columns"]=NormalizedColumns(RecorderDraftJson(*o.recorder));table->projectSnapshot=o.batchInput->projectSnapshot;table->batchDraft=o.batchInput->batchDraft;table->recorderDraft=o.recorder;
        const auto replayDraft=o.batchInput->projectSnapshot;
        for(const auto& source:o.batchInput->rows) {
            ctx.ThrowIfCancellationRequested();
            auto row=CreateRef<scaps_core::BatchRow>(*source);
            scaps_core::record_batch(*row,columns);table->rows.push_back(std::move(row));
        }
        table->body.prim=std::move(metadata);o.typedOut=std::move(table);o.output={{"provenance",o.batchInput->Schema().at("provenance")}};
    }
    else if(op=="export_batch" || op=="send_to_plotlab") {
        if(!o.batchInput)throw std::invalid_argument("No current batch results");
        auto rows=o.batchInput->rows;
        if(o.request.contains("member_id")){const auto id=o.request.at("member_id").get<std::string>();std::erase_if(rows,[&](const auto& row){return row->id!=id;});if(rows.empty())throw std::invalid_argument("Unknown member_id");}
        const auto path=o.request.at("path").get<std::string>();
        if(op=="send_to_plotlab")Workbook(*o.batchInput,rows,path);
        else {
            const auto schema=o.batchInput->runPlan?o.batchInput->FileMetadata():o.batchInput->Schema();std::vector<std::string> paths;for(const auto& axis:schema.at("spec").at("axes"))paths.push_back(axis.at("path"));
            std::vector<scaps_core::RecorderColumn> columns;for(const auto& c:schema.at("columns")){scaps_core::RecorderColumn column;column.id=c.at("id");column.unit=c.at("unit");columns.push_back(std::move(column));}
            scaps_core::BatchTable table;table.rows=rows;Save(path,scaps_core::batch_table_csv(table,paths,columns,schema.at("solver_version")));
        }
        o.output={{"path",path},{"rows",rows.size()},{"export_status","saved"}};
    }

    else return false;
    return true;
}
}
