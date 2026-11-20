#pragma once
#include "Domain/CapLabBundleImport.hpp"
#include "Domain/CapLabAnalysis.hpp"
namespace SST::Prototypes::CapLab {
inline Json SourceMetadata(const Project &p, pl::Id id) {
    (void)AnalysisMeasurement(p,id);
    const auto &source=std::get<pl::Worksheet>(p.engine->object(id).data).source;
    if (source.contains("provenance")) return Json::parse(source.at("provenance"));
    return {{"source",source.at("path")},{"sha256",source.at("sha256")},{"profile",source.at("profile")},
        {"decimal",source.at("decimal")},{"temperature_K",Json::parse(source.at("temperature_K"))}};
}
inline void ConfirmMetadata(const Json &preview, bool confirmed) {
    if (confirmed) return;
    auto check=[](const Json &measurement) {
        if (!measurement.value("metadata_conflicts",Json::array()).empty())
            throw std::runtime_error("Confirm metadata conflicts before importing.");
    };
    if (preview.contains("samples")) {
        for (const auto &s:preview.at("samples")) for (const auto &m:s.at("measurements")) check(m);
    } else check(preview);
}
inline Json ReplaceMeasurement(Project &p, pl::Id id, const Json &preview) {
    if (preview.at("samples").size()!=1 || !preview.at("historical").empty())
        throw std::runtime_error("Re-import requires one source sample, without historical results.");
    auto old=AnalysisMeasurement(p,id);
    const auto origin=SourceMetadata(p,id);
    std::vector<pl::Id> legacyColumns;
    for (const auto &s:p.sweeps) if (s.id==id) {
        legacyColumns={s.frequency,s.capacitance,s.resistance};
        for (auto cid:p.engine->children(id)) if (std::find(legacyColumns.begin(),legacyColumns.end(),cid)==legacyColumns.end()) {
            const auto &object=p.engine->object(cid);
            if (const auto *column=std::get_if<pl::Column>(&object.data)) {
                legacyColumns.push_back(cid); old["names"].push_back(object.name); old["units"].push_back(column->units);
                old["columns"].push_back(std::get<std::vector<double>>(column->values));
            }
        }
    }
    const auto &items=preview.at("samples").at(0).at("measurements");
    const auto block=origin.value("block_index",std::size_t{0});
    if (block>=items.size()) throw std::runtime_error("Source block no longer exists.");
    const auto &m=items.at(block);
    if (m.at("kind")!=old.at("kind") || m.at("variant")!=old.at("variant") || m.at("names")!=old.at("names") || m.at("units")!=old.at("units"))
        throw std::runtime_error("Re-import changed measurement schema; import as a new measurement instead.");
    auto table=MeasurementTable(m);
    auto object=p.engine->object(id);
    auto &sheet=std::get<pl::Worksheet>(object.data);
    const auto originalRows=sheet.rows;
    const bool gridChanged=old.at("source_rows")!=m.at("source_rows") || old.at("columns").at(0)!=m.at("columns").at(0);
    if (gridChanged) {
        sheet.rows.clear();
        for (std::size_t i=0;i<m.at("source_rows").size();++i) sheet.rows.push_back(p.engine->reserveId());
        sheet.masks.clear(); sheet.filters="[]"; sheet.visibleRows.clear();
    }
    auto meta=m; meta.erase("columns");
    meta["normalization_area_m2"]=origin.value("normalization_area_m2",old.at("area_m2"));
    for (auto key:{"groups","sweep_segments"}) if (meta.contains(key)) for (auto &group:meta[key]) {
        Json rows=Json::array(); for (const auto &r:group.at("rows")) rows.push_back(sheet.rows.at(r.get<std::size_t>()));
        group.erase("rows"); group["row_ids"]=std::move(rows);
    }
    table.source["provenance"]=meta.dump(); sheet.source=table.source;
    std::vector<pl::Id> columns;
    for (const auto &item:p.measurements) if (item.at("id")==id) columns=item.at("columns").get<std::vector<pl::Id>>();
    if (columns.empty()) columns=legacyColumns;
    std::vector<pl::Object> objects{std::move(object)};
    std::size_t changed=0;
    Json cells=Json::array();
    for (std::size_t i=0;i<columns.size();++i) {
        auto column=p.engine->object(columns[i]);
        for (std::size_t r=0;r<std::max(old.at("columns").at(i).size(),m.at("columns").at(i).size());++r) {
            const auto before=r<old.at("columns").at(i).size() ? old.at("columns").at(i).at(r) : Json(nullptr);
            const auto after=r<m.at("columns").at(i).size() ? m.at("columns").at(i).at(r) : Json(nullptr);
            if (before!=after) {
                ++changed;
                // shortcut: preview lists 100 cells; add paging if larger interactive diffs are needed.
                if (cells.size()<100) cells.push_back({{"column",table.names[i]},{"unit",m.at("units").at(i)},
                    {"row_index",r},{"before",before},{"after",after}});
            }
        }
        column.name=table.names[i]; column.data=table.columns[i]; objects.push_back(std::move(column));
    }
    auto label=[](const Json &source, const Json &temperature) {
        auto name=Path::FromResolved(std::filesystem::u8path(source.get<std::string>()).stem()).Utf8();
        if (!temperature.is_null()) name+=", T = "+temperature.dump()+" K";
        return name;
    };
    const auto oldLabel=label(origin.at("source"),old.at("temperature_K")), newLabel=label(m.at("source"),m.at("temperature_K"));
    if (oldLabel!=newLabel) for (const auto &[key,value]:p.engine->objects())
        if (const auto *plot=std::get_if<pl::Plot>(&value->data); plot && plot->worksheet==id && plot->x==columns[0] && plot->y==columns[1] && value->name==oldLabel) {
            auto object=*value; object.name=newLabel; objects.push_back(std::move(object));
        }
    p.engine->change("Re-import measurement",std::move(objects)).apply(*p.engine);
    bool found=false;
    for (auto &item:p.measurements) if (item.at("id")==id) {item["metadata"]=meta; found=true;}
    if (!found) {
        p.measurements.push_back({{"id",id},{"sample_id",old.at("sample_id")},{"columns",columns},{"metadata",meta}});
        std::erase_if(p.sweeps,[&](const auto &s) {return s.id==id;});
    }
    if (gridChanged) {
        // Changed grids never silently bind approved defect points to different rows.
        for (auto &d:p.defects) for (auto &point:d.at("points")) if (point.at("measurement_id")==id) {
            point["previous_measurement_id"]=id; point["previous_row_id"]=point.at("row_id");
            point["measurement_id"]=nullptr; point["row_id"]=nullptr; point["enabled"]=false;
            point["origin"]="reimport_unresolved";
        }
        std::function<void(Json&)> unresolve=[&](Json &v) {
            if (v.is_array()) {for (auto &item:v) unresolve(item);}
            else if (v.is_object()) {
                if (v.value("measurement_id",Json(nullptr))==id && v.contains("row_id")) {v["row_id"]=nullptr; v["reference_status"]="unresolved";}
                for (auto &[key,item]:v.items()) unresolve(item);
            }
        };
        unresolve(p.historical);
    }
    RefreshAnalyses(p); Validate(p);
    return {{"measurement_id",id},{"old_rows",originalRows.size()},{"new_rows",m.at("source_rows").size()},
        {"changed_cells",changed},{"cell_changes",cells},{"cell_changes_truncated",changed>cells.size()},
        {"grid_changed",gridChanged},{"old_sha256",origin.at("sha256")},{"new_sha256",m.at("sha256")},
        {"old_metadata",origin},{"new_metadata",meta}};
}
inline void RelinkSource(Project &p, pl::Id id, const Json &preview) {
    const auto origin=SourceMetadata(p,id);
    if (preview.at("sha256")!=origin.at("sha256")) throw std::runtime_error("Relink requires the same source hash; use re-import for changed data.");
    const auto old=origin.at("source"), replacement=preview.at("source");
    std::function<void(Json&)> relink=[&](Json &v) {
        if (v.is_array()) {for (auto &item:v) relink(item);}
        else if (v.is_object()) {
            if (v.contains("source") && v.at("source")==old) v["source"]=replacement;
            for (auto &[key,item]:v.items()) relink(item);
        }
    };
    for (auto &m:p.measurements) relink(m["metadata"]);
    relink(p.analyses); relink(p.imports); relink(p.historical);
    std::vector<pl::Object> objects;
    for (const auto &[key,value]:p.engine->objects()) if (const auto *sheet=std::get_if<pl::Worksheet>(&value->data)) {
        auto object=*value; auto &source=std::get<pl::Worksheet>(object.data).source;
        if (source.contains("provenance")) {auto meta=Json::parse(source.at("provenance")); relink(meta); source["provenance"]=meta.dump();}
        if (source.contains("path") && source.at("path")==old.get<std::string>()) source["path"]=replacement.get<std::string>();
        if (source!=sheet->source) objects.push_back(std::move(object));
    }
    if (!objects.empty()) p.engine->change("Relink source",std::move(objects)).apply(*p.engine);
    Validate(p);
}
}
