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
    namespace pl=sst::plotlab;const auto& table=result.Schema();
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
    const auto op=o.request.at("operation").get<std::string>();
    if(op=="batch_select"){const int i=o.request.at("index");if(i<0 || !o.batchInput || i>=int(o.batchInput->rows.size()))throw std::invalid_argument("Unknown member index");}
    else if(op=="batch_parameters")o.output=BatchParameters(*o.draft);
    else if(op=="batch_settings") {
        try{o.output=BatchPlanJson(PrepareBatch(*o.draft,*o.batch,*o.recorder));}catch(const std::invalid_argument& e){o.output={{"configuration_error",e.what()}};}
    }
    else if(op=="quick_sweep" || op=="preview_batch" || op=="run_batch" || op=="retry_batch") {
        if(op=="retry_batch" && !o.batchInput)throw std::invalid_argument("No batch to retry");
        auto config=op=="retry_batch"?o.batchInput->batchDraft:o.batch;auto recorder=op=="retry_batch"?o.batchInput->recorderDraft:o.recorder;
        if(!config || !recorder || !o.draft)throw std::invalid_argument("Batch has no typed project/settings snapshot");
        if(op=="quick_sweep") {
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
        scaps_core::BatchOptions options;options.include_curves=true;options.parallel_member=true;
        options.include_conditioning_profiles=o.jsonResponse;
        if(o.jsonResponse && plan.spec.include_curves)options.profile_biases=typedSpec.voltages_V;
        o.batchGroup="batch-"+std::to_string(metadata.at("provenance").at("time_unix_ms").get<std::int64_t>());
        o.members.resize(points.size());
        std::atomic<std::size_t> finished{0};
        if(o.feed) {std::lock_guard g(o.feed->lock);o.feed->memberTotal=points.size();}
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
                    *row=scaps_core::run_batch_member(project,memberSpec,options);
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
        metadata["elapsed_s"]=elapsed;metadata["cancelled"]=ctx.IsCancellationRequested();table->body.prim=std::move(metadata);o.typedOut=table;
        o.output={{"provenance",table->Schema().at("provenance")},{"elapsed_s",elapsed},{"cancelled",ctx.IsCancellationRequested()}};
        ctx.SetProgress(float(points.size()),float(points.size()));
    } else if(op=="record_batch") {
        if(!o.batchInput)throw std::invalid_argument("No current batch results");
        auto table=CreateRef<BatchResult>();table->native=true;auto metadata=o.batchInput->Schema();
        if(!o.batchInput->native || o.batchInput->legacyBase){table->legacyBase=o.batchInput->legacyBase?o.batchInput->legacyBase:o.batchInput;table->replacedRows=o.batchInput->replacedRows;}
        const auto& columns=o.recorder->columns;metadata["columns"]=NormalizedColumns(RecorderDraftJson(*o.recorder));table->projectSnapshot=o.batchInput->projectSnapshot;table->batchDraft=o.batchInput->batchDraft;table->recorderDraft=o.recorder;
        const auto replayDraft=o.batchInput->projectSnapshot;
        for(const auto& source:o.batchInput->rows) {
            ctx.ThrowIfCancellationRequested();
            if(source->status=="Converged" && !source->curves_retained)throw std::invalid_argument("recording again requires include_curves=true");
            auto row=CreateRef<scaps_core::BatchRow>(*source);
            scaps_core::BatchOptions fetch;fetch.include_curves=true;fetch.parallel_member=true;
            std::vector<scaps_core::RecorderColumn> missing;
            for(const auto& column:columns)if(column.quantity=="profile" && source->status=="Converged")for(const auto& point:source->iv) {
                if(std::abs(point.voltage_V-column.voltage_V)<=8*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(point.voltage_V),std::abs(column.voltage_V)}) && (!point.profiles || !point.profiles->fields.contains(column.field))) {
                    missing.push_back(column);break;
                }
            }
            if(!missing.empty()) {
                if(!replayDraft || !o.batchInput->batchDraft)throw std::invalid_argument("Batch has no typed replay snapshot");
                auto project=replayDraft->project;
                const auto parameters=ProjectNumbers(project);
                for(const auto& [path,value]:source->parameters)if(parameters.contains(path))std::visit([&](const auto& number){if constexpr(std::is_arithmetic_v<std::decay_t<decltype(number)>>)SetProjectParameter(project,path,double(number));else throw std::invalid_argument("Expected numeric batch parameter: "+path);},value);
                const auto& config=*o.batchInput->batchDraft;scaps_core::BatchSpec spec;spec.voltages_V=config.voltages_V.value_or(scaps_core::make_voltage_list(project.voltage));spec.columns=std::move(missing);spec.mesh_coordinates_m=config.mesh_coordinates_m;
                for(const auto& [path,value]:source->parameters)if(path.starts_with("illumination.") && path!="illumination.enabled")project.illumination.enabled=true;
                spec=PrepareBatchMember(project,config,std::move(spec));
                const auto samples=scaps_core::run_batch_member(project,spec,fetch);
                if(samples.status!="Converged")throw std::runtime_error("Profile acquisition failed: "+samples.status+" "+samples.message);
                for(auto& point:row->iv)for(const auto& sample:samples.iv)if(sample.voltage_V==point.voltage_V && sample.profiles) {
                    if(!point.profiles){point.profiles=sample.profiles;continue;}
                    auto profiles=point.profiles?CreateRef<scaps_core::BatchProfile>(*point.profiles):CreateRef<scaps_core::BatchProfile>();profiles->x_m=sample.profiles->x_m;
                    for(const auto& [field,values]:sample.profiles->fields)profiles->fields[field]=values;point.profiles=std::move(profiles);
                }
            }
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
            const auto& schema=o.batchInput->Schema();std::vector<std::string> paths;for(const auto& axis:schema.at("spec").at("axes"))paths.push_back(axis.at("path"));
            std::vector<scaps_core::RecorderColumn> columns;for(const auto& c:schema.at("columns")){scaps_core::RecorderColumn column;column.id=c.at("id");column.unit=c.at("unit");columns.push_back(std::move(column));}
            scaps_core::BatchTable table;table.rows=rows;Save(path,scaps_core::batch_table_csv(table,paths,columns,schema.at("solver_version")));
        }
        o.output={{"path",path},{"rows",rows.size()},{"export_status","saved"}};
    }

    else return false;
    return true;
}
}
