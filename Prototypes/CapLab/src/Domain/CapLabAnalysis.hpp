#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Domain/CapLabPlotStyle.hpp"
namespace SST::Prototypes::CapLab {
inline pl::Id AnalysisSample(const Json &a) {
    if (a.contains("sample_id")) return a.at("sample_id");
    if (a.contains("defect")) return a.at("defect").at("sample_id");
    return a.at("inputs").at(0).at("sample_id");
}
inline AnalysisMeasurementData AnalysisMeasurement(const Project &p, pl::Id id, bool includeData = true) {
    AnalysisMeasurementData result;
    std::vector<pl::Id> columns;
    for (const auto &m:p.measurements) if (m.at("id")==id) {
        result.metadata=m.at("metadata"); result.metadata["id"]=id;
        result.metadata["sample_id"]=m.at("sample_id");
        columns=m.at("columns").get<std::vector<pl::Id>>(); break;
    }
    if (result.empty()) for (const auto &m:p.sweeps) if(m.id==id) {
        result.metadata={{"id",id},{"sample_id",m.sample_id},{"kind","FrequencySweep"},{"variant","Cp_Rp"},
            {"temperature_K",Nullable(m.temperature_K)},{"names",{"f","C","Rp"}},{"units",{"Hz","F","Ohm"}}};
        if(includeData)result.metadata["source_rows"]=p.sources.at(id).rows;
        columns={m.frequency,m.capacitance,m.resistance};break;
    }
    if (result.empty()) throw std::runtime_error("Missing analysis measurement.");
    for (const auto &sample:p.samples) if(result.metadata.at("sample_id")==sample.id) {
        result.metadata["area_m2"]=Nullable(sample.area_m2);result.metadata["sample_metadata"]=sample.metadata;
    }
    if(includeData) {
        const auto owner=p.engine->objects().at(id);
        result.rowIds=std::shared_ptr<const std::vector<pl::Id>>(owner,&std::get<pl::Worksheet>(owner->data).rows);
        for(auto cid:columns) {
            const auto column=p.engine->objects().at(cid);
            result.columns.emplace_back(column,&std::get<std::vector<double>>(std::get<pl::Column>(column->data).values));
        }
    }
    return result;
}
inline bool SameAnalysisMeasurement(const Project &p, const AnalysisMeasurementData &input, bool &sameSample) {
    const auto current=AnalysisMeasurement(p,input.metadata.at("id").get<pl::Id>());
    sameSample=current.metadata.at("sample_id")==input.metadata.at("sample_id");
    if (current.metadata!=input.metadata || !input.rowIds || *current.rowIds!=*input.rowIds || current.columns.size()!=input.columns.size()) return false;
    for(std::size_t i=0;i<current.columns.size();++i) if(*current.columns[i]!=*input.columns[i])return false;
    return true;
}
inline void EditDefect(Project &p, const Json &edit) {
    const std::string action=edit.at("action");
    if (action=="create") {
        const auto sample=edit.at("sample_id").get<pl::Id>();
        if (std::none_of(p.samples.begin(),p.samples.end(),[&](const auto &s) {return s.id==sample;})) throw std::runtime_error("Missing sample.");
        p.defects.push_back({{"id",p.engine->reserveId()},{"sample_id",sample},{"name",edit.at("name")},{"points",Json::array()}});
    } else {
        auto it=std::find_if(p.defects.begin(),p.defects.end(),[&](const auto &d) { return d.at("id")==edit.at("defect_id"); });
        if (it==p.defects.end()) throw std::runtime_error("Missing defect.");
        if (action=="delete") p.defects.erase(it);
        else if (action=="rename") (*it)["name"]=edit.at("name");
        else if (action=="add") {
            auto point=edit.at("point");
            if (point.contains("analysis_id")) {
                const auto source=std::find_if(p.analyses.begin(),p.analyses.end(),[&](const auto &a) {return a.at("id")==point.at("analysis_id");});
                if (source==p.analyses.end() || source->at("type")!="AS" || source->at("status")!="valid") throw std::runtime_error("Stale/missing AS candidate; recalculate AS.");
                bool found=false;
                for (const auto &c : source->at("candidates")) found |= c.at("measurement_id")==point.at("measurement_id") && c.at("row_id")==point.at("row_id");
                if (!found) throw std::runtime_error("Unknown AS candidate.");
            }
            if (!point.at("measurement_id").is_null()) {
                const auto m=AnalysisMeasurement(p,point.at("measurement_id").get<pl::Id>());
                if (m.metadata.at("sample_id")!=it->at("sample_id") || m.metadata.at("kind")!="FrequencySweep" || !m.metadata.at("names").is_array()) throw std::runtime_error("Invalid point measurement.");
                const auto &rows=*m.rowIds;
                auto row=std::find(rows.begin(),rows.end(),point.at("row_id").get<pl::Id>());
                if (row==rows.end()) throw std::runtime_error("Missing point row.");
                const auto names=m.metadata.at("names").get<std::vector<std::string>>();
                const auto f=std::find(names.begin(),names.end(),"f");
                if (f==names.end() || m.metadata.at("temperature_K").is_null()) throw std::runtime_error("Missing point T/f.");
                point["frequency_Hz"]=m.columns.at(std::distance(names.begin(),f))->at(std::distance(rows.begin(),row));
                point["temperature_K"]=m.metadata.at("temperature_K");
            }
            point["id"]=p.engine->reserveId(); point["enabled"]=true;
            it->at("points").push_back(std::move(point));
        } else if (action=="remove" || action=="enable") {
            auto &points=it->at("points");
            auto point=std::find_if(points.begin(),points.end(),[&](const auto &v) {return v.at("id")==edit.at("point_id");});
            if (point==points.end()) throw std::runtime_error("Missing defect point.");
            if (action=="remove") points.erase(point); else (*point)["enabled"]=edit.at("enabled");
        } else throw std::runtime_error("Unknown defect edit.");
    }
    Validate(p);
}
inline void DeleteMeasurement(Project &p, pl::Id id) {
    const bool found=std::any_of(p.measurements.begin(),p.measurements.end(),[&](const auto &m) {return m.at("id")==id;}) ||
        std::any_of(p.sweeps.begin(),p.sweeps.end(),[&](const auto &m) {return m.id==id;});
    if (!found) throw std::runtime_error("Missing measurement.");
    // Keep engine objects used by saved graphs; remove the domain input without rebinding IDs.
    p.measurements.erase(std::remove_if(p.measurements.begin(),p.measurements.end(),[&](const auto &m) {return m.at("id")==id;}),p.measurements.end());
    p.sweeps.erase(std::remove_if(p.sweeps.begin(),p.sweeps.end(),[&](const auto &m) {return m.id==id;}),p.sweeps.end());
}
inline void RefreshAnalyses(Project &p) {
    for (auto &a : p.analyses) {
        bool missing = false, stale = false;
        for (const auto &input : p.analysisInputs.at(a.at("id").get<pl::Id>())) {
            try {
                bool sameSample=false;
                stale |= !SameAnalysisMeasurement(p,input,sameSample);
                missing |= !sameSample;
            }
            catch (const std::exception &) { missing = true; }
        }
        if (a.contains("circuit")) {
            bool found=false;
            for (const auto &c : p.circuits) if (c.at("id")==a.at("circuit").at("id")) { found=true; stale |= c!=a.at("circuit"); }
            missing |= !found;
        }
        if (a.contains("defect")) {
            bool found=false;
            for (const auto &d : p.defects) if (d.at("id")==a.at("defect").at("id")) { found=true; stale |= d!=a.at("defect"); }
            missing |= !found;
        }
        if (a.contains("source_analyses")) for (const auto &source : a.at("source_analyses")) {
            bool found=false;
            for (const auto &current : p.analyses) if (current.at("id")==source.at("id")) {
                found=true; stale |= current!=source || current.at("status")!="valid";
            }
            missing |= !found;
        }
        if (a.contains("historical")) {
            bool found=false;
            for (const auto &h : p.historical) if (h.at("sample_id")==a.at("historical").at("sample_id")) {found=true; stale |= h!=a.at("historical");}
            missing |= !found;
        }
        a["status"] = missing ? "missing_input" : stale ? "stale" : a.at("computed_status").get<std::string>();
    }
}
inline pl::Id AnalysisPlot(Project &p, pl::Id workbook, AnalysisPlotData spec) {
    const std::string &name = spec.name;
    const auto graph = p.engine->reserveId(), layer = p.engine->reserveId();
    pl::Layer l; l.spec.x.label = spec.xLabel; l.spec.y.label = spec.yLabel; l.spec.autoFit = sst::ui::plot::AutoFit::Once;
    l.spec.equalAspect=spec.equalAspect;
    if (spec.logX) l.spec.x.scale=sst::ui::plot::AxisScale::Log10;
    if (spec.logY) l.spec.y.scale=sst::ui::plot::AxisScale::Log10;
    FormatCompareAxes(l.spec);
    std::vector<pl::Object> objects{{graph,p.engine->root(),name,0,pl::Graph{}},{layer,graph,name,0,l}};
    for (auto &curve : spec.curves) {
        auto sheet = p.engine->reserveId(), x = p.engine->reserveId(), y = p.engine->reserveId();
        pl::Worksheet w;
        if (curve.temperature_K) p.sources[sheet].temperatures["temperature_K"]=curve.temperature_K;
        if (curve.integerTemperature) p.sources[sheet].integerTemperatures["temperature_K"]=*curve.integerTemperature;
        for(std::size_t i=0;i<curve.x.size();++i)w.rows.push_back(p.engine->reserveId());
        pl::Column cx,cy;cx.values=std::move(curve.x);cy.values=std::move(curve.y);
        cx.role = pl::Role::X; cy.role = pl::Role::Y;
        pl::Plot plot{sheet,x,y}; plot.kind = sst::ui::plot::SeriesKind::LineScatter;
        if (curve.fit) plot.kind=sst::ui::plot::SeriesKind::Line;
        if (curve.candidates) plot.kind=sst::ui::plot::SeriesKind::Scatter;
        plot.style.marker=sst::ui::plot::Marker::Circle;
        plot.style.color=curve.color;
        objects.push_back({sheet,workbook,name,0,std::move(w)});
        objects.push_back({x,sheet,"x",0,std::move(cx)}); objects.push_back({y,sheet,"y",0,std::move(cy)});
        objects.push_back({p.engine->reserveId(),layer,curve.name,0,plot});
    }
    auto op = p.engine->change(name,std::move(objects)); op.apply(*p.engine); return graph;
}
inline void AppendAnalysis(Project &p, AnalysisResult result) {
    auto &record=result.record;
    const auto id=p.engine->reserveId();record["id"]=id;
    record["inputs"]=Json::array();for(const auto &m:result.inputs)record["inputs"].push_back(m.metadata);
    if (record.contains("x_d") && record.at("type")!="Walter") record["x_d"]["fit_id"] = record.at("type")=="DLCPPolynomial" && record.at("x_d").at("measurement_id")==record.at("inputs").at(0).at("id") ? record.at("id") : Json(nullptr);
    record["graphs"]=Json::array();
    const auto sample=record.contains("sample_id")?record.at("sample_id").get<pl::Id>():record.at("inputs").at(0).at("sample_id").get<pl::Id>();
    for(auto &plot:result.plots) {
        std::size_t i=0;
        for(auto &curve:plot.curves) {
            Json temperature=Nullable(curve.temperature_K);
            if(temperature.is_null())temperature=record.value("temperature_K",Json(nullptr));
            if(temperature.is_null() && !result.inputs.empty())temperature=result.inputs.front().metadata.value("temperature_K",Json(nullptr));
            if(record.at("type")=="Walter" && i<record.at("spectra").size())temperature=record.at("spectra").at(i).value("temperature_K",Json(nullptr));
            if(temperature.is_number_integer())curve.integerTemperature=temperature.get<std::uint64_t>();
            curve.temperature_K=temperature.is_null()?std::optional<double>{}:std::optional<double>{temperature.get<double>()};
            curve.color=SampleTemperatureColor(sample,temperature);++i;
        }
        record["graphs"].push_back(AnalysisPlot(p,sample,std::move(plot)));
    }
    p.graph=record.at("graphs").at(0).get<pl::Id>();
    p.analysisInputs[id]=std::move(result.inputs);
    p.analyses.push_back(std::move(record));
}
}
