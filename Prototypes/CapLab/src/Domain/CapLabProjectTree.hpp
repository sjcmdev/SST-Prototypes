#pragma once
#include "Domain/CapLabAnalysis.hpp"
#include "Domain/CapLabIS.hpp"

namespace SST::Prototypes::CapLab {
inline Sample &FindSample(Project &p, pl::Id id) {
    const auto it=std::find_if(p.samples.begin(),p.samples.end(),[&](const auto &s) {return s.id==id;});
    if (it==p.samples.end()) throw std::runtime_error("Missing sample_id.");
    return *it;
}
inline void MoveMeasurements(Project &p, const std::vector<pl::Id> &ids, pl::Id target) {
    FindSample(p,target);
    if (ids.empty() || std::set<pl::Id>(ids.begin(),ids.end()).size()!=ids.size()) throw std::runtime_error("Expected distinct measurement_ids.");
    std::vector<pl::Object> objects;
    for (auto id:ids) {
        (void)AnalysisMeasurement(p,id);
        auto object=p.engine->object(id); object.parent=target; objects.push_back(std::move(object));
        for (auto &m:p.measurements) if (m.at("id")==id) m["sample_id"]=target;
        for (auto &m:p.sweeps) if (m.id==id) m.sample_id=target;
    }
    auto change=p.engine->change("Move measurements",std::move(objects)); change.apply(*p.engine);
    for (auto &h:p.historical) if (h.at("sample_id")!=target) {
        auto moved=[&](const Json &value) {return value.is_number_unsigned() && std::find(ids.begin(),ids.end(),value.get<pl::Id>())!=ids.end();};
        auto remember=[&](const Json &id,const Json &row) {
            if (!h.contains("moved_references")) h["moved_references"]=Json::array();
            h["moved_references"].push_back({{"original_measurement_id",id},{"original_row_id",row},{"target_sample_id",target}});
        };
        std::function<void(Json&)> detach=[&](Json &value) {
            if (value.is_array()) {for (auto &v:value) detach(v); return;}
            if (!value.is_object()) return;
            if (value.contains("measurement_id") && moved(value.at("measurement_id"))) {
                remember(value.at("measurement_id"),value.value("row_id",Json(nullptr)));
                value["measurement_id"]=nullptr;
                if (value.contains("row_id")) value["row_id"]=nullptr;
            }
            if (value.contains("selections")) for (auto &[key,id]:value.at("selections").items())
                if (moved(id)) {remember(id,nullptr); id=nullptr;}
            if (value.contains("plot_browser_settings") && value.at("plot_browser_settings").is_object())
                for (auto &[key,entries]:value.at("plot_browser_settings").items()) for (auto &id:entries)
                    if (moved(id)) {remember(id,nullptr); id=nullptr;}
            for (auto &[key,v]:value.items()) detach(v);
        };
        detach(h.at("data"));
    }
    // Keep the original IDs and measured values when a point leaves its defect's sample.
    for (auto &d:p.defects) for (auto &point:d.at("points"))
        if (!point.at("measurement_id").is_null() && d.at("sample_id")!=target &&
            std::find(ids.begin(),ids.end(),point.at("measurement_id").get<pl::Id>())!=ids.end()) {
            point["provenance"]=point.at("provenance").get<std::string>()+"; measurement moved: "+point.at("measurement_id").dump();
            point["moved_from"]={{"measurement_id",point.at("measurement_id")},{"row_id",point.at("row_id")},{"sample_id",d.at("sample_id")}};
            point["enabled"]=false;
            point["measurement_id"]=nullptr; point["row_id"]=nullptr;
        }
}
inline void DuplicateSample(Project &p, pl::Id source, const std::string &name) {
    auto sample=FindSample(p,source);
    std::map<pl::Id,pl::Id> ids;
    auto reserve=[&](pl::Id id) { if (id && !ids.contains(id)) ids[id]=p.engine->reserveId(); };
    std::function<void(pl::Id)> collect=[&](pl::Id id) {
        reserve(id);
        const auto &o=p.engine->object(id);
        if (const auto *sheet=std::get_if<pl::Worksheet>(&o.data)) {
            for (auto row:sheet->rows) reserve(row);
            for (const auto &parameter:sheet->parameters) reserve(parameter.id);
        }
        for (auto child:p.engine->children(id)) collect(child);
    };
    collect(source);
    for (const auto &a:p.analyses) if (AnalysisSample(a)==source) {
        reserve(a.at("id")); for (const auto &g:a.at("graphs")) collect(g.get<pl::Id>());
    }
    for (const auto &d:p.defects) if (d.at("sample_id")==source) {
        reserve(d.at("id")); for (const auto &point:d.at("points")) reserve(point.at("id"));
    }
    for (const auto &c:p.circuits) if (c.at("sample_id")==source) reserve(c.at("id"));
    for (const auto &h:p.historical) if (h.at("sample_id")==source) {
        const auto &data=h.at("data");
        for (const auto &d:data.value("defects",Json::array())) reserve(d.at("id"));
        if (data.contains("walter") && data.at("walter").is_object())
            for (const auto &f:data.at("walter").value("fits",Json::array())) reserve(f.at("id"));
    }
    auto mapped=[&](pl::Id id) {return ids.contains(id) ? ids.at(id) : id;};
    std::function<void(Json&,std::string)> remap=[&](Json &j,std::string key) {
        if (j.is_object()) { for (auto &[k,v]:j.items()) remap(v,key=="selections" || key=="plot_browser_settings" ? key : k); }
        else if (j.is_array()) { for (auto &v:j) remap(v,key); }
        else if (j.is_number_unsigned() && (key=="id" || key.ends_with("_id") || key.ends_with("_ids") || key=="graphs" || key=="columns" || key=="selections" || key=="plot_browser_settings")) j=mapped(j.get<pl::Id>());
    };
    std::vector<pl::Object> objects;
    for (const auto &[oldId,newId]:ids) if (p.engine->objects().contains(oldId)) {
        auto o=p.engine->object(oldId); o.id=newId; o.parent=mapped(o.parent);
        if (oldId==source) o.name=name;
        auto remapKeys=[&](auto &values) {
            auto copy=values; values.clear();
            for (const auto &[key,value]:copy) values.emplace(mapped(key),value);
        };
        if (auto *w=std::get_if<pl::Worksheet>(&o.data)) {
            for (auto &r:w->rows) r=mapped(r);
            for (auto &parameter:w->parameters) parameter.id=mapped(parameter.id);
            remapKeys(w->masks);
        }
        if (auto *column=std::get_if<pl::Column>(&o.data)) {
            column->errorOf=mapped(column->errorOf); remapKeys(column->masks); remapKeys(column->parameters);
            if (column->formula) {
                for (auto &[key,id]:column->formula->bindings) id=mapped(id);
                for (auto &[key,id]:column->formula->parameterBindings) id=mapped(id);
                for (auto &id:column->formula->scriptInputs) id=mapped(id);
            }
        }
        if (auto *plot=std::get_if<pl::Plot>(&o.data)) {
            plot->worksheet=mapped(plot->worksheet); plot->x=mapped(plot->x); plot->y=mapped(plot->y);
            plot->z=mapped(plot->z); plot->yError=mapped(plot->yError); plot->label=mapped(plot->label);
        }
        objects.push_back(std::move(o));
    }
    auto change=p.engine->change("Duplicate sample",std::move(objects)); change.apply(*p.engine);
    sample.id=ids.at(source); sample.name=name; p.samples.push_back(sample);
    const auto sweeps=p.sweeps;
    for (auto m:sweeps) if (m.sample_id==source) {
        m.id=mapped(m.id); m.sample_id=sample.id; m.frequency=mapped(m.frequency); m.capacitance=mapped(m.capacitance); m.resistance=mapped(m.resistance); p.sweeps.push_back(m);
    }
    for (auto *items:{&p.measurements,&p.defects,&p.circuits,&p.analyses,&p.historical}) {
        const auto original=*items;
        for (auto item:original) if ((items==&p.analyses ? AnalysisSample(item) : item.at("sample_id").get<pl::Id>())==source) {
            remap(item,""); items->push_back(std::move(item));
        }
    }
}
inline void EditProjectTree(Project &p, const Json &edit) {
    const std::string action=edit.at("action");
    if (action=="move_measurements") MoveMeasurements(p,edit.at("measurement_ids").get<std::vector<pl::Id>>(),edit.at("target_sample_id"));
    else if (action=="delete_analysis") {
        for (const auto &a:p.analyses) if (a.at("id")==edit.at("analysis_id"))
            for (const auto &g:a.at("graphs")) if (g==p.graph) p.graph=0;
        const auto count=p.analyses.size();
        p.analyses.erase(std::remove_if(p.analyses.begin(),p.analyses.end(),[&](const auto &a) {return a.at("id")==edit.at("analysis_id");}),p.analyses.end());
        if (count==p.analyses.size()) throw std::runtime_error("Missing analysis_id.");
    } else if (action=="delete_circuit") EditCircuit(p,{{"action","delete"},{"circuit_id",edit.at("circuit_id")}});
    else if (action=="create_sample") {
        const std::string name=edit.at("name");
        if (name.find_first_not_of(" \t\r\n")==std::string::npos) throw std::runtime_error("Sample name is required.");
        const auto id=p.engine->reserveId(); auto op=p.engine->change("Create sample",{{id,p.engine->root(),name,0,pl::Workbook{}}}); op.apply(*p.engine);
        p.samples.push_back({id,name,std::nullopt});
    } else {
        const auto id=edit.at("sample_id").get<pl::Id>(); FindSample(p,id);
        if (action=="rename_sample" || action=="duplicate_sample") {
            const std::string name=edit.at("name");
            if (name.find_first_not_of(" \t\r\n")==std::string::npos) throw std::runtime_error("Sample name is required.");
            if (action=="duplicate_sample") DuplicateSample(p,id,name);
            else {
                FindSample(p,id).name=name; auto object=p.engine->object(id); object.name=name;
                auto op=p.engine->change("Rename sample",{object}); op.apply(*p.engine);
            }
        } else if (action=="sample_metadata") {
            auto &sample=FindSample(p,id);
            if (edit.contains("area_m2")) {
                const auto area=Optional(edit.at("area_m2"));
                for (auto &m:p.measurements) if (m.at("sample_id")==id &&
                    (!m.at("metadata").contains("normalization_area_m2") || m.at("metadata").at("normalization_area_m2").is_null()))
                    m["metadata"]["normalization_area_m2"]=Nullable(sample.area_m2 ? sample.area_m2 : area);
                sample.area_m2=area;
            }
            for (const auto *key:{"material","notes"}) if (edit.contains(key)) sample.metadata[key]=edit.at(key).get<std::string>();
        } else if (action=="merge_samples") {
            const auto target=edit.at("target_sample_id").get<pl::Id>(); FindSample(p,target);
            if (id==target) throw std::runtime_error("Choose a different target sample.");
            std::vector<pl::Id> measurements;
            for (const auto &m:p.measurements) if (m.at("sample_id")==id) measurements.push_back(m.at("id"));
            for (const auto &m:p.sweeps) if (m.sample_id==id) measurements.push_back(m.id);
            for (auto *items:{&p.defects,&p.circuits}) for (auto &item:*items) if (item.at("sample_id")==id) item["sample_id"]=target;
            if (!measurements.empty()) MoveMeasurements(p,measurements,target);
            // Preserve source metadata and historical records under their original identity.
        } else if (action=="delete_sample") {
            for (const auto &a:p.analyses) if (AnalysisSample(a)==id)
                for (const auto &g:a.at("graphs")) if (g==p.graph) p.graph=0;
            std::vector<pl::Id> measurements;
            for (const auto &m:p.measurements) if (m.at("sample_id")==id) measurements.push_back(m.at("id"));
            for (const auto &m:p.sweeps) if (m.sample_id==id) measurements.push_back(m.id);
            for (auto mid:measurements) DeleteMeasurement(p,mid);
            for (auto *items:{&p.defects,&p.circuits,&p.historical,&p.analyses})
                items->erase(std::remove_if(items->begin(),items->end(),[&](const auto &item) {return (items==&p.analyses ? AnalysisSample(item) : item.at("sample_id").template get<pl::Id>())==id;}),items->end());
            std::erase_if(p.samples,[&](const auto &s) {return s.id==id;});
        } else throw std::runtime_error("Unknown tree action.");
    }
    RefreshAnalyses(p); Validate(p);
}
}
