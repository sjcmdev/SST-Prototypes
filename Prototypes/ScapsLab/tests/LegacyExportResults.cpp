// Frozen pre-json2 file exporter: byte-level regression oracle, never linked into the application.
#include "LegacyResultRuns.hpp"
#include "Core/IO/TextFileIO.hpp"
#include "Io.hpp"
#include "Prototypes/PlotLab/src/PlotLabExport.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace SST::ScapsLab {

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
Json LegacyExportResults(const Json &workspace,const Json &request) {
    const auto runs=ResultRuns(workspace);Json selected=Json::array();
    const auto ids=request.value("run_ids",Json::array());
    if(!ids.is_array()) throw std::invalid_argument("run_ids must be an array");
    for(const auto &id:ids) if(!id.is_string() || std::none_of(runs.begin(),runs.end(),[&](const Json &r){return r.at("run_id")==id;})) throw std::invalid_argument("Unknown run_id");
    for(const auto &r:runs) if(request.value("all",false) || std::find(ids.begin(),ids.end(),r.at("run_id"))!=ids.end()) selected.push_back(r);
    if(selected.empty()) throw std::invalid_argument("Select at least one saved run");
    const auto path=request.at("path").get<std::string>(),format=request.value("format",std::string("csv"));
    Json files=Json::array();
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
            std::size_t index=0;
            for(const auto &point:r.at(run.at("kind")=="batch"?"rows":"points")) {
                Json row={{"run_id",run.at("run_id")},{"device_id",run.at("device_id")},{"kind",run.at("kind")},{"point_index",index++},
                    {"project_hash",r.at("provenance").at("device_hash")},{"provenance_json",r.at("provenance")},{"settings_json",snapshot},{"request_json",r.value("request",Json::object())},
                    {"effective_conditions_json",Effective(r,point)},
                    {"point_json",point}};
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
            if(run.at("kind")!="batch") {plots.push_back(run);continue;}
            for(const auto &member:run.at("result").at("rows")) if(member.contains("curves")) {
                auto curve=run;curve["kind"]="iv";curve["run_id"]=run.at("run_id").get<std::string>()+"/"+member.at("id").get<std::string>()+" "+member.at("parameters").dump();curve["result"]["points"]=Json::array();
                for(auto p:member.at("curves").at("iv")) {p["J_A_m2"]=p.at("J_Am2");p["valid"]=p.at("status")=="Converged" && p.at("J_Am2").is_number();curve["result"]["points"].push_back(p);}plots.push_back(std::move(curve));
            }
        }
        if(plots.empty()) throw std::invalid_argument("No retained curves to plot");
        for(const auto &run:plots) {
            const auto kind=run.at("kind").get<std::string>();
            const std::string x=kind=="qe"?"wavelength_nm":kind=="cf"?"frequency_Hz":"voltage_V";
            const std::string y=kind=="qe"?"EQE":kind=="cv" || kind=="cf"?"capacitance_F":"J_A_m2";
            sst::plotlab::GraphSnapshot graph;graph.spec.x.label=x;graph.spec.y.label=y;graph.widthMm=160;graph.heightMm=100;
            sst::plotlab::SnapshotSeries series;series.kind=sst::ui::plot::SeriesKind::Line;series.label=run.at("run_id");
            series.style.color={.12f,.45f,.8f,1.f};series.style.markerFill=series.style.color;series.style.markerOutline=series.style.color;series.style.lineWidthPt=1.5f;series.style.markerSizePt=3;series.style.errorCapPt=3;
            bool finite=false;graph.ranges={0,1,0,1};
            for(const auto &p:run.at("result").at("points")) {
                const bool valid=p.value("valid",false) && p.at(x).is_number() && p.at(y).is_number();
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
    Json output={{"export_status","saved"},{"run_count",selected.size()},{"files",Json::array()}};
    for(const auto &file:files) output["files"].push_back({{"path",file},{"size_bytes",std::filesystem::file_size(file.get<std::string>())}});
    return output;
}
}
