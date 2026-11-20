#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include <scaps_core/batch.hpp>
#include <scaps_core/api.hpp>
#include <sst/plotlab/Engine.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <atomic>
#include <thread>
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
Json SelectedTable(const Json &table,const Json &request) {
    if(table.empty()) throw std::invalid_argument("No current batch results");
    auto selected=table;
    if(request.contains("member_id")) {selected["rows"]=Json::array();for(const auto &r:table.at("rows")) if(r.at("id")==request.at("member_id")) selected["rows"].push_back(r);if(selected["rows"].empty())throw std::invalid_argument("Unknown member_id");}
    return selected;
}
void Save(const std::string &path,const std::string &text) {std::string error;if(!TextFileIO::Save(Path{path},text,error)) throw std::runtime_error(error);}
void Workbook(const Json &table,const std::string &path) {
    namespace pl=sst::plotlab;
    pl::Project project("ScapsLab batch");const auto book=project.reserveId(),sheet=project.reserveId();
    pl::Worksheet worksheet;worksheet.source["provenance"]=table.at("provenance").dump();worksheet.source["batch_spec"]=table.at("spec").dump();
    for(const auto &r:table.at("rows")) worksheet.rows.push_back(project.reserveId());
    std::vector<pl::Object> objects={{book,project.root(),"Batch workbook",0,pl::Workbook{}},{sheet,book,"Recorder",0,worksheet}};
    auto column=[&](const std::string &name,const std::string &unit,pl::Values values,pl::Role role=pl::Role::None) {pl::Column c;c.units=unit;c.values=std::move(values);c.role=role;objects.push_back({project.reserveId(),sheet,name,0,std::move(c)});};
    for(const auto *key:{"id","status","error_type","message"}) {std::vector<std::string> values;for(const auto &r:table.at("rows")) values.push_back(r.value(key,std::string{}));column(key,"",values);}
    for(const auto &a:table.at("spec").at("axes")) {const std::string key=a.at("path");std::vector<double> values;for(const auto &r:table.at("rows")) values.push_back(r.at("parameters").at(key).get<double>());column(key,a.value("unit",Unit(key)),values,pl::Role::X);}
    for(const auto &c:table.at("columns")) {const std::string id=c.at("id");std::vector<double> values;std::vector<std::string> statuses;for(const auto &r:table.at("rows")) {const auto &v=r.at("values").at(id);values.push_back(v.is_number()?v.get<double>():NAN);statuses.push_back(r.at("statuses").at(id));}column(id,c.at("unit"),values,pl::Role::Y);column(id+" status","",statuses);}
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
Json BatchPreview(const Json &document,const Json &input) {
    auto spec=input;spec["include_curves"]=true;spec["threads"]=1;
    const auto mode=spec.value("mode",std::string("nested"));if(mode!="nested" && mode!="together")throw std::invalid_argument("Mode must be nested or together");
    if(!spec.contains("axes") || !spec.at("axes").is_array() || spec.at("axes").empty() || spec.at("axes").size()>2) throw std::invalid_argument("Select one or two axes");
    const auto parameters=BatchParameters(document);Json combinations=Json::array({Json::object()});std::set<std::string> seen;
    for(auto &axis:spec["axes"]) {
        const auto found=std::find_if(parameters.begin(),parameters.end(),[&](const Json &p){return axis.contains("parameter_id")?p.at("id")==axis.at("parameter_id"):p.at("path")==axis.at("path");});
        if(found==parameters.end())throw std::invalid_argument("Unknown numeric parameter; select it from the project tree");
        const auto path=found->at("path").get<std::string>();if(!seen.insert(path).second)throw std::invalid_argument("Axes must use different parameters");
        axis["path"]=path;axis["parameter_id"]=found->at("id");axis["unit"]=found->at("unit");
        Json values=Json::array();
        if(axis.contains("values")) values=axis.at("values");
        else {if(!axis.at("count").is_number_integer())throw std::invalid_argument("Member count must be an integer");const auto n=axis.at("count").get<int>();const double a=axis.at("start"),b=axis.at("stop");const auto spacing=axis.value("spacing",std::string("linear"));if(n<1 || n>10000 || !std::isfinite(a) || !std::isfinite(b) || (spacing!="linear" && spacing!="log") || (spacing=="log" && (a<=0 || b<=0)))throw std::invalid_argument("Invalid axis range/count/spacing");for(int i=0;i<n;++i) {const double t=n==1?0:double(i)/(n-1);values.push_back(i==0?a:i==n-1?b:spacing=="log"?std::exp(std::lerp(std::log(a),std::log(b),t)):std::lerp(a,b,t));}}
        if(!values.is_array() || values.empty() || values.size()>10000)throw std::invalid_argument("Axis requires 1..10000 values");
        for(const auto &v:values) if(!v.is_number() || !std::isfinite(v.get<double>()))throw std::invalid_argument("Axis values must be finite numbers");
        if(mode=="together") {if(combinations.size()==1 && combinations[0].empty())combinations=Json::array();if(combinations.empty())for(const auto &v:values)combinations.push_back(Json::object());if(combinations.size()!=values.size())throw std::invalid_argument("Together axes require equal counts");for(std::size_t i=0;i<values.size();++i)combinations[i][path]=values[i];}
        else {if(combinations.size()>10000/values.size())throw std::invalid_argument("Maximum 10000 members");auto expanded=Json::array();for(const auto &c:combinations)for(const auto &v:values) {auto p=c;p[path]=v;expanded.push_back(p);}combinations=std::move(expanded);}
    }
    auto volts=spec.value("voltages_V",Json::array());if(!spec.contains("voltages_V")) {const auto p=Model(document);for(double v:scaps_core::make_voltage_list(p.voltage))volts.push_back(v);}
    if(!volts.is_array() || volts.empty() || volts.size()>10001)throw std::invalid_argument("Voltage sampling requires 1..10001 points");
    for(const auto &v:volts)if(!v.is_number() || !std::isfinite(v.get<double>()))throw std::invalid_argument("Invalid voltage sample");
    auto frequencies=spec.value("frequencies_Hz",Json::array());if(!frequencies.is_array() || frequencies.size()>1000)throw std::invalid_argument("Invalid frequency sampling");for(const auto &f:frequencies)if(!f.is_number() || !std::isfinite(f.get<double>()) || f.get<double>()<=0)throw std::invalid_argument("Frequency must be positive");
    spec["columns"]=spec.value("columns",RecorderDefaults());
    (void)scaps_core::record_batch_json(Json{{"rows",Json::array()}}.dump(),spec["columns"].dump());
    // Acquire requested Recorder samples inside the declared range; never extrapolate.
    const auto [low,high]=std::minmax_element(volts.begin(),volts.end());const double first=low->get<double>(),last=high->get<double>();
    for(const auto &column:spec["columns"]) {
        if(column.contains("voltage_V")) {const double v=column.at("voltage_V");if(v>=first && v<=last && std::find(volts.begin(),volts.end(),v)==volts.end()) for(std::size_t i=1;i<volts.size();++i) {const double a=volts[i-1],b=volts[i];if(v>=std::min(a,b) && v<=std::max(a,b)) {volts.insert(volts.begin()+i,v);break;}}}
        if(column.contains("frequency_Hz") && std::find(frequencies.begin(),frequencies.end(),column.at("frequency_Hz"))==frequencies.end()) frequencies.push_back(column.at("frequency_Hz"));
    }
    if(volts.size()>10001 || frequencies.size()>1000)throw std::invalid_argument("Recorder samples exceed batch sampling limits");
    spec["voltages_V"]=volts;spec["frequencies_Hz"]=frequencies;
    return {{"spec",spec},{"combinations",combinations},{"member_count",combinations.size()},{"bias_count",volts.size()},{"frequency_count",frequencies.size()},{"sample_count",combinations.size()*volts.size()*(1+frequencies.size())},{"voltages_V",volts},{"frequencies_Hz",frequencies},{"time_estimate_s",nullptr},{"time_estimate_status","unavailable before first run"}};
}
Json QuickSweep(const Json &document,const Json &request) {
    const auto parameters=BatchParameters(document);
    auto parameter=std::find_if(parameters.begin(),parameters.end(),[&](const Json &p){return p.at("id")==request.at("parameter_id");});
    if(parameter==parameters.end())throw std::invalid_argument("Unknown parameter_id");
    const double centre=parameter->at("value"),delta=request.at("delta_native_units");
    if(!std::isfinite(delta) || delta<=0)throw std::invalid_argument("delta must be positive, in the parameter's native unit");
    auto spec=BatchDefaults();spec["axes"]=Json::array({{{"parameter_id",request.at("parameter_id")},{"start",centre-delta},{"stop",centre+delta},{"count",request.value("points",5)},{"spacing",request.value("spacing",std::string("linear"))}}});
    if(request.contains("voltages_V"))spec["voltages_V"]=request.at("voltages_V");
    if(request.contains("frequencies_Hz"))spec["frequencies_Hz"]=request.at("frequencies_Hz");
    return BatchPreview(document,spec).at("spec");
}
bool BatchOperation(Operation &o,JobContext &ctx) {
    const auto op=o.request.at("operation").get<std::string>();
    if(op=="batch_select") {const int i=o.request.at("index");if(i<0 || o.batchResult.empty() || i>=int(o.batchResult.at("rows").size()))throw std::invalid_argument("Unknown member index");}
    else if(op=="batch_parameters") o.output=BatchParameters(o.document);
    else if(op=="batch_settings") {
        o.batchSpec=o.request.value("spec",o.batchSpec);o.recorderColumns=o.request.value("columns",o.recorderColumns);
        const auto &axes=o.batchSpec.at("axes");
        if(!o.batchSpec.is_object() || !axes.is_array() || axes.empty() || axes.size()>2)throw std::invalid_argument("Settings require one or two axis objects");
        for(const auto &axis:axes) {
            if(!axis.is_object() || !axis.at("parameter_id").is_string())throw std::invalid_argument("Select a stable parameter_id from the project tree");
            if(axis.contains("values")) {if(!axis.at("values").is_array())throw std::invalid_argument("Values must be a numeric list");for(const auto &v:axis.at("values"))if(!v.is_number() || !std::isfinite(v.get<double>()))throw std::invalid_argument("Values must be finite numbers");}
            else if(!axis.at("start").is_number() || !axis.at("stop").is_number() || !axis.at("count").is_number_integer())throw std::invalid_argument("Range requires numeric start/stop and integer count");
        }
        (void)scaps_core::record_batch_json("{\"rows\":[]}",o.recorderColumns.dump());
        auto spec=o.batchSpec;spec["columns"]=o.recorderColumns;
        try {o.output=BatchPreview(o.document,spec);}catch(const std::invalid_argument &e) {o.output={{"configuration_error",e.what()}};}
    }
    else if(op=="quick_sweep" || op=="preview_batch" || op=="run_batch" || op=="retry_batch") {
        if(op=="retry_batch" && o.batchResult.empty())throw std::invalid_argument("No batch to retry");
        auto spec=op=="quick_sweep"?QuickSweep(o.document,o.request):op=="retry_batch"?o.batchResult.at("spec"):o.request.value("spec",o.batchSpec);
        spec["columns"]=op=="retry_batch"?o.batchResult.at("columns"):o.request.value("columns",o.recorderColumns);
        const auto plan=BatchPreview(o.document,spec);spec=plan.at("spec");
        if(op=="preview_batch") {o.output=plan;return true;}
        const auto &points=plan.at("combinations");
        o.output=op=="retry_batch"?o.batchResult:Json{{"schema_version",1},{"spec",spec},{"columns",Json::parse(scaps_core::record_batch_json("{\"rows\":[]}",spec.at("columns").dump())).at("columns")},{"rows",Json::array()},{"solver_version",SCAPSLAB_SOLVER_VERSION},{"elapsed_s",0.},{"project_snapshot",o.document},{"provenance",{{"device_hash",DocumentHash(o.document)},{"solver_version",SCAPSLAB_SOLVER_VERSION},{"time_unix_ms",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()}}}};
        // Members are independent: solve them on a small thread pool, then merge rows in order (progress/cancel via the main thread).
        std::vector<Json> solved(points.size());std::vector<char> ready(points.size(),0);
        auto wanted=[&](std::size_t i){return !(op=="retry_batch" && o.output["rows"][i].at("id")!=o.request.at("member_id"));};
        std::atomic<std::size_t> next{0},finished{0};std::atomic<bool> stop{false};std::exception_ptr failure;std::mutex failureLock;
        auto work=[&]{
            for(std::size_t i;(i=next.fetch_add(1))<points.size();) {
                if(!wanted(i)) {++finished;continue;}
                if(stop) {++finished;continue;}
                try {auto single=spec;for(auto &axis:single["axes"]) {axis["values"]=Json::array({points[i].at(axis.at("path").get<std::string>())});axis.erase("count");axis.erase("start");axis.erase("stop");}
                    solved[i]=Json::parse(scaps_core::run_batch_json(o.document.dump(),single.dump())).at("rows")[0];ready[i]=1;}
                catch(...) {std::lock_guard g(failureLock);if(!failure)failure=std::current_exception();stop=true;}
                ++finished;
            }
        };
        {
            std::vector<std::thread> pool;const auto threads=std::min<std::size_t>(points.size(),std::max(1u,std::thread::hardware_concurrency()));
            for(std::size_t t=0;t<threads;++t) pool.emplace_back(work);
            while(finished<points.size()) {if(ctx.IsCancellationRequested())stop=true;ctx.SetStage("Members "+std::to_string(finished.load())+" / "+std::to_string(points.size()));ctx.SetProgress(float(finished.load()),float(points.size()));std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            for(auto &t:pool) t.join();
        }
        if(failure) std::rethrow_exception(failure);
        for(std::size_t i=0;i<points.size();++i) {
            if(!wanted(i))continue;
            Json row;
            if(!ready[i]) {row={{"parameters",points[i]},{"status","Cancelled"},{"error_type","cancelled"},{"message","Stopped before this member"},{"elapsed_s",0.},{"values",Json::object()},{"statuses",Json::object()}};for(const auto &c:o.output.at("columns")) {row["values"][c.at("id").get<std::string>()]=nullptr;row["statuses"][c.at("id").get<std::string>()]="cancelled";}}
            else {row=std::move(solved[i]);o.output["elapsed_s"]=o.output.at("elapsed_s").get<double>()+row.at("elapsed_s").get<double>();}
            row["id"]="point_"+std::to_string(i);row["attempt"]=op=="retry_batch"?o.output["rows"][i].value("attempt",1)+1:1;
            if(op=="retry_batch")o.output["rows"][i]=row;else o.output["rows"].push_back(row);
        }
        ctx.SetProgress(float(points.size()),float(points.size()));
        if(op=="retry_batch" && std::none_of(o.output.at("rows").begin(),o.output.at("rows").end(),[&](const Json &r){return r.at("id")==o.request.at("member_id");}))throw std::invalid_argument("Unknown member_id");
        o.output["cancelled"]=ctx.IsCancellationRequested();
    } else if(op=="record_batch") {auto table=SelectedTable(o.batchResult,Json::object());o.output=Json::parse(scaps_core::record_batch_json(table.dump(),o.request.value("columns",o.recorderColumns).dump()));}
    else if(op=="export_batch" || op=="send_to_plotlab") {const auto table=SelectedTable(o.batchResult,o.request);const auto path=o.request.at("path").get<std::string>();if(op=="send_to_plotlab")Workbook(table,path);else Save(path,scaps_core::batch_table_csv(table.dump()));o.output={{"path",path},{"rows",table.at("rows").size()},{"export_status","saved"}};}
    else return false;
    return true;
}
}
