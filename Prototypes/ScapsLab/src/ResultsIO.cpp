#include "Workflow.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Io.hpp"
#include "Prototypes/PlotLab/src/PlotLabExport.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace SST::ScapsLab {
std::vector<ResultRun> Workflow::ResultRuns() const {
    std::vector<ResultRun> runs;
    for(const auto& device:project.devices) {
        const auto& d=*device;
        const auto live=[&](const std::string& kind)->Ref<const ResultBase> {
            Ref<const ResultBase> result=kind=="iv"?std::static_pointer_cast<const ResultBase>(d.result.typed()):kind=="band"?std::static_pointer_cast<const ResultBase>(d.bandResult.typed()):std::static_pointer_cast<const ResultBase>(d.acResults.get(kind));
            return result && !result->liveOnly?result:nullptr;
        };
        const auto time=[](const ResultBase& result)->std::int64_t {
            if(const auto* batch=dynamic_cast<const BatchResult*>(&result);batch && batch->runPlan)return batch->time_unix_ms;
            return result.Provenance().at("time_unix_ms").get<std::int64_t>();
        };
        for(const auto& run:d.history) {
            const auto kind=run.at("kind").get<std::string>();
            const auto stored=d.RunResult(run.at("run_id").get<std::string>());
            const auto* storedBatch=dynamic_cast<const BatchResult*>(stored.get());
            if(kind=="batch" && d.batchResult.typed()) {
                const auto timestamp=storedBatch && storedBatch->runPlan?time(*stored):run.at("provenance").at("time_unix_ms").get<std::int64_t>();
                if(timestamp==time(*d.batchResult.typed()))continue;
            }
            auto result=live(kind);
            if(!result || result->Provenance()!=run.at("provenance"))result=stored;
            runs.push_back({run.at("run_id"),d.id,d.name,kind,std::move(result)});
        }
        const auto add=[&](const std::string& kind,Ref<const ResultBase> result) {
            if(!result)return;
            for(const auto& run:d.history)if(kind!="batch" && run.at("kind")==kind && run.at("provenance")==result->Provenance())return;
            runs.push_back({d.id+"/"+kind+"/"+std::to_string(time(*result)),d.id,d.name,kind,std::move(result)});
        };
        add("iv",live("iv"));add("band",live("band"));
        for(const auto& [kind,result]:d.acResults.all())add(kind,live(kind));
        add("batch",d.batchResult.typed());
    }
    return runs;
}
Json Workflow::Runs() const {
    Json runs=Json::array();
    for(const auto& run:ResultRuns())runs.push_back({{"run_id",run.runId},{"device_id",run.deviceId},{"device_name",run.deviceName},{"kind",run.kind}});
    return runs;
}

namespace {
Json Effective(const Json &r,const Json &point) {
    const auto &snapshot=r.at("project_snapshot");
    Json out={{"temperature_K",point.value("temperature_K",snapshot.at("temperature_K"))},{"illumination",snapshot.value("illumination",Json::object())}};
    if(r.value("kind",std::string{})=="qe") {out["illumination"]["enabled"]=false;out["background"]="dark";out["probe_mode"]=r.at("probe_mode");out["probe_request"]=r.at("request");}
    for(const auto *key:{"frequency_Hz","voltage_V","internal_voltage_V","terminal_voltage_V"}) if(point.contains(key))out[key]=point.at(key);
    return out;
}
std::string Csv(const Json &value) {
    if(value.is_null()) return "";
    if(!value.is_string()) return value.is_primitive()?value.dump():Csv(Json(value.dump()));
    std::string out="\"";for(char c:value.get<std::string>()) {out+=c;if(c=='"') out+='"';}return out+'"';
}
void Save(const std::string &path,const std::string &text) {
    std::string error;if(!Io::Save(path,text,error)) throw std::runtime_error(error);
}
}
std::vector<std::filesystem::path> ExportResults(const std::vector<ResultRun>& runs,const std::filesystem::path& destination,const std::string& format) {
    if(runs.empty())throw std::invalid_argument("Select at least one saved run");
    // File edge: materialize only selected immutable results, on the IO worker.
    Json selected=Json::array();
    for(const auto& run:runs)selected.push_back({{"run_id",run.runId},{"device_id",run.deviceId},{"device_name",run.deviceName},{"kind",run.kind},{"result",run.result?run.result->Dump():Json::object()}});
    const auto path=destination.string();
    std::vector<std::filesystem::path> files;
    if(format=="json") {
        for(auto &run:selected) if(run.at("kind")!="batch") {
            run["effective_conditions"]=Effective(run.at("result"),run.at("result").at("points").front());
            run["voltage_columns"]={{"terminal_voltage_V",Json::array()},{"internal_voltage_V",Json::array()}};
            for(const auto &p:run.at("result").at("points")) {
                run["voltage_columns"]["terminal_voltage_V"].push_back(p.value("terminal_voltage_V",p.value("voltage_V",Json(nullptr))));
                run["voltage_columns"]["internal_voltage_V"].push_back(p.value("internal_voltage_V",Json(nullptr)));
            }
        }
        Save(path,WorkspaceText(PackResults(Json{{"schema_version",1},{"runs",selected}})));files.push_back(path);
    } else if(format=="csv") {
        // One rectangular sheet; structured columns retain profiles and per-cell diagnostics losslessly.
        Json rows=Json::array();std::set<std::string> columns;
        for(const auto &run:selected) {
            const auto &r=run.at("result"),&snapshot=r.at("project_snapshot");
            if(r.contains("dlts")) {
                for(const auto& trace:r.at("dlts").at("traces"))for(std::size_t i=0;i<trace.at("time_s").size();++i) {
                    Json row={{"run_id",run.at("run_id")},{"device_id",run.at("device_id")},{"kind",run.at("kind")},{"trace_id",trace.at("id")},{"time_s",trace.at("time_s")[i]},{"capacitance_F",trace.at("capacitance_F")[i]},{"current_A_m2",trace.at("current_A_m2")[i]},{"run_plan_json",r.at("run_plan")},{"provenance_json",r.at("provenance")}};
                    for(const auto* key:{"temperature_K","pulse_width_s","reverse_V","pulse_V","readout_resolution_F","baseline_F","equilibrium_F","density_valid","depth_m","pulse_depth_m","trap_density_m3","status","message"})row[key]=trace.at(key);
                    if(!trace.at("reference_capacitance_F").empty())row["reference_capacitance_F"]=trace.at("reference_capacitance_F")[i];
                    if(i==0){row["signal_F"]=trace.at("signal_F");row["amplitude_F"]=trace.at("amplitude_F");}
                    if(i==0 && trace.contains("laplace"))row["laplace_json"]=trace.at("laplace");
                    if(i==0 && r.at("dlts").contains("arrhenius"))row["arrhenius_json"]=r.at("dlts").at("arrhenius");
                    if(i==0 && r.at("dlts").contains("capture"))row["capture_json"]=r.at("dlts").at("capture");
                    for(const auto& [key,value]:row.items())columns.insert(key);rows.push_back(std::move(row));
                }
                continue;
            }
            std::size_t index=0;
            for(const auto &point:r.at(run.at("kind")=="batch"?"rows":run.at("kind")=="dlcp"?"dlcp_points":"points")) {
                Json row={{"run_id",run.at("run_id")},{"device_id",run.at("device_id")},{"kind",run.at("kind")},{"point_index",index++},
                    {"project_hash",r.at("provenance").at("device_hash")},{"provenance_json",r.at("provenance")},{"settings_json",snapshot},{"request_json",r.value("request",Json::object())},
                    {"effective_conditions_json",Effective(r,point)},
                    {"point_json",point}};
                if(run.at("kind")=="dlcp"){row["frequency_Hz"]=r.at("points").front().at("frequency_Hz");row["temperature_K"]=r.at("points").front().at("temperature_K");row["profiling_eps_r"]=r.at("profiling_eps_r");}
                for(auto field=point.begin();field!=point.end();++field) if(field->is_primitive()) row[field.key()]=field.value();
                row["terminal_voltage_V"]=point.value("terminal_voltage_V",point.value("voltage_V",Json(nullptr)));
                row["internal_voltage_V"]=point.value("internal_voltage_V",Json(nullptr));
                if(run.at("kind")=="batch") {
                    row["batch_spec_json"]=r.at("spec");row["effective_conditions_json"]["parameters"]=point.at("parameters");
                    if(point.at("parameters").contains("temperature_K"))row["effective_conditions_json"]["temperature_K"]=point.at("parameters").at("temperature_K");
                    for(const auto *key:{"parameters","values","statuses"}) for(auto f=point.at(key).begin();f!=point.at(key).end();++f) row[std::string(key)+"/"+f.key()]=f.value();
                    for(const auto &c:r.at("columns")) {const auto id=c.at("id").get<std::string>();row[id+" ["+c.at("unit").get<std::string>()+"]"]=point.at("values").at(id);}
                }
                for(auto f=row.begin();f!=row.end();++f) columns.insert(f.key());rows.push_back(std::move(row));
            }
        }
        std::ostringstream csv;bool first=true;for(const auto &key:columns) {if(!first)csv<<',';first=false;csv<<Csv(Json(key));}csv<<'\n';
        for(const auto &row:rows) {first=true;for(const auto &key:columns) {if(!first)csv<<',';first=false;csv<<Csv(row.value(key,Json(nullptr)));}csv<<'\n';}
        Save(path,csv.str());files.push_back(path);
    } else if(format=="png" || format=="svg" || format=="pdf") {
        Json plots=Json::array();
        for(const auto &run:selected) {
            if(run.at("result").contains("dlts")) {
                const auto& result=run.at("result");
                const auto add=[&](const std::string& name,const std::string& x,const std::string& y,const Json& xs,const Json& ys){auto plot=run;plot["kind"]="dlts_plot";plot["run_id"]=run.at("run_id").get<std::string>()+"/"+name;plot["x_label"]=x;plot["y_label"]=y;plot["result"]["points"]=Json::array();for(std::size_t i=0;i<xs.size() && i<ys.size();++i)plot["result"]["points"].push_back({{"dlts_x",xs[i]},{"dlts_y",ys[i]},{"valid",true}});plots.push_back(std::move(plot));};
                for(const auto& trace:result.at("dlts").at("traces"))if(trace.at("status")=="converged") {
                    add(trace.at("id"),"Time after filling [s]","C [F]",trace.at("time_s"),trace.at("capacitance_F"));
                    if(trace.contains("laplace")){const auto& l=trace.at("laplace");add(trace.at("id").get<std::string>()+"-Laplace","Rate [s^-1]","Amplitude / log(rate) [F]",l.at("rate_per_s"),l.at("density_F_per_log_rate"));}
                }
                for(const auto& spectrum:result.at("dlts").at("spectra"))add(spectrum.at("id"),"Temperature [K]","Signal [F]",spectrum.at("temperature_K"),spectrum.at("signal_F"));
                continue;
            }
            if(run.at("kind")!="batch") {plots.push_back(run);continue;}
            for(const auto &member:run.at("result").at("rows")) if(member.contains("curves")) {
                auto curve=run;curve["kind"]="iv";curve["run_id"]=run.at("run_id").get<std::string>()+"/"+member.at("id").get<std::string>()+" "+member.at("parameters").dump();curve["result"]["points"]=Json::array();
                for(auto p:member.at("curves").at("iv")) {p["J_A_m2"]=p.at("J_Am2");p["valid"]=p.at("status")=="Converged" && p.at("J_Am2").is_number();curve["result"]["points"].push_back(p);}plots.push_back(std::move(curve));
            }
        }
        if(plots.empty()) throw std::invalid_argument("No retained curves to plot");
        for(const auto &run:plots) {
            const auto kind=run.at("kind").get<std::string>();
            const std::string x=kind=="dlts_plot"?"dlts_x":kind=="dlcp"?"x_m":kind=="qe"?"wavelength_nm":kind=="cf"?"frequency_Hz":"voltage_V";
            const std::string y=kind=="dlts_plot"?"dlts_y":kind=="dlcp"?"N_DL_m3":kind=="qe"?"EQE":kind=="cv" || kind=="cf"?"capacitance_F":"J_A_m2";
            sst::plotlab::GraphSnapshot graph;graph.spec.x.label=run.value("x_label",x);graph.spec.y.label=run.value("y_label",y);graph.widthMm=160;graph.heightMm=100;
            sst::plotlab::SnapshotSeries series;series.kind=sst::ui::plot::SeriesKind::Line;series.label=run.at("run_id");
            series.style.color={.12f,.45f,.8f,1.f};series.style.markerFill=series.style.color;series.style.markerOutline=series.style.color;series.style.lineWidthPt=1.5f;series.style.markerSizePt=3;series.style.errorCapPt=3;
            bool finite=false;graph.ranges={0,1,0,1};
            for(const auto &p:run.at("result").at(kind=="dlcp"?"dlcp_points":"points")) {
                const bool valid=(kind=="dlcp"?p.at("status")=="converged":p.value("valid",false)) && p.at(x).is_number() && p.at(y).is_number();
                const double a=p.at(x).is_number()?p.at(x).get<double>():NAN,b=valid?p.at(y).get<double>():NAN;
                series.x.push_back(a);series.y.push_back(b);
                if(std::isfinite(a) && std::isfinite(b)) {if(!finite)graph.ranges={a,a,b,b};finite=true;graph.ranges.minX=std::min(graph.ranges.minX,a);graph.ranges.maxX=std::max(graph.ranges.maxX,a);graph.ranges.minY=std::min(graph.ranges.minY,b);graph.ranges.maxY=std::max(graph.ranges.maxY,b);}
            }
            if(!finite) throw std::invalid_argument("No valid points to plot");
            auto pad=[](double &lo,double &hi){const double d=hi>lo?(hi-lo)*.05:std::max(1e-12,std::abs(lo)*.05);lo-=d;hi+=d;};pad(graph.ranges.minX,graph.ranges.maxX);pad(graph.ranges.minY,graph.ranges.maxY);
            graph.series.push_back(std::move(series));
            auto destination=std::filesystem::path(path);
            if(plots.size()>1) destination=destination.parent_path()/(destination.stem().string()+"-"+std::to_string(files.size()+1)+"."+format);
            Prototypes::PlotLab::ExportGraph(graph,destination,format,300,SCAPSLAB_PLOTLAB_PYTHON);
            files.push_back(destination.string());
        }
    } else throw std::invalid_argument("format must be csv, json, png, svg or pdf");
    return files;
}
}
