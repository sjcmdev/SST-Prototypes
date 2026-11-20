#pragma once
#include "Storage/CapLabDocument.hpp"
#include "Domain/CapLabPlotStyle.hpp"
namespace SST::Prototypes::CapLab {
inline pl::Id AnalysisSample(const Json &a) {
    if (a.contains("sample_id")) return a.at("sample_id");
    if (a.contains("defect")) return a.at("defect").at("sample_id");
    return a.at("inputs").at(0).at("sample_id");
}
inline Json AnalysisMeasurement(const Project &p, pl::Id id) {
    for (const auto &m : p.measurements) if (m.at("id") == id) {
        auto data = m.at("metadata"); data["id"] = id; data["columns"] = Json::array();
        for (const auto &column : m.at("columns"))
            data["columns"].push_back(std::get<std::vector<double>>(std::get<pl::Column>(p.engine->object(column.get<pl::Id>()).data).values));
        data["row_ids"] = std::get<pl::Worksheet>(p.engine->object(id).data).rows;
        for (const auto &s : p.samples) if (m.at("sample_id") == s.id) {
            data["sample_id"] = s.id; data["area_m2"] = Nullable(s.area_m2); data["sample_metadata"] = s.metadata;
        }
        return data;
    }
    for (const auto &m : p.sweeps) if (m.id==id) {
        Json data{{"id",id},{"sample_id",m.sample_id},{"kind","FrequencySweep"},{"variant","Cp_Rp"},
            {"temperature_K",Nullable(m.temperature_K)},{"names",{"f","C","Rp"}},{"units",{"Hz","F","Ohm"}},
            {"columns",Json::array()},{"row_ids",std::get<pl::Worksheet>(p.engine->object(id).data).rows}};
        for (const auto &sample : p.samples) if (sample.id==m.sample_id) {
            data["area_m2"]=Nullable(sample.area_m2); data["sample_metadata"]=sample.metadata;
        }
        data["source_rows"]=Json::parse(std::get<pl::Worksheet>(p.engine->object(id).data).source.at("source_rows"));
        for (auto cid : {m.frequency,m.capacitance,m.resistance}) data["columns"].push_back(std::get<std::vector<double>>(std::get<pl::Column>(p.engine->object(cid).data).values));
        return data;
    }
    throw std::runtime_error("Missing analysis measurement.");
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
                if (m.at("sample_id")!=it->at("sample_id") || m.at("kind")!="FrequencySweep" || !m.at("names").is_array()) throw std::runtime_error("Invalid point measurement.");
                const auto rows=m.at("row_ids").get<std::vector<pl::Id>>();
                auto row=std::find(rows.begin(),rows.end(),point.at("row_id").get<pl::Id>());
                if (row==rows.end()) throw std::runtime_error("Missing point row.");
                const auto names=m.at("names").get<std::vector<std::string>>();
                const auto f=std::find(names.begin(),names.end(),"f");
                if (f==names.end() || m.at("temperature_K").is_null()) throw std::runtime_error("Missing point T/f.");
                point["frequency_Hz"]=m.at("columns").at(std::distance(names.begin(),f)).at(std::distance(rows.begin(),row));
                point["temperature_K"]=m.at("temperature_K");
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
        for (const auto &input : a.at("inputs")) {
            try {
                const auto current=AnalysisMeasurement(p,input.at("id").get<pl::Id>());
                missing |= current.at("sample_id")!=input.at("sample_id");
                stale |= current!=input;
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
inline pl::Id AnalysisPlot(Project &p, pl::Id workbook, const Json &spec) {
    const std::string name = spec.at("name");
    const auto graph = p.engine->reserveId(), layer = p.engine->reserveId();
    pl::Layer l; l.spec.x.label = spec.at("x_label"); l.spec.y.label = spec.at("y_label"); l.spec.autoFit = sst::ui::plot::AutoFit::Once;
    l.spec.equalAspect=spec.value("equal_aspect",false);
    if (spec.value("log_x",false)) l.spec.x.scale=sst::ui::plot::AxisScale::Log10;
    if (spec.value("log_y",false)) l.spec.y.scale=sst::ui::plot::AxisScale::Log10;
    FormatCompareAxes(l.spec);
    std::vector<pl::Object> objects{{graph,p.engine->root(),name,0,pl::Graph{}},{layer,graph,name,0,l}};
    for (const auto &curve : spec.at("curves")) {
        auto sheet = p.engine->reserveId(), x = p.engine->reserveId(), y = p.engine->reserveId();
        pl::Worksheet w;
        if (curve.contains("temperature_K")) w.source["temperature_K"]=curve.at("temperature_K").dump();
        for (std::size_t i=0; i<curve.at("x").size(); ++i) w.rows.push_back(p.engine->reserveId());
        pl::Column cx, cy; auto values=[](const Json &array) {std::vector<double> out; for (const auto &v : array) out.push_back(v.is_null() ? std::numeric_limits<double>::quiet_NaN() : v.get<double>()); return out;};
        cx.values=values(curve.at("x")); cy.values=values(curve.at("y"));
        cx.role = pl::Role::X; cy.role = pl::Role::Y;
        pl::Plot plot{sheet,x,y}; plot.kind = sst::ui::plot::SeriesKind::LineScatter;
        if (curve.value("fit",false)) plot.kind=sst::ui::plot::SeriesKind::Line;
        if (curve.value("candidates",false)) plot.kind=sst::ui::plot::SeriesKind::Scatter;
        plot.style.marker=sst::ui::plot::Marker::Circle;
        plot.style.color = curve.value("fit",false) ? sst::ui::plot::Color{.85f,.25f,.12f,1} : sst::ui::plot::Color{.12f,.45f,.78f,1};
        if (curve.contains("color")) plot.style.color=curve.at("color").get<sst::ui::plot::Color>();
        objects.push_back({sheet,workbook,name,0,w});
        objects.push_back({x,sheet,"x",0,cx}); objects.push_back({y,sheet,"y",0,cy});
        objects.push_back({p.engine->reserveId(),layer,curve.at("name").get<std::string>(),0,plot});
    }
    auto op = p.engine->change(name,std::move(objects)); op.apply(*p.engine); return graph;
}
inline void AppendAnalysis(Project &p, Json result) {
    result["id"] = p.engine->reserveId();
    if (result.contains("x_d") && result.at("type")!="Walter") result["x_d"]["fit_id"] = result.at("type")=="DLCPPolynomial" && result.at("x_d").at("measurement_id")==result.at("inputs").at(0).at("id") ? result.at("id") : Json(nullptr);
    result["graphs"] = Json::array();
    const auto sample=result.contains("sample_id") ? result.at("sample_id").get<pl::Id>() : result.at("inputs").at(0).at("sample_id").get<pl::Id>();
    for (auto plot : result.at("plots")) {
        std::size_t i=0;
        for (auto &curve:plot.at("curves")) {
            Json temperature=curve.value("temperature_K",result.value("temperature_K",Json(nullptr)));
            if (temperature.is_null() && !result.at("inputs").empty()) temperature=result.at("inputs").at(0).value("temperature_K",Json(nullptr));
            if (result.at("type")=="Walter" && i<result.at("spectra").size()) temperature=result.at("spectra").at(i).value("temperature_K",Json(nullptr));
            curve["temperature_K"]=temperature; curve["color"]=SampleTemperatureColor(sample,temperature); ++i;
        }
        result["graphs"].push_back(AnalysisPlot(p,sample,plot));
    }
    p.graph = result.at("graphs").at(0).get<pl::Id>();
    result.erase("plots"); p.analyses.push_back(std::move(result));
}
}
